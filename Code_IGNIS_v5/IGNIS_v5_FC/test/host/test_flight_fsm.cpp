// Host tests for the flight FSM + pyro sequencer together (simulated sensor
// profiles and simulated continuity; no hardware, nothing is fired).
#include <cmath>
#include "mini_test.h"
#include "../../src/core/flight_fsm.h"
#include "../../src/core/pyro_sequencer.h"

using namespace ignis;

namespace {

const float kP0 = 86000.0f;  // ~1350 m site

float pressureAt(float agl_m) {
  return kP0 * std::pow(1.0f - agl_m / 44330.0f, 1.0f / 0.19029495f);
}

// Simulated e-match: continuity until it "burns" (when burns_on_attempt
// reaches the attempt number on that channel). 0 = never burns.
struct Ematch { bool present = true; int burns_on_attempt = 1; };

struct Sim {
  FlightFsm fsm;
  PyroSequencer seq;
  uint32_t t;
  Ematch em[2];
  int deploy_cmds = 0;
  uint32_t first_gate_ms[2] = {0, 0};
  bool overlap = false;
  bool fired_while_climbing = false;
  float last_alt = 0;
  explicit Sim(bool recovery = false, uint32_t t0 = 1000) : t(t0) {
    fsm.begin(defaultFsmConfig(), recovery);
    seq.begin(defaultPyroSeqConfig());
    if (recovery) seq.lockout();
  }
  Continuity cont() const {
    Continuity c;
    c.valid = true;
    c.ch1 = em[0].present;
    c.ch2 = em[1].present;
    return c;
  }
  void tick(float agl, float accel, bool imu_ok = true, bool baro_ok = true, bool cont_valid = true) {
    FsmInput in;
    in.now_ms = t;
    in.imu_valid = imu_ok;
    in.accel_total_g = accel;
    in.baro_valid = baro_ok;
    in.pressure_pa = pressureAt(agl);
    in.baro_alt_m = 1350.0f + agl;
    in.cont = cont();
    if (!cont_valid) in.cont.valid = false;
    FsmOutput o = fsm.step(in);
    if (o.deploy_command) { deploy_cmds++; seq.commandDeploy(t, in.cont); }
    uint8_t started = seq.service(t, in.cont, o.fire_permitted);
    for (int ch = 0; ch < 2; ch++) {
      if (started & (1 << ch)) {
        if (!first_gate_ms[ch]) first_gate_ms[ch] = t;
        if (agl > last_alt + 0.01f) fired_while_climbing = true;
        int a = seq.attempts((PyroChannel)ch);
        if (em[ch].burns_on_attempt && a >= em[ch].burns_on_attempt) em[ch].present = false;
      }
    }
    if (seq.gateOn(PYRO_CH1_PRIMARY) && seq.gateOn(PYRO_CH2_BACKUP)) overlap = true;
    last_alt = agl;
    t += 20;
  }
  void pad(int ticks) { for (int i = 0; i < ticks; i++) tick(0.0f, 1.0f); }
  void fly(float apogee, float descent_mps = 20.0f) {
    for (int i = 0; i < 100; i++) tick(i * 0.5f, 6.0f);  // 2 s boost to 50 m
    float alt = 50.0f;
    float v = std::sqrt(2.0f * 9.81f * (apogee - alt));
    while (v > 0) { alt += v * 0.02f; v -= 9.81f * 0.02f; tick(alt, 0.2f); }
    for (int i = 0; i < 3000 && alt > 0; i++) {
      alt -= descent_mps * 0.02f;
      if (alt < 0) alt = 0;
      tick(alt, 1.0f);
    }
    for (int i = 0; i < 400; i++) tick(0.0f, 1.0f);
  }
  int att(int ch) const { return seq.attempts((PyroChannel)ch); }
};

}  // namespace

