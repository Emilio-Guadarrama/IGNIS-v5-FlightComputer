// Host tests: boot policy (requirement A), OTA gate (requirement C), pyro latch.
#include "mini_test.h"
#include "../../src/core/boot_policy.h"
#include "../../src/core/ota_gate.h"
#include "../../src/core/pyro_logic.h"

using namespace ignis;

// ---- Boot policy ------------------------------------------------------------
static PersistFlags F(bool flight, bool fire) { PersistFlags p; p.flight_in_progress = flight; p.post_fire = fire; return p; }

TEST(boot_poweron_clears_flags_and_boots_normal) {
  BootDecision d = decideBoot(1, F(true, true), true, F(true, true), true);
  CHECK(d.mode == BootMode::NORMAL);
  CHECK(d.clear_flags);
}

TEST(boot_poweron_without_flags_does_not_write) {
  BootDecision d = decideBoot(1, F(false, false), true, F(false, false), true);
  CHECK(d.mode == BootMode::NORMAL);
  CHECK(!d.clear_flags);
}

TEST(boot_any_non_poweron_with_post_fire_is_recovery) {
  const uint8_t reasons[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 99};
  for (uint8_t r : reasons) {
    CHECK(decideBoot(r, F(false, true), true, F(false, false), false).mode == BootMode::RECOVERY);
    CHECK(decideBoot(r, F(true, false), true, F(false, false), false).mode == BootMode::RECOVERY);
    CHECK(!decideBoot(r, F(true, true), true, F(false, false), false).clear_flags);
  }
}

TEST(boot_rtc_mirror_alone_forces_recovery) {
  CHECK(decideBoot(5, F(false, false), true, F(false, true), true).mode == BootMode::RECOVERY);
  // Invalid RTC record is ignored.
  CHECK(decideBoot(5, F(false, false), true, F(true, true), false).mode == BootMode::NORMAL);
}

TEST(boot_nvs_unreadable_fails_closed) {
  CHECK(decideBoot(6, F(false, false), false, F(false, false), false).mode == BootMode::RECOVERY);
  CHECK(decideBoot(1, F(false, false), false, F(false, false), false).mode == BootMode::NORMAL);
}

TEST(boot_watchdog_on_pad_without_flags_is_normal) {
  CHECK(decideBoot(6, F(false, false), true, F(false, false), true).mode == BootMode::NORMAL);
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
  CHECK(!otaAllowed(OtaGateInput{}));  // defaults deny
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

TEST(ota_invalid_continuity_counts_as_armed) {
  OtaGateInput g = okInput();
  g.cont = {false, false, false};
  CHECK(!otaAllowed(g));
}

// ---- Pyro latch -------------------------------------------------------------
TEST(pyro_one_shot_and_pulse) {
  PyroLogic p(1000);
  CHECK(p.request(PYRO_CH1_DROGUE, 100));
  CHECK(p.gateOn(PYRO_CH1_DROGUE));
  CHECK(p.fired(PYRO_CH1_DROGUE));            // latched at the instant of firing
  CHECK(!p.request(PYRO_CH1_DROGUE, 200));    // one-shot
  CHECK_EQ((int)p.service(1099), 0);
  CHECK_EQ((int)p.service(1100), 1);
  CHECK(!p.gateOn(PYRO_CH1_DROGUE));
  CHECK(p.inFlushWindow(4000, 4000));
  CHECK(!p.inFlushWindow(4100, 4000));
}

TEST(pyro_lockout_blocks_everything) {
  PyroLogic p(1000);
  p.lockout();
  CHECK(!p.request(PYRO_CH1_DROGUE, 0));
  CHECK(!p.request(PYRO_CH2_MAIN, 0));
  CHECK(!p.gateOn(PYRO_CH1_DROGUE));
  CHECK(!p.fired(PYRO_CH1_DROGUE));
}

TEST(pyro_lockout_drops_an_active_gate) {
  PyroLogic p(1000);
  CHECK(p.request(PYRO_CH2_MAIN, 0));
  p.lockout();
  CHECK(!p.gateOn(PYRO_CH2_MAIN));
}

TEST(pyro_spent_channel_never_refires) {
  PyroLogic p(1000);
  p.markSpent(PYRO_CH1_DROGUE);
  CHECK(!p.request(PYRO_CH1_DROGUE, 0));
  CHECK(p.request(PYRO_CH2_MAIN, 0));
}

