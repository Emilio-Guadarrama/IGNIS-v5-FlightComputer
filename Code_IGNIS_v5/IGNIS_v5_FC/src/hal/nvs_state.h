// =============================================================================
// IGNIS v5: nvs_state.h  SAFETY-CRITICAL (requirement A)
// Persistent flags in NVS + an RTC_NOINIT mirror.
// Flash wear: keys are written ONLY on transitions (boot counter once per boot,
// flight flag once at BOOST, post-fire flag once at the first fire, clear once
// on a power-on reset that finds a flag set). No periodic writes.
// =============================================================================
#pragma once
#include <stdint.h>
#include "../core/boot_policy.h"

namespace ignis {

struct NvsBootRead {
  bool ok = false;
  PersistFlags flags;
  uint8_t fired_mask = 0;    // which channels were commanded before this reset (bit0 CH1, bit1 CH2)
  uint16_t boot_count = 0;   // already incremented and stored for this boot
};

// Open NVS, read flags, increment boot counter. power_on_reset allows the NVS
// partition to be re-initialized if it is full/incompatible (never otherwise).
NvsBootRead nvsBootRead(bool power_on_reset);
bool nvsClearFlags();                    // power-on reset path only
bool nvsSetFlightInProgress();           // BOOST entry (idempotent; writes once)
bool nvsSetPostFire(uint8_t channel_mask);  // on a fire command; writes only when the mask changes

// RTC_NOINIT mirror: survives software/watchdog/panic resets, not power-on.
void rtcMirrorRead(PersistFlags& out, bool& valid);
void rtcMirrorSet(const PersistFlags& f);
void rtcMirrorClear();

}  // namespace ignis
