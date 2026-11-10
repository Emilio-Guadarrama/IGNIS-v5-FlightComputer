// Host tests: boot policy (reboot handling), OTA gate, pyro sequencer edge cases.
#include "mini_test.h"
#include "../../src/core/boot_policy.h"
#include "../../src/core/flight_fsm.h"
#include "../../src/core/ota_gate.h"
#include "../../src/core/pyro_sequencer.h"

using namespace ignis;

// ---- Boot policy ------------------------------------------------------------
static PersistedFlight P(uint8_t phase, float gnd = 86000.0f, uint8_t a1 = 0, uint8_t a2 = 0) {
  PersistedFlight p; p.phase = phase; p.ground_pa = gnd; p.attempts[0] = a1; p.attempts[1] = a2; return p;
}
static const PersistedFlight kNone{};

TEST(boot_poweron_clears_and_boots_normal) {
  BootDecision d = decideBoot(1, P(PHASE_DESCENT, 86000, 1, 1), true, P(PHASE_DESCENT), true);
  CHECK(d.mode == BootMode::NORMAL);
  CHECK(d.clear_state);
  CHECK(!decideBoot(1, kNone, true, kNone, true).clear_state);  // nothing to clear: no write
}

TEST(boot_non_poweron_phase_mapping) {
  const uint8_t reasons[] = {0, 2, 3, 4, 5, 6, 7, 9, 11, 12, 15};
  for (uint8_t r : reasons) {
    CHECK(decideBoot(r, kNone, true, kNone, true).mode == BootMode::NORMAL);
    CHECK(decideBoot(r, P(PHASE_BOOST), true, kNone, false).mode == BootMode::RESUME_ASCENT);
    CHECK(decideBoot(r, P(PHASE_COAST), true, kNone, false).mode == BootMode::RESUME_ASCENT);
    CHECK(decideBoot(r, P(PHASE_DESCENT, 86000, 1), true, kNone, false).mode == BootMode::RESUME_DESCENT);
    CHECK(decideBoot(r, P(PHASE_LANDED, 86000, 1, 1), true, kNone, false).mode == BootMode::RECOVERY);
    CHECK(decideBoot(r, P(9), true, kNone, false).mode == BootMode::RECOVERY);   // unknown phase
    CHECK(!decideBoot(r, P(PHASE_DESCENT), true, kNone, false).clear_state);
  }
}

TEST(boot_missing_ground_reference_is_recovery) {
  CHECK(decideBoot(9, P(PHASE_COAST, 0), true, kNone, false).mode == BootMode::RECOVERY);
  CHECK(decideBoot(9, P(PHASE_DESCENT, 0, 1), true, kNone, false).mode == BootMode::RECOVERY);
}

TEST(boot_unreadable_storage_fails_closed) {
  CHECK(decideBoot(6, kNone, false, kNone, false).mode == BootMode::RECOVERY);
  CHECK(decideBoot(1, kNone, false, kNone, false).mode == BootMode::NORMAL);
  // RTC alone is enough to resume
  CHECK(decideBoot(6, kNone, false, P(PHASE_COAST), true).mode == BootMode::RESUME_ASCENT);
}

TEST(boot_merge_takes_latest_phase_and_max_attempts) {
  // RTC is written before NVS: it can be one step ahead.
  PersistedFlight m = mergePersisted(P(PHASE_COAST, 86000), true, P(PHASE_DESCENT, 0, 1, 0), true);
  CHECK_EQ((int)m.phase, (int)PHASE_DESCENT);
  CHECK_EQ((int)m.attempts[0], 1);
  CHECK_NEAR(m.ground_pa, 86000.0, 1e-3);
  // Attempts recorded but phase lost -> treated as descent.
  PersistedFlight m2 = mergePersisted(P(PHASE_COAST, 86000, 0, 1), true, kNone, false);
  CHECK_EQ((int)m2.phase, (int)PHASE_DESCENT);
}

// ---- OTA gate ---------------------------------------------------------------
static OtaGateInput okInput() {
  OtaGateInput g;
  g.state = FlightState::SAFE;
  g.cont = {false, false, true};
  g.flight_latched = false;
  g.post_fire = false;
  g.recovery_boot = false;
  g.ota_built = true;
  return g;
}

TEST(ota_allowed_only_when_everything_safe) {
  CHECK(otaAllowed(okInput()));
  CHECK(!otaAllowed(OtaGateInput{}));
}

