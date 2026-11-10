#include "flight_task.h"
#include <math.h>
#include "esp_task_wdt.h"
#include "../config/config_v5.h"
#include "../core/flight_fsm.h"
#include "../core/pyro_logic.h"
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
static bool s_recovery = false;
static uint8_t s_persisted_mask = 0;   // channels commanded before this reset

TaskHandle_t flightTaskHandle() { return s_handle; }

static void flightTask(void*) {
  FlightFsm fsm;
  fsm.begin(defaultFsmConfig(), s_recovery);
  PyroLogic pyro(PYRO_FIRE_PULSE_MS);
  // Requirement A: a recovery boot locks the pyro layer out BEFORE the first
  // tick. There is no unlock path. (Second barrier after the FSM.)
  if (s_recovery) pyro.lockout();
  if (s_persisted_mask & 0x01) pyro.markSpent(PYRO_CH1_DROGUE);
  if (s_persisted_mask & 0x02) pyro.markSpent(PYRO_CH2_MAIN);

  esp_task_wdt_add(nullptr);   // subscribe this task; timeout unchanged (sdkconfig 5 s)

  FlightSnapshot s;
  s.recovery_boot = s_recovery;
  s.post_fire_latched = s_persisted_mask != 0 || g_post_fire.load();
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

    // ---- Pyro (gate first, then persist: flash never delays a fire)
    uint8_t fired_mask = 0;
    if (out.fire_drogue && pyroFire(pyro, PYRO_CH1_DROGUE, now)) fired_mask |= 0x01;
    if (out.fire_main && pyroFire(pyro, PYRO_CH2_MAIN, now)) fired_mask |= 0x02;
    bool fired_now = fired_mask != 0;
    pyroService(pyro, now);
    if (fired_now) {
      g_post_fire = true;
      s.post_fire_latched = true;
      nvsSetPostFire(fired_mask);
    }
    if (out.entered_boost) {
      g_flight_latched = true;   // WiFi/OTA task sees this and shuts down for good
      nvsSetFlightInProgress();
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
    // fired() includes channels marked spent from the persisted mask.
    s.pyro1_fired = pyro.fired(PYRO_CH1_DROGUE) || (s_persisted_mask & 0x01);
    s.pyro2_fired = pyro.fired(PYRO_CH2_MAIN) || (s_persisted_mask & 0x02);
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

void flightTaskStart(bool recovery_boot, uint8_t persisted_fired_mask) {
  s_recovery = recovery_boot;
  s_persisted_mask = persisted_fired_mask;
  xTaskCreatePinnedToCore(flightTask, "flight", 8192, nullptr, TASK_PRIO_FLIGHT, &s_handle,
                          TASK_CORE_FLIGHT);
}

}  // namespace ignis
