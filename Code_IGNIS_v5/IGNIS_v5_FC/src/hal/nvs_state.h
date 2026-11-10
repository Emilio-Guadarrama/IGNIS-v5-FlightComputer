// =============================================================================
// IGNIS v5 SINGLE-DEPLOY: nvs_state.h  SAFETY-CRITICAL
// Persisted flight state (phase, ground pressure, pyro attempts per channel)
// in NVS, plus an RTC_NOINIT mirror written first (no flash delay).
// Flash wear: writes ONLY on transitions:
//   boot counter (1/boot), BOOST (phase + ground pressure), COAST, DESCENT,
//   each pyro attempt (max PYRO_MAX_ATTEMPTS per channel), LANDED,
//   and one clear on a power-on reset that finds state. No periodic writes.
// =============================================================================
#pragma once
#include <stdint.h>
#include "../core/boot_policy.h"

namespace ignis {

struct NvsBootRead {
  bool ok = false;
  PersistedFlight pf;
  uint16_t boot_count = 0;   // already incremented and stored for this boot
};

// power_on_reset allows re-initializing a full/incompatible NVS partition
// (never otherwise: that could erase the flight record).
NvsBootRead nvsBootRead(bool power_on_reset);
void persistSeed(const PersistedFlight& pf);   // state this boot resumed from
bool persistClear();                            // power-on reset path only
bool persistPhase(uint8_t phase, float ground_pa);  // ground_pa 0 = keep
bool persistAttempts(uint8_t att1, uint8_t att2);

void rtcMirrorRead(PersistedFlight& out, bool& valid);

}  // namespace ignis