TEST(fsm_nominal_single_deploy_primary_then_backup) {
  Sim s;
  s.pad(100);
  CHECK(s.fsm.state() == FlightState::ARMED);
  s.fly(1000.0f);
  CHECK_EQ(s.deploy_cmds, 1);
  CHECK_EQ(s.att(0), 1);                // e-match burned open: no retry
  CHECK_EQ(s.att(1), 1);                // backup fires ALWAYS
  CHECK_EQ((long long)(s.first_gate_ms[1] - s.first_gate_ms[0]), 500LL);  // CH2 right after CH1 pulse
  CHECK(!s.overlap);
  CHECK(!s.fired_while_climbing);
  CHECK_EQ((int)s.fsm.deployTrigger(), (int)DEPLOY_BARO_APOGEE);
  CHECK(s.fsm.state() == FlightState::LANDED);
}

TEST(fsm_deploys_within_400ms_of_apogee) {
  Sim s;
  s.pad(100);
  for (int i = 0; i < 100; i++) s.tick(i * 0.5f, 6.0f);
  float alt = 50, v = std::sqrt(2 * 9.81f * 950);
  while (v > 0) { alt += v * 0.02f; v -= 9.81f * 0.02f; s.tick(alt, 0.2f); }
  uint32_t apogee_t = s.t;
  for (int i = 0; i < 100 && !s.first_gate_ms[0]; i++) { alt -= 0.4f; s.tick(alt, 0.2f); }
  CHECK(s.first_gate_ms[0] != 0);
  CHECK(s.first_gate_ms[0] - apogee_t <= 400);
}

TEST(fsm_retry_when_continuity_stays_then_cap) {
  Sim s;
  s.em[0].burns_on_attempt = 0;   // CH1 never burns open: retries until the cap
  s.em[1].burns_on_attempt = 2;   // CH2 burns on its second attempt
  s.pad(100);
  s.fly(800.0f);
  CHECK_EQ(s.att(0), 3);          // 1 + 2 retries
  CHECK_EQ(s.att(1), 2);
  CHECK(!s.overlap);
}

TEST(fsm_no_retry_if_channel_was_open_on_the_pad) {
  Sim s;
  s.em[1].present = false;        // CH2 never had continuity
  s.em[1].burns_on_attempt = 0;
  s.pad(100);
  s.fly(800.0f);
  CHECK_EQ(s.att(0), 1);
  CHECK_EQ(s.att(1), 1);          // backup still fires once (always), no retries
}

TEST(fsm_drogue_only_after_apogee) {
  Sim s;
  s.pad(100);
  for (int i = 0; i < 100; i++) s.tick(i * 0.5f, 6.0f);
  float alt = 50, v = 120;
  while (v > 1.0f) { alt += v * 0.02f; v -= 9.81f * 0.02f; s.tick(alt, 0.2f); }
  CHECK_EQ(s.att(0) + s.att(1), 0);
}

TEST(fsm_pulley_jerk_rejected_by_debounce) {
  Sim s;
  s.pad(100);
  for (int i = 0; i < 5; i++) s.tick(0, 2.2f);   // 100 ms above 1.8 g
  s.pad(50);
  CHECK(s.fsm.state() == FlightState::ARMED);
}

TEST(fsm_min_altitude_interlock_blocks_ground_fire) {
  Sim s;
  s.pad(100);
  for (int i = 0; i < 20; i++) s.tick(0, 2.5f);   // 400 ms jerk -> false BOOST
  CHECK(s.fsm.state() != FlightState::ARMED);
  for (int i = 0; i < 2000; i++) s.tick((i % 2) ? 0.5f : -0.5f - i * 0.0001f, 1.0f);
  CHECK_EQ(s.att(0) + s.att(1), 0);
}

