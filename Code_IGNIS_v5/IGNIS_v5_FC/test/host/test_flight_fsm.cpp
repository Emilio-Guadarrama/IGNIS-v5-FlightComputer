// Host tests for the flight FSM (simulated sensor profiles; no hardware).
#include <cmath>
#include "mini_test.h"
#include "../../src/core/flight_fsm.h"

using namespace ignis;

namespace {

const float kP0 = 86000.0f;  // ~1350 m site

float pressureAt(float agl_m) {
  // Inverse of altitudeFromPressure relative to the pad.
  return kP0 * std::pow(1.0f - agl_m / 44330.0f, 1.0f / 0.19029495f);
}

struct Sim {
  FlightFsm fsm;
  uint32_t t;
  int drogue = 0, main_ = 0, boosts = 0;
  explicit Sim(bool recovery = false, bool single = true, uint32_t t0 = 1000) : t(t0) {
    FsmConfig c = defaultFsmConfig();
    c.single_event_mode = single;
    fsm.begin(c, recovery);
  }
  FsmOutput tick(float agl, float accel, bool imu_ok = true, bool baro_ok = true,
                 Continuity cont = {true, false, true}) {
    FsmInput in;
    in.now_ms = t;
    in.imu_valid = imu_ok;
    in.accel_total_g = accel;
    in.baro_valid = baro_ok;
    in.pressure_pa = pressureAt(agl);
    in.baro_alt_m = 1350.0f + agl;
    in.cont = cont;
    FsmOutput o = fsm.step(in);
    drogue += o.fire_drogue;
    main_ += o.fire_main;
    boosts += o.entered_boost;
    t += 20;
    return o;
  }
  void pad(int ticks, Continuity c = {true, false, true}) {
    for (int i = 0; i < ticks; i++) tick(0.0f, 1.0f, true, true, c);
  }
  // Simple ballistic-ish profile: boost 2 s at 6 g, coast up to apogee, descend.
  void fly(float apogee, float descent_mps = 20.0f, bool baro_ok_after_boost = true) {
    for (int i = 0; i < 100; i++) tick(i * 0.5f, 6.0f);  // 2 s boost, climbs 50 m
    float alt = 50.0f;
    float v = std::sqrt(2.0f * 9.81f * (apogee - alt));
    while (v > 0) {
      alt += v * 0.02f;
      v -= 9.81f * 0.02f;
      tick(alt, 0.2f, true, baro_ok_after_boost);
    }
    for (int i = 0; i < 1500 && alt > 0; i++) {
      alt -= descent_mps * 0.02f;
      if (alt < 0) alt = 0;
      tick(alt, 1.0f, true, baro_ok_after_boost);
    }
    for (int i = 0; i < 400; i++) tick(0.0f, 1.0f, true, baro_ok_after_boost);
  }
};

}  // namespace

TEST(fsm_nominal_single_event_flight) {
  Sim s;
  s.pad(100);
  CHECK(s.fsm.state() == FlightState::ARMED);
  CHECK(s.fsm.groundCaptured());
  s.fly(1000.0f);
  CHECK_EQ(s.boosts, 1);
  CHECK_EQ(s.drogue, 1);
  CHECK_EQ(s.main_, 0);   // SINGLE_EVENT_MODE: CH2 never requested
  CHECK_EQ((int)s.fsm.deployTrigger(), (int)DEPLOY_BARO_APOGEE);
  CHECK(s.fsm.maxAgl() > 900.0f);
  CHECK(s.fsm.state() == FlightState::LANDED);
}

TEST(fsm_drogue_only_after_apogee) {
  Sim s;
  s.pad(100);
  for (int i = 0; i < 100; i++) s.tick(i * 0.5f, 6.0f);
  float alt = 50, v = 120;
  while (v > 1.0f) {  // still climbing: no request may appear
    alt += v * 0.02f; v -= 9.81f * 0.02f;
    FsmOutput o = s.tick(alt, 0.2f);
    CHECK(!o.fire_drogue && !o.fire_main);
  }
  CHECK_EQ(s.drogue, 0);
}

TEST(fsm_pulley_jerk_rejected_by_debounce) {
  Sim s;
  s.pad(100);
  for (int i = 0; i < 5; i++) s.tick(0, 2.2f);   // 100 ms above 1.8 g (FLIGHT018-like)
  s.pad(50);
  CHECK(s.fsm.state() == FlightState::ARMED);
  CHECK_EQ(s.boosts, 0);
}

TEST(fsm_min_altitude_interlock_blocks_ground_fire) {
  Sim s;
  s.pad(100);
  for (int i = 0; i < 20; i++) s.tick(0, 2.5f);   // 400 ms jerk -> false BOOST
  CHECK(s.fsm.state() != FlightState::ARMED);
  // Sits on the ground with falling noise for far longer than APOGEE_TIMEOUT.
  for (int i = 0; i < 2000; i++) s.tick((i % 2) ? 0.5f : -0.5f - i * 0.0001f, 1.0f);
  CHECK_EQ(s.drogue, 0);
  CHECK_EQ(s.main_, 0);
}

