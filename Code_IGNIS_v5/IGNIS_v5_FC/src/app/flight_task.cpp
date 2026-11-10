#include "flight_task.h"
#include <math.h>
#include "esp_task_wdt.h"
#include "../config/config_v5.h"
#include "../core/flight_fsm.h"
#include "../core/pyro_sequencer.h"
#include "../hal/app_state.h"
#include "../hal/baro_dps368.h"
#include "../hal/gps_battery.h"
#include "../hal/i2c_bus.h"
#include "../hal/imu_lsm6dsv32x.h"
#include "../hal/lora_sx1276.h"
#include "../hal/nvs_state.h"
#include "../hal/pyro_hw.h"
#include "../hal/sd_logger.h"
#include "../net/wifi_dashboard.h"

namespace ignis {

static TaskHandle_t s_handle = nullptr;
static BootDecision s_boot;

TaskHandle_t flightTaskHandle() { return s_handle; }

static void flightTask(void*) {
  const PersistedFlight& pf = s_boot.merged;
  FlightFsm fsm;
  PyroSequencer pyro;
  pyro.begin(defaultPyroSeqConfig());
  switch (s_boot.mode) {
    case BootMode::RESUME_ASCENT:
    case BootMode::RESUME_DESCENT:
      fsm.beginResume(defaultFsmConfig(), pf.phase, pf.ground_pa);
      break;
    case BootMode::RECOVERY:
      fsm.begin(defaultFsmConfig(), true);
      break;
    default:
      fsm.begin(defaultFsmConfig(), false);
      break;
  }
  pyro.restore(pf.attempts[0], pf.attempts[1]);
  persistSeed(pf);
  // RECOVERY locks the pyro layer out BEFORE the first tick. No unlock path.
  // (Second barrier: the FSM never permits firing in RECOVERY either.)
  if (fsm.state() == FlightState::RECOVERY) pyro.lockout();
  else if (fsm.resumedDescent()) pyro.resumeAfterReboot(millis());

  esp_task_wdt_add(nullptr);   // subscribe this task; timeout unchanged (sdkconfig 5 s)

  FlightSnapshot s;
  s.recovery_boot = fsm.state() == FlightState::RECOVERY;
  s.boot_mode = (uint8_t)s_boot.mode;
  s.post_fire_latched = pf.attempts[0] || pf.attempts[1];
  s.reset_reason = g_boot.reset_reason;
  s.boot_count = g_boot.boot_count;

  uint16_t i2c_fail_ticks = 0;
  uint32_t tick = 0;
  int boost_axis = -1;              // 0=x 1=y 2=z, from gravity on the pad
  uint32_t last_tilt_ms = 0;
  TickType_t wake = xTaskGetTickCount();

  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(SENSOR_LOOP_INTERVAL_MS));
    uint32_t now = millis();
    s.timestamp_ms = now;

    // ---- Sensors (hold last-known-good on failure, reference Sensors.h:19-30)
    s.imu_valid = imuRead(s);
    s.baro_valid = baroRead(s);
    if (imuOk() && baroOk() && !s.imu_valid && !s.baro_valid) {
      if (++i2c_fail_ticks >= I2C_FAIL_RECOVERY_THRESHOLD) { i2cBusRecover(); i2c_fail_ticks = 0; }
    } else {
      i2c_fail_ticks = 0;
    }
    gpsPump(s);
    if ((tick % 5) == 0) s.battery_v = batteryReadV();
    s.cont = pyroReadContinuity();

    // ---- FSM
    FsmInput in;
    in.now_ms = now;
    in.imu_valid = s.imu_valid;
    in.accel_total_g = s.accel_total_g;
    in.baro_valid = s.baro_valid;
    in.pressure_pa = s.pressure_pa;
    in.baro_alt_m = s.baro_alt_m;
    in.cont = s.cont;
    FlightState before = fsm.state();
    FsmOutput out = fsm.step(in);

    // ---- Pyro sequence (GPIO first, then persist: flash never delays a fire)
    if (out.deploy_command) pyro.commandDeploy(now, s.cont);
    uint8_t started = pyro.service(now, s.cont, out.fire_permitted);
    pyroDrive(pyro);
    bool fired_now = started != 0;
    if (out.persist_phase == PHASE_BOOST) g_flight_latched = true;  // WiFi/OTA off for good
    if (out.persist_phase != PHASE_NONE) {
      persistPhase(out.persist_phase, out.persist_phase == PHASE_BOOST ? fsm.groundPressure() : 0.0f);
    }
    if (fired_now) {
      g_post_fire = true;
      s.post_fire_latched = true;
      persistAttempts(pyro.attempts(PYRO_CH1_PRIMARY), pyro.attempts(PYRO_CH2_BACKUP));
    }

    // ---- Tilt: integrated gyro, diagnostic only (reference FlightState.h:104-128)
    if (boost_axis < 0 && isPadState(fsm.state()) && s.imu_valid && tick > 25) {
      float ax = fabsf(s.accel_x_g), ay = fabsf(s.accel_y_g), az = fabsf(s.accel_z_g);
      boost_axis = (ax >= ay && ax >= az) ? 0 : (ay >= az ? 1 : 2);
    }
    if (isPadState(fsm.state()) || fsm.state() == FlightState::RECOVERY) {
      s.tilt_deg = 0;
      last_tilt_ms = now;
    } else if (s.imu_valid && boost_axis >= 0) {
      float dt = (now - last_tilt_ms) / 1000.0f;
      last_tilt_ms = now;
      if (dt > 0 && dt <= 0.5f) {
        float gx = s.gyro_x_dps, gy = s.gyro_y_dps, gz = s.gyro_z_dps;
        float off = boost_axis == 0 ? sqrtf(gy * gy + gz * gz)
                  : boost_axis == 1 ? sqrtf(gx * gx + gz * gz) : sqrtf(gx * gx + gy * gy);
        s.tilt_deg += off * dt;
      }
    }

    // ---- Snapshot
    s.state = fsm.state();
    s.agl_m = fsm.agl();
    s.max_agl_m = fsm.maxAgl();
    s.baro_vel_mps = fsm.baroVelocity();
    s.deploy_trigger = fsm.deployTrigger();
    // attempts() includes attempts restored from before a reboot.
    s.pyro_attempts[0] = pyro.attempts(PYRO_CH1_PRIMARY);
    s.pyro_attempts[1] = pyro.attempts(PYRO_CH2_BACKUP);
    s.pyro1_fired = s.pyro_attempts[0] > 0;
    s.pyro2_fired = s.pyro_attempts[1] > 0;
    s.sd = sdStatus();
    s.sd_rows_dropped = sdRowsDropped();
    s.lora_ok = loraOk();
    s.lora_last_tx_ok = loraLastTxOk();
    s.lora_tx_count = loraTxCount();
    s.lora_skipped = loraSkipped();
    s.wifi = wifiStatus();
    snapshotPublish(s);

    uint8_t flags = 0;
    if (out.state_changed || before != s.state || fired_now) flags |= SD_FLAG_FORCE_FLUSH;
    if (pyro.inFlushWindow(now, PYRO_FORCE_FLUSH_MS)) flags |= SD_FLAG_PYRO_WINDOW;
    sdEnqueue(s, flags);

    tick++;
    g_flight_loop_count++;
    esp_task_wdt_reset();
  }
}

void flightTaskStart(const BootDecision& boot) {
  s_boot = boot;
  xTaskCreatePinnedToCore(flightTask, "flight", 8192, nullptr, TASK_PRIO_FLIGHT, &s_handle,
                          TASK_CORE_FLIGHT);
}

}  // namespace ignis