TEST(fsm_timeout_failsafe_when_baro_dies) {
  Sim s;
  s.pad(100);
  for (int i = 0; i < 100; i++) s.tick(i * 0.5f, 6.0f);
  for (int i = 0; i < 50; i++) s.tick(50.0f + i * 2.0f, 0.2f);  // coast to 150 m
  uint32_t coastStart = s.t;
  while (s.att(0) == 0 && s.t - coastStart < 30000) s.tick(150.0f, 0.2f, true, false);
  CHECK_EQ(s.att(0), 1);
  CHECK_EQ((int)s.fsm.deployTrigger(), (int)DEPLOY_TIMEOUT_FAILSAFE);
  CHECK(s.t - coastStart >= 19000);
}

TEST(fsm_recovery_boot_never_fires) {
  Sim s(/*recovery=*/true);
  s.em[0].burns_on_attempt = 0;
  s.pad(100);
  s.fly(1000.0f);
  CHECK(s.fsm.state() == FlightState::RECOVERY);
  CHECK_EQ(s.att(0) + s.att(1), 0);
}

TEST(fsm_continuity_safe_armed_fail_closed) {
  Sim s;
  s.em[0].present = false; s.em[1].present = false;
  s.pad(5);
  CHECK(s.fsm.state() == FlightState::SAFE);
  s.tick(0, 1.0f, true, true, /*cont_valid=*/false);
  CHECK(s.fsm.state() == FlightState::ARMED);
  s.pad(40);
  CHECK(s.fsm.state() == FlightState::ARMED);
  s.pad(15);
  CHECK(s.fsm.state() == FlightState::SAFE);
  s.em[1].present = true;
  s.pad(1);
  CHECK(s.fsm.state() == FlightState::ARMED);
}

TEST(fsm_liftoff_detected_from_safe) {
  Sim s;
  s.em[0].present = false; s.em[1].present = false;
  s.pad(100);
  CHECK(s.fsm.state() == FlightState::SAFE);
  for (int i = 0; i < 20; i++) s.tick(i * 0.5f, 6.0f);
  CHECK(s.fsm.state() == FlightState::BOOST);
}

TEST(fsm_baro_backup_liftoff_with_dead_imu) {
  Sim s;
  s.pad(100);
  float alt = 0;
  for (int i = 0; i < 50 && s.fsm.state() == FlightState::ARMED; i++) {
    alt += 30.0f * 0.02f;
    s.tick(alt, 0.0f, /*imu_ok=*/false);
  }
  CHECK(s.fsm.state() == FlightState::BOOST);
}

TEST(fsm_invalid_baro_samples_do_not_close_apogee_window) {
  Sim s;
  s.pad(100);
  for (int i = 0; i < 100; i++) s.tick(i * 0.5f, 6.0f);
  for (int i = 0; i < 100; i++) s.tick(50.0f + i * 1.0f, 0.2f);
  for (int i = 0; i < 20; i++) s.tick(100.0f - i * 5.0f, 0.2f, true, false);
  CHECK_EQ(s.att(0), 0);
}

TEST(fsm_millis_rollover_is_safe) {
  Sim s(false, 0xFFFFF000u);
  s.pad(100);
  s.fly(600.0f);
  CHECK_EQ(s.att(0), 1);
  CHECK_EQ(s.att(1), 1);
}

// ---- Resume after an unwanted reboot ---------------------------------------

namespace {
struct Resumed {
  FlightFsm fsm;
  PyroSequencer seq;
  uint32_t t = 500;   // millis restarts after a reboot
  Ematch em[2];
  Resumed(uint8_t phase, uint8_t att1, uint8_t att2) {
    fsm.beginResume(defaultFsmConfig(), phase, kP0);
    seq.begin(defaultPyroSeqConfig());
    seq.restore(att1, att2);
    if (fsm.state() == FlightState::RECOVERY) seq.lockout();
    else if (fsm.resumedDescent()) seq.resumeAfterReboot(t);
  }
  void tick(float agl, bool baro_ok = true) {
    FsmInput in;
    in.now_ms = t;
    in.imu_valid = true;
    in.accel_total_g = 1.0f;
    in.baro_valid = baro_ok;
    in.pressure_pa = pressureAt(agl);
    in.baro_alt_m = 1350.0f + agl;
    in.cont.valid = true;
    in.cont.ch1 = em[0].present;
    in.cont.ch2 = em[1].present;
    FsmOutput o = fsm.step(in);
    if (o.deploy_command) seq.commandDeploy(t, in.cont);
    uint8_t st = seq.service(t, in.cont, o.fire_permitted);
    for (int ch = 0; ch < 2; ch++)
      if ((st & (1 << ch)) && em[ch].burns_on_attempt && seq.attempts((PyroChannel)ch) >= em[ch].burns_on_attempt)
        em[ch].present = false;
    t += 20;
  }
};
}  // namespace