TEST(fsm_timeout_failsafe_when_baro_dies) {
  Sim s;
  s.pad(100);
  for (int i = 0; i < 100; i++) s.tick(i * 0.5f, 6.0f);
  for (int i = 0; i < 50; i++) s.tick(50.0f + i * 2.0f, 0.2f);  // coast to 150 m
  // Barometer dead from here: no valid samples, apogee window never closes.
  uint32_t coastStart = s.t;
  while (s.drogue == 0 && s.t - coastStart < 30000) s.tick(150.0f, 0.2f, true, false);
  CHECK_EQ(s.drogue, 1);
  CHECK_EQ((int)s.fsm.deployTrigger(), (int)DEPLOY_TIMEOUT_FAILSAFE);
  CHECK(s.t - coastStart >= 19000);  // ~APOGEE_TIMEOUT_MS after burnout
}

TEST(fsm_dual_event_main_altitude_and_stagger) {
  Sim s(false, /*single=*/false);
  s.pad(100);
  for (int i = 0; i < 100; i++) s.tick(i * 0.5f, 6.0f);
  float alt = 50, v = std::sqrt(2 * 9.81f * 950);
  while (v > 0) { alt += v * 0.02f; v -= 9.81f * 0.02f; s.tick(alt, 0.2f); }
  uint32_t drogueT = 0;
  for (int i = 0; i < 400 && s.drogue == 0; i++) { alt -= 0.4f; s.tick(alt, 1.0f); drogueT = s.t; }
  CHECK_EQ(s.drogue, 1);
  // Descend: main must not fire above 450 m AGL.
  while (alt > 0 && s.main_ == 0) {
    alt -= 0.4f;
    FsmOutput o = s.tick(alt, 1.0f);
    if (o.fire_main) { CHECK(alt <= 451.0f); CHECK(s.t - drogueT >= 1500); }
  }
  CHECK_EQ(s.main_, 1);
}

TEST(fsm_low_apogee_dual_event_enforces_stagger) {
  Sim s(false, false);
  s.pad(100);
  for (int i = 0; i < 100; i++) s.tick(i * 0.5f, 6.0f);  // 50 m
  float alt = 50, v = std::sqrt(2 * 9.81f * 50);           // apogee ~100 m < 450 m
  while (v > 0) { alt += v * 0.02f; v -= 9.81f * 0.02f; s.tick(alt, 0.2f); }
  uint32_t tDrogue = 0;
  for (int i = 0; i < 400 && s.main_ == 0; i++) {
    alt -= 0.1f;
    FsmOutput o = s.tick(alt, 1.0f);
    if (o.fire_drogue) { tDrogue = s.t; CHECK(!o.fire_main); }
    if (o.fire_main) CHECK(s.t - tDrogue >= 1500);
  }
  CHECK_EQ(s.drogue, 1);
  CHECK_EQ(s.main_, 1);
}

TEST(fsm_recovery_boot_never_requests_fire) {
  Sim s(/*recovery=*/true, /*single=*/false);
  s.pad(100);
  s.fly(1000.0f);
  CHECK(s.fsm.state() == FlightState::RECOVERY);
  CHECK_EQ(s.drogue, 0);
  CHECK_EQ(s.main_, 0);
  CHECK_EQ(s.boosts, 0);
}

TEST(fsm_continuity_safe_armed_fail_closed) {
  Sim s;
  s.pad(5, {false, false, true});
  CHECK(s.fsm.state() == FlightState::SAFE);
  s.pad(1, {false, false, false});           // invalid reading -> ARMED
  CHECK(s.fsm.state() == FlightState::ARMED);
  s.pad(40, {false, false, true});           // 800 ms open: still ARMED
  CHECK(s.fsm.state() == FlightState::ARMED);
  s.pad(15, {false, false, true});           // > 1000 ms open -> SAFE
  CHECK(s.fsm.state() == FlightState::SAFE);
  s.pad(1, {false, true, true});             // CH2 continuity alone -> ARMED
  CHECK(s.fsm.state() == FlightState::ARMED);
}

TEST(fsm_liftoff_detected_from_safe) {
  Sim s;
  s.pad(100, {false, false, true});
  CHECK(s.fsm.state() == FlightState::SAFE);
  for (int i = 0; i < 20; i++) s.tick(i * 0.5f, 6.0f, true, true, {false, false, true});
  CHECK(s.fsm.state() == FlightState::BOOST);
}

TEST(fsm_baro_backup_liftoff_with_dead_imu) {
  Sim s;
  s.pad(100);
  float alt = 0;
  for (int i = 0; i < 50 && s.fsm.state() == FlightState::ARMED; i++) {
    alt += 30.0f * 0.02f;  // 30 m/s climb, IMU dead
    s.tick(alt, 0.0f, /*imu_ok=*/false);
  }
  CHECK(s.fsm.state() == FlightState::BOOST);
}

TEST(fsm_invalid_baro_samples_do_not_close_apogee_window) {
  Sim s;
  s.pad(100);
  for (int i = 0; i < 100; i++) s.tick(i * 0.5f, 6.0f);
  for (int i = 0; i < 100; i++) s.tick(50.0f + i * 1.0f, 0.2f);  // climbing to 150 m
  // Corrupt "falling" samples flagged invalid must be ignored.
  for (int i = 0; i < 20; i++) s.tick(100.0f - i * 5.0f, 0.2f, true, false);
  CHECK_EQ(s.drogue, 0);
}

TEST(fsm_millis_rollover_is_safe) {
  Sim s(false, true, 0xFFFFF000u);  // wraps ~4 s into the run
  s.pad(100);
  s.fly(600.0f);
  CHECK_EQ(s.drogue, 1);
  CHECK_EQ(s.boosts, 1);
}
