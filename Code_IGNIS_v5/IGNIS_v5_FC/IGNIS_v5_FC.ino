/*
  ============================================================================
  IGNIS v5 Flight Computer, SINGLE-DEPLOY variant: preliminary firmware (0.2.0-prelim)
  Ignitia Rocket Lab. ESP32-S3-WROOM-1-N16R8, Arduino-ESP32 3.3.12.

  HANDLE A POWERED BOARD AS LIVE PYRO. Arming is hardware (pyro opto +
  e-match continuity); the RBF power cut is the ONLY way to disable the board.
  This firmware has no software disable/kill path and no remote fire command.

  Boot sequence:
    1. Pyro gates driven LOW (before anything else).
    2. reset_reason + persisted flight state -> NORMAL / RESUME_ASCENT /
       RESUME_DESCENT / RECOVERY (core/boot_policy). State clears only on
       ESP_RST_POWERON (RBF power cycle).
    3. Identity: version, git hash, SHA-256 of the running app partition.
    4. Sensors, SPI bus, SD task, LoRa task, status IO.
    5. Flight task (core 1). In RECOVERY the pyro layer is locked out first.
    6. WiFi task (core 0, AP off until BOOT is held) unless RECOVERY.
    7. loop(): status LEDs/buzzer, serial status, OTA self-test, WiFi backstop.

  Layout: src/config (pins, constants), src/core (pure, host-tested),
  src/hal (drivers, tasks), src/net (dashboard, OTA), src/app (flight task,
  self-test). See CHANGES.md.
  ============================================================================
*/
#include <Arduino.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "src/app/flight_task.h"
#include "src/app/selftest.h"
#include "src/config/config_v5.h"
#include "src/config/pins_v5.h"
#include "src/config/version.h"
#include "src/core/boot_policy.h"
#include "src/hal/app_state.h"
#include "src/hal/baro_dps368.h"
#include "src/hal/gps_battery.h"
#include "src/hal/i2c_bus.h"
#include "src/hal/imu_lsm6dsv32x.h"
#include "src/hal/lora_sx1276.h"
#include "src/hal/nvs_state.h"
#include "src/hal/pyro_hw.h"
#include "src/hal/sd_logger.h"
#include "src/hal/spi_bus.h"
#include "src/hal/status_io.h"
#include "src/net/wifi_dashboard.h"

using namespace ignis;

// Requirement C: keep a freshly OTA'd image in PENDING_VERIFY until the
// self-test confirms it (see src/app/selftest.h). Overrides the weak symbol in
// the Arduino core (esp32-hal-misc.c, C linkage).
extern "C" bool verifyRollbackLater() { return true; }

static void computeAppSha256() {
  uint8_t sha[32];
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (run && esp_partition_get_sha256(run, sha) == ESP_OK) {
    for (int i = 0; i < 32; i++) snprintf(&g_boot.app_sha256[2 * i], 3, "%02x", sha[i]);
  }
}

