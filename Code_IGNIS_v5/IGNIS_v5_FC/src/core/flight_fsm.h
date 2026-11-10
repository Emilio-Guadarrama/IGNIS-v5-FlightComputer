// =============================================================================
// IGNIS v5: flight_fsm.h  (pure C++, host-testable)  SAFETY-CRITICAL
//
// Flight state machine. Ported from the reference
// IGNIS_FC_heavy_13/FlightState.h (lines cited in flight_fsm.cpp).
//
// The FSM never touches hardware. It returns fire REQUESTS in FsmOutput;
// main hands them to pyro_hw, which applies its own one-shot latch and
// RECOVERY lockout. Interlocks enforced here:
//   - No fire request before apogee is detected (barometric falling window or
//     APOGEE_TIMEOUT failsafe), and only after BOOST -> COAST.
//   - MIN_APOGEE_ALT_M gate (peak AGL since liftoff) on BOTH apogee paths.
//   - Drogue -> main stagger MAIN_MIN_DELAY_AFTER_DROGUE_MS.
//   - SINGLE_EVENT_MODE: main is never requested.
//   - RECOVERY is terminal and produces no requests.
//   - Continuity is NOT a firing gate (reference Pyro.h:11-16). It only selects
//     SAFE vs ARMED on the pad.
// =============================================================================
#pragma once
#include <stdint.h>
#include "flight_types.h"

namespace ignis {

struct FsmConfig {
  float    boost_accel_g;
  uint32_t boost_debounce_ms;
  float    boost_baro_climb_mps;
  uint32_t boost_baro_sustain_ms;
  uint32_t baro_vel_window_ms;
  float    burnout_accel_g;
  uint32_t burnout_timeout_ms;
  uint8_t  apogee_falling_samples;      // 2..8
  uint32_t apogee_min_sample_interval_ms;
  uint32_t apogee_timeout_ms;
  bool     apogee_timeout_enabled;
  float    min_apogee_alt_m;
  float    main_deploy_alt_agl_m;
  uint32_t main_min_delay_after_drogue_ms;
  bool     single_event_mode;
  float    landed_alt_delta_m;
  uint32_t landed_stable_ms;
  uint16_t ground_cal_samples;
  uint32_t safe_return_debounce_ms;
};

// Built from config_v5.h (GROUND_TEST_MODE resolved here, once).
FsmConfig defaultFsmConfig();

struct FsmInput {
  uint32_t now_ms = 0;
  bool  imu_valid = false;
  float accel_total_g = 0;
  bool  baro_valid = false;
  float pressure_pa = 0;
  float baro_alt_m = 0;
  Continuity cont;
};

struct FsmOutput {
  bool fire_drogue = false;     // request: drive CH1
  bool fire_main = false;       // request: drive CH2
  bool state_changed = false;
  bool entered_boost = false;   // main persists flight_in_progress to NVS on this
};

class FlightFsm {
 public:
  void begin(const FsmConfig& cfg, bool recovery_boot);
  FsmOutput step(const FsmInput& in);

  FlightState state() const { return state_; }
  float agl() const { return agl_m_; }
  float maxAgl() const { return max_agl_; }
  float baroVelocity() const { return baro_vel_mps_; }
  bool groundCaptured() const { return ground_captured_; }
  float groundPressure() const { return ground_pressure_pa_; }
  uint8_t deployTrigger() const { return deploy_trigger_; }
  bool drogueRequested() const { return drogue_requested_; }
  bool mainRequested() const { return main_requested_; }
  bool recoveryBoot() const { return recovery_boot_; }

 private:
  static constexpr int kMaxFalling = 8;
  void captureGround(const FsmInput& in);
  void updateBaroVelocity(const FsmInput& in);
  void pushAlt(float alt);
  bool isMonotonicFalling() const;
  void enterApogee(uint32_t now, uint8_t trigger, FsmOutput& out);
  void setState(FlightState s, FsmOutput& out);

  FsmConfig cfg_{};
  FlightState state_ = FlightState::SAFE;
  bool recovery_boot_ = false;

  double ground_sum_ = 0;
  uint16_t ground_n_ = 0;
  float ground_pressure_pa_ = 0;
  bool ground_captured_ = false;
  float agl_m_ = 0;
  float last_baro_alt_m_ = 0;

  float alt_samples_[kMaxFalling] = {};
  int alt_count_ = 0;
  uint32_t last_alt_sample_ms_ = 0;
  bool have_alt_sample_time_ = false;

  uint32_t boost_above_since_ms_ = 0; bool boost_above_ = false;
  uint32_t baro_climb_since_ms_ = 0;  bool baro_climb_ = false;
  uint32_t no_cont_since_ms_ = 0;     bool no_cont_ = false;
  uint32_t landed_since_ms_ = 0;      bool landed_timing_ = false;
  float last_landed_alt_ = 0;

  uint32_t boost_entered_ms_ = 0;
  uint32_t coast_entered_ms_ = 0;
  uint32_t drogue_fired_ms_ = 0;

  float baro_vel_base_alt_ = 0;
  uint32_t baro_vel_base_ms_ = 0;
  bool baro_vel_have_base_ = false;
  float baro_vel_mps_ = 0;

  float max_agl_ = 0;
  uint8_t deploy_trigger_ = DEPLOY_NONE;
  bool drogue_requested_ = false;
  bool main_requested_ = false;
};

// Barometric altitude from a pressure ratio (reference Sensors.h:251-253).
float altitudeFromPressure(float pressure_pa, float reference_pa);

}  // namespace ignis
