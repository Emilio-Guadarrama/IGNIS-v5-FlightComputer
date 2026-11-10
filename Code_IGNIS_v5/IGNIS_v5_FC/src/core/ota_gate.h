// =============================================================================
// IGNIS v5: ota_gate.h  (pure C++, host-testable)  SAFETY-CRITICAL
// Requirement C: OTA is refused if ANY of these is true:
//   continuity on any pyro channel, continuity reading invalid (fail closed),
//   state != SAFE, flight detected (latched), post-fire, recovery boot.
// Evaluated before an upload starts AND on every received chunk; a non-zero
// result mid-upload aborts it.
// =============================================================================
#pragma once
#include <stdint.h>
#include "flight_types.h"

namespace ignis {

enum OtaDeny : uint32_t {
  OTA_OK                 = 0,
  OTA_DENY_CONT_CH1      = 1u << 0,
  OTA_DENY_CONT_CH2      = 1u << 1,
  OTA_DENY_CONT_INVALID  = 1u << 2,
  OTA_DENY_NOT_SAFE      = 1u << 3,
  OTA_DENY_FLIGHT        = 1u << 4,
  OTA_DENY_POST_FIRE     = 1u << 5,
  OTA_DENY_RECOVERY      = 1u << 6,
  OTA_DENY_BUILD         = 1u << 7,  // OTA compiled out
};

struct OtaGateInput {
  FlightState state = FlightState::RECOVERY;  // default = denied
  Continuity cont;                            // default valid=false = denied
  bool flight_latched = true;                 // default = denied
  bool post_fire = true;
  bool recovery_boot = true;
  bool ota_built = false;
};

uint32_t otaDenyReasons(const OtaGateInput& in);
inline bool otaAllowed(const OtaGateInput& in) { return otaDenyReasons(in) == OTA_OK; }

}  // namespace ignis