void setup() {
  pyroHwInitSafe();   // FIRST: both gates LOW, continuity inputs configured

  Serial.begin(115200);
  delay(50);

  // ---- Reboot handling: reset reason + persisted flight state ----
  uint8_t rr = (uint8_t)esp_reset_reason();
  bool power_on = (rr == RESET_REASON_POWERON_VALUE);
  NvsBootRead nv = nvsBootRead(power_on);
  PersistedFlight rtc;
  bool rtc_valid = false;
  rtcMirrorRead(rtc, rtc_valid);
  BootDecision bd = decideBoot(rr, nv.pf, nv.ok, rtc, rtc_valid);
  if (bd.clear_state) persistClear();
  bool normal = (bd.mode == BootMode::NORMAL);
  bool recovery = (bd.mode == BootMode::RECOVERY);

  g_boot.reset_reason = rr;
  g_boot.boot_count = nv.boot_count;
  g_boot.boot_why = bd.why;
  g_recovery_boot = recovery;
  if (!normal) {
    g_flight_latched = true;   // WiFi/OTA never start after an in-flight reboot
    g_post_fire = bd.merged.attempts[0] || bd.merged.attempts[1];
  }

  computeAppSha256();

  Serial.println();
  Serial.println("================================================================");
  Serial.printf("%s %s %s  git %s\n", IGNIS_FW_NAME, IGNIS_FW_VARIANT, IGNIS_FW_VERSION, IGNIS_GIT_HASH);
  Serial.printf("app sha256 %s  (image %s)\n", g_boot.app_sha256, otaImageStateName());
  Serial.printf("boot_count %u  reset_reason %u %s  nvs %s\n", nv.boot_count, rr, resetReasonName(rr),
                nv.ok ? "ok" : "UNREADABLE");
  Serial.printf("boot mode: %s (%s)  phase %u  attempts %u/%u\n", bootModeName(bd.mode), bd.why,
                bd.merged.phase, bd.merged.attempts[0], bd.merged.attempts[1]);
  Serial.printf("build: %s FLIGHT=%d GROUND_TEST_MODE=%d WIFI=%d OTA=%d  pyro: pulse %d ms, backup +%d ms, max %d attempts/ch\n",
                IGNIS_FW_VARIANT, IGNIS_FLIGHT_BUILD, GROUND_TEST_MODE, IGNIS_ENABLE_WIFI, IGNIS_ENABLE_OTA,
                PYRO_FIRE_PULSE_MS, PYRO_BACKUP_DELAY_MS, PYRO_MAX_ATTEMPTS);
  Serial.println("!!! Powered board = LIVE PYRO. RBF power cut is the only disable. !!!");
  if (recovery) {
    Serial.println("################################################################");
    Serial.println("###  RECOVERY: reset after landing / without flight record.   ###");
    Serial.println("###  Logging + LoRa only. Pyro LOCKED OUT. WiFi/OTA off.      ###");
    Serial.println("###  Power-cycle (RBF) to return to normal operation.         ###");
    Serial.println("################################################################");
  }
  if (bd.mode == BootMode::RESUME_ASCENT || bd.mode == BootMode::RESUME_DESCENT) {
    Serial.println("###  RESUMED after an in-flight reset (WiFi/OTA off). Deploy only post-apogee;");
    Serial.println("###  after a reset in descent: retry/backup only if airborne + e-match intact.");
  }
#if GROUND_TEST_MODE
  Serial.println("###  GROUND_TEST_MODE ON: NOT FLIGHT CONFIG  ###");
#endif
  Serial.println("================================================================");

  // ---- Sensors ----
  i2cBusBegin();
  bool imu = imuBegin();
  bool baro = baroBegin();
  if (imu && normal) imuCalibrateGyroBias();   // never calibrate a possibly moving body
  gpsBegin();
  batteryBegin();
  Serial.printf("[BOOT] IMU %s  BARO %s  VBAT %.2f V\n", imu ? "OK" : "FAIL", baro ? "OK" : "FAIL", batteryReadV());

  // ---- Shared SPI bus, SD + LoRa tasks ----
  spiBusBegin();
  char banner[200];
  snprintf(banner, sizeof(banner), "%s %s %s git=%s sha256=%s boot=%u reset=%u(%s) mode=%s",
           IGNIS_FW_NAME, IGNIS_FW_VARIANT, IGNIS_FW_VERSION, IGNIS_GIT_HASH, g_boot.app_sha256,
           nv.boot_count, rr, resetReasonName(rr), bootModeName(bd.mode));
  sdLoggerStart(banner);
  loraStart();
  statusBegin();

  // ---- Flight task (core 1) ----
  flightTaskStart(bd);

  // ---- WiFi dashboard (core 0), only on a normal (pad) boot ----
  if (normal) wifiDashboardStart();

  Serial.println("[BOOT] complete");
}

void loop() {
  static uint32_t last_ui = 0, last_print = 0;
  static FlightState last_state = FlightState::RECOVERY;
  static bool first = true;
  uint32_t now = millis();

  if (now - last_ui >= 50) {
    last_ui = now;
    FlightSnapshot s;
    snapshotRead(s);
    statusUpdate(s, now);
    if (first || s.state != last_state) {
      Serial.printf("[FSM] -> %s  (agl %.1f m, trigger %u, fired %d/%d)\n", flightStateName(s.state),
                    s.agl_m, s.deploy_trigger, s.pyro1_fired, s.pyro2_fired);
      last_state = s.state;
      first = false;
    }
    if (now - last_print >= SERIAL_STATUS_INTERVAL_MS) {
      last_print = now;
      Serial.printf("[STAT] %s agl %.1f p %.0f vbat %.2f cont %d/%d%s imu %d baro %d gps %d/%u lora %d tx %lu sd %u drop %lu wifi %u\n",
                    flightStateName(s.state), s.agl_m, s.pressure_pa, s.battery_v, s.cont.ch1, s.cont.ch2,
                    s.cont.valid ? "" : "(INVALID)", s.imu_valid, s.baro_valid, s.gps_fix, s.gps_sats,
                    s.lora_ok, (unsigned long)s.lora_tx_count, (unsigned)s.sd,
                    (unsigned long)s.sd_rows_dropped, (unsigned)s.wifi);
    }
  }
  selftestService(now);
  wifiBackstop(now);
  delay(10);
}