TEST(resume_coast_still_deploys_at_apogee) {
  Resumed r(PHASE_COAST, 0, 0);
  CHECK(r.fsm.state() == FlightState::COAST);
  float alt = 400, v = 40;
  while (v > 0) { alt += v * 0.02f; v -= 9.81f * 0.02f; r.tick(alt); }
  for (int i = 0; i < 200; i++) { alt -= 0.4f; r.tick(alt); }
  CHECK_EQ((int)r.seq.attempts(PYRO_CH1_PRIMARY), 1);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH2_BACKUP), 1);
}

TEST(resume_descent_retries_intact_channel_while_airborne) {
  Resumed r(PHASE_DESCENT, 1, 0);   // CH1 fired once before the reboot, e-match still intact
  r.em[0].burns_on_attempt = 2;
  for (int i = 0; i < 200; i++) r.tick(300.0f - i * 0.4f);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH1_PRIMARY), 2);   // retried after reboot
  CHECK_EQ((int)r.seq.attempts(PYRO_CH2_BACKUP), 1);    // backup that never fired
}

TEST(resume_descent_no_refire_if_ematch_opened) {
  Resumed r(PHASE_DESCENT, 1, 1);
  r.em[0].present = false;
  r.em[1].present = false;
  for (int i = 0; i < 200; i++) r.tick(300.0f - i * 0.4f);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH1_PRIMARY), 1);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH2_BACKUP), 1);
}

TEST(resume_descent_on_the_ground_never_fires) {
  Resumed r(PHASE_DESCENT, 1, 0);   // e.g. impact reset before LANDED was persisted
  for (int i = 0; i < 600; i++) r.tick(0.5f);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH1_PRIMARY), 1);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH2_BACKUP), 0);
}

TEST(resume_descent_invalid_baro_never_fires) {
  Resumed r(PHASE_DESCENT, 1, 0);
  for (int i = 0; i < 600; i++) r.tick(300.0f, /*baro_ok=*/false);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH1_PRIMARY), 1);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH2_BACKUP), 0);
}

TEST(resume_attempt_cap_holds_across_reboot) {
  Resumed r(PHASE_DESCENT, 3, 3);
  r.em[0].burns_on_attempt = 0;
  r.em[1].burns_on_attempt = 0;
  for (int i = 0; i < 400; i++) r.tick(500.0f - i * 0.4f);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH1_PRIMARY), 3);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH2_BACKUP), 3);
}

TEST(resume_window_expires) {
  Resumed r(PHASE_DESCENT, 1, 0);
  r.em[0].burns_on_attempt = 0;
  for (int i = 0; i < 600; i++) r.tick(0.0f);           // 12 s on the ground: nothing
  for (int i = 0; i < 300; i++) r.tick(300.0f);          // later "airborne" reading: window closed
  CHECK_EQ((int)r.seq.attempts(PYRO_CH1_PRIMARY), 1);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH2_BACKUP), 0);
}

TEST(resume_landed_is_recovery) {
  Resumed r(PHASE_LANDED, 1, 1);
  r.em[0].burns_on_attempt = 0;
  for (int i = 0; i < 300; i++) r.tick(300.0f);
  CHECK(r.fsm.state() == FlightState::RECOVERY);
  CHECK_EQ((int)r.seq.attempts(PYRO_CH1_PRIMARY), 1);
}