TEST(ota_denied_on_each_condition) {
  OtaGateInput g;
  g = okInput(); g.cont.ch1 = true;               CHECK(otaDenyReasons(g) & OTA_DENY_CONT_CH1);
  g = okInput(); g.cont.ch2 = true;               CHECK(otaDenyReasons(g) & OTA_DENY_CONT_CH2);
  g = okInput(); g.cont.valid = false;            CHECK(otaDenyReasons(g) & OTA_DENY_CONT_INVALID);
  g = okInput(); g.state = FlightState::ARMED;    CHECK(otaDenyReasons(g) & OTA_DENY_NOT_SAFE);
  g = okInput(); g.state = FlightState::LANDED;   CHECK(otaDenyReasons(g) & OTA_DENY_FLIGHT);
  g = okInput(); g.flight_latched = true;         CHECK(otaDenyReasons(g) & OTA_DENY_FLIGHT);
  g = okInput(); g.post_fire = true;              CHECK(otaDenyReasons(g) & OTA_DENY_POST_FIRE);
  g = okInput(); g.recovery_boot = true;          CHECK(otaDenyReasons(g) & OTA_DENY_RECOVERY);
  g = okInput(); g.state = FlightState::RECOVERY; CHECK(!otaAllowed(g));
  g = okInput(); g.ota_built = false;             CHECK(otaDenyReasons(g) & OTA_DENY_BUILD);
}

// ---- Pyro sequencer edge cases (direct) -------------------------------------
static Continuity C(bool c1, bool c2, bool v = true) { Continuity c; c.ch1 = c1; c.ch2 = c2; c.valid = v; return c; }

TEST(seq_nothing_before_command) {
  PyroSequencer s; s.begin(defaultPyroSeqConfig());
  for (uint32_t t = 0; t < 5000; t += 20) CHECK_EQ((int)s.service(t, C(true, true), true), 0);
}

TEST(seq_not_permitted_means_no_start) {
  PyroSequencer s; s.begin(defaultPyroSeqConfig());
  s.commandDeploy(0, C(true, true));
  for (uint32_t t = 0; t < 3000; t += 20) CHECK_EQ((int)s.service(t, C(true, true), false), 0);
  CHECK_EQ((int)s.attempts(PYRO_CH1_PRIMARY), 0);
}

TEST(seq_invalid_continuity_never_triggers_retry) {
  PyroSequencer s; s.begin(defaultPyroSeqConfig());
  s.commandDeploy(0, C(true, true));
  for (uint32_t t = 0; t < 8000; t += 20) s.service(t, C(true, true, /*valid=*/false), true);
  CHECK_EQ((int)s.attempts(PYRO_CH1_PRIMARY), 1);
  CHECK_EQ((int)s.attempts(PYRO_CH2_BACKUP), 1);
}

TEST(seq_single_gate_and_timing) {
  PyroSequencer s; s.begin(defaultPyroSeqConfig());
  s.commandDeploy(0, C(true, true));
  uint32_t starts[8]; int n = 0; int chs[8];
  for (uint32_t t = 0; t < 8000; t += 10) {
    uint8_t st = s.service(t, C(true, true), true);   // e-matches never open
    if (st && n < 8) { starts[n] = t; chs[n] = (st & 1) ? 0 : 1; n++; }
    CHECK(!(s.gateOn(PYRO_CH1_PRIMARY) && s.gateOn(PYRO_CH2_BACKUP)));
  }
  CHECK_EQ(n, 6);                 // 3 attempts x 2 channels
  CHECK_EQ(chs[0], 0); CHECK_EQ((int)starts[0], 0);
  CHECK_EQ(chs[1], 1); CHECK_EQ((int)starts[1], 500);
  CHECK_EQ(chs[2], 0); CHECK_EQ((int)starts[2], 1000);   // CH1 pulse end 500 + gap 500
  CHECK(starts[5] < 4000);        // whole sequence done within ~3 s
  CHECK(!s.gateOn(PYRO_CH1_PRIMARY) && !s.gateOn(PYRO_CH2_BACKUP));
}

TEST(seq_lockout_is_absolute) {
  PyroSequencer s; s.begin(defaultPyroSeqConfig());
  s.commandDeploy(0, C(true, true));
  s.service(0, C(true, true), true);
  CHECK(s.gateOn(PYRO_CH1_PRIMARY));
  s.lockout();
  CHECK(!s.gateOn(PYRO_CH1_PRIMARY));
  for (uint32_t t = 20; t < 5000; t += 20) CHECK_EQ((int)s.service(t, C(true, true), true), 0);
}

TEST(seq_window_bounds_activity) {
  PyroSeqConfig c = defaultPyroSeqConfig();
  PyroSequencer s; s.begin(c);
  s.commandDeploy(0, C(true, true));
  // Not permitted until after the window: nothing may start late.
  for (uint32_t t = 0; t < c.window_ms; t += 20) s.service(t, C(true, true), false);
  for (uint32_t t = c.window_ms; t < c.window_ms + 3000; t += 20) CHECK_EQ((int)s.service(t, C(true, true), true), 0);
}

TEST(seq_single_gate_even_when_a_retry_is_due_during_the_other_pulse) {
  PyroSeqConfig c = defaultPyroSeqConfig();
  c.retry_gap_ms = 100;   // CH1 retry becomes due while CH2's backup pulse is on
  PyroSequencer s; s.begin(c);
  s.commandDeploy(0, C(true, true));
  int starts = 0;
  for (uint32_t t = 0; t < 6000; t += 10) {
    if (s.service(t, C(true, true), true)) starts++;
    CHECK(!(s.gateOn(PYRO_CH1_PRIMARY) && s.gateOn(PYRO_CH2_BACKUP)));
  }
  CHECK_EQ(starts, 6);
}
