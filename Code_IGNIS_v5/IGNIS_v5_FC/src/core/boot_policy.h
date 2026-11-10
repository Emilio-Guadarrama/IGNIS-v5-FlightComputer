// =============================================================================
// IGNIS v5 SINGLE-DEPLOY: boot_policy.h  (pure C++, host-testable)  SAFETY-CRITICAL
// Decide how to boot from the reset reason and the persisted flight state.
// Pure function, so every combination is unit-tested.
//
//   power-on reset (RBF cycle)      -> NORMAL, persisted state cleared
//   no flight recorded              -> NORMAL (pad)
//   phase BOOST/COAST, ground known -> RESUME_ASCENT  (deploy at apogee as normal)
//   phase DESCENT, ground known     -> RESUME_DESCENT (retry / backup only while airborne)
//   phase LANDED, unknown phase,
//   missing ground reference,
//   NVS and RTC both unreadable     -> RECOVERY (log + telemetry, pyro locked out)
// =============================================================================
#pragma once
#include <stdint.h>

namespace ignis {

struct PersistedFlight {
  uint8_t phase = 0;          // PersistPhase (flight_fsm.h): 0 none, 1 BOOST, 2 COAST, 3 DESCENT, 4 LANDED
  float ground_pa = 0;        // ground pressure captured before liftoff (0 = unknown)
  uint8_t attempts[2] = {0, 0};  // pyro attempts already made per channel
  bool any() const { return phase != 0 || attempts[0] || attempts[1]; }
};

enum class BootMode : uint8_t { NORMAL = 0, RESUME_ASCENT = 1, RESUME_DESCENT = 2, RECOVERY = 3 };
const char* bootModeName(BootMode m);

struct BootDecision {
  BootMode mode = BootMode::NORMAL;
  bool clear_state = false;   // erase persisted flight state (only on a power-on reset)
  PersistedFlight merged;     // state to resume from (NVS and RTC merged)
  const char* why = "";
};

// Merge: the later phase and the larger attempt count win (RTC is written
// before NVS, so it can be newer by one step); ground pressure from NVS first.
PersistedFlight mergePersisted(const PersistedFlight& nvs, bool nvs_ok,
                               const PersistedFlight& rtc, bool rtc_valid);

BootDecision decideBoot(uint8_t reset_reason, const PersistedFlight& nvs, bool nvs_ok,
                        const PersistedFlight& rtc, bool rtc_valid);

const char* resetReasonName(uint8_t r);

}  // namespace ignis
