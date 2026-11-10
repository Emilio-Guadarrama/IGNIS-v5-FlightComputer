#include "ota_gate.h"

namespace ignis {

uint32_t otaDenyReasons(const OtaGateInput& in) {
  uint32_t r = OTA_OK;
  if (!in.ota_built) r |= OTA_DENY_BUILD;
  if (!in.cont.valid) r |= OTA_DENY_CONT_INVALID;  // fail closed
  // Even with an invalid reading, also report raw channel bits for the log.
  if (in.cont.ch1) r |= OTA_DENY_CONT_CH1;
  if (in.cont.ch2) r |= OTA_DENY_CONT_CH2;
  if (in.state != FlightState::SAFE) r |= OTA_DENY_NOT_SAFE;
  if (in.flight_latched || isFlightOrAfter(in.state)) r |= OTA_DENY_FLIGHT;
  if (in.post_fire) r |= OTA_DENY_POST_FIRE;
  if (in.recovery_boot) r |= OTA_DENY_RECOVERY;
  return r;
}

}  // namespace ignis
