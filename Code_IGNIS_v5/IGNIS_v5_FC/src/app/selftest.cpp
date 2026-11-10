#include "selftest.h"
#include <Arduino.h>
#include "esp_ota_ops.h"
#include "esp_task_wdt.h"
#include "flight_task.h"
#include "../hal/app_state.h"
#include "../hal/baro_dps368.h"
#include "../hal/imu_lsm6dsv32x.h"
#include "../hal/sd_logger.h"

namespace ignis {

static bool s_done = false;
static uint32_t s_loop_at_start = 0;
static bool s_armed_timer = false;
static uint32_t s_t0 = 0;

const char* otaImageStateName() {
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) != ESP_OK) return "UNDEFINED(no otadata)";
  switch (st) {
    case ESP_OTA_IMG_NEW: return "NEW";
    case ESP_OTA_IMG_PENDING_VERIFY: return "PENDING_VERIFY";
    case ESP_OTA_IMG_VALID: return "VALID";
    case ESP_OTA_IMG_INVALID: return "INVALID";
    case ESP_OTA_IMG_ABORTED: return "ABORTED";
    default: return "UNDEFINED";
  }
}

static SelfTestResult run() {
  SelfTestResult r;
  FlightSnapshot s;
  snapshotRead(s);
  r.imu = imuOk() && s.imu_valid;
  r.baro = baroOk() && s.baro_valid;
  r.sd = sdStatus() == SdStatus::LOGGING;
  r.cont = s.cont.valid;
  TaskHandle_t h = flightTaskHandle();
  r.wdt = h && esp_task_wdt_status(h) == ESP_OK;   // ESP_OK == subscribed
  r.loop = g_flight_loop_count.load() - s_loop_at_start >= 100;  // >= 2 s of 50 Hz ticks
  return r;
}

void selftestService(uint32_t now) {
  if (s_done) return;
  if (!s_armed_timer) { s_armed_timer = true; s_t0 = now; s_loop_at_start = g_flight_loop_count.load(); return; }
  if (now - s_t0 < 3000) return;
  s_done = true;

  SelfTestResult r = run();
  esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
  esp_ota_get_state_partition(esp_ota_get_running_partition(), &st);
  Serial.printf("[SELFTEST] imu=%d baro=%d sd=%d cont=%d wdt=%d loop=%d -> %s (image %s)\n",
                r.imu, r.baro, r.sd, r.cont, r.wdt, r.loop, r.pass() ? "PASS" : "FAIL",
                otaImageStateName());
  if (st != ESP_OTA_IMG_PENDING_VERIFY) return;   // not a fresh OTA image: report only

  if (r.pass()) {
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[SELFTEST] new image marked VALID");
    return;
  }
  if (g_recovery_boot || g_flight_latched) {
    Serial.println("[SELFTEST] FAIL but recovery/flight: not rebooting; image stays unconfirmed");
    return;
  }
  Serial.println("[SELFTEST] FAIL on new image: rolling back to previous image");
  Serial.flush();
  esp_ota_mark_app_invalid_rollback_and_reboot();
}

}  // namespace ignis
