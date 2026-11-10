// =============================================================================
// IGNIS v5: boot_policy.h  (pure C++, host-testable)  SAFETY-CRITICAL
// Requirement A: decide NORMAL vs RECOVERY at boot from the reset reason and
// the persisted flags. Pure function, so every combination is unit-tested.
// =============================================================================
#pragma once
#include <stdint.h>

namespace ignis {

struct PersistFlags {
  bool flight_in_progress = false;  // written at BOOST entry
  bool post_fire = false;           // written right after a fire command
  bool any() const { return flight_in_progress || post_fire; }
};

enum class BootMode : uint8_t { NORMAL = 0, RECOVERY = 1 };

struct BootDecision {
  BootMode mode = BootMode::NORMAL;
  bool clear_flags = false;   // erase NVS + RTC flags (only on a true power-on reset)
  const char* why = "";
};

// reset_reason: esp_reset_reason_t value (1 == ESP_RST_POWERON).
// nvs_ok: false if NVS could not be opened/read. Fail closed: on a non-power-on
//         reset with unreadable NVS the FC boots into RECOVERY.
// rtc: RTC_NOINIT mirror of the flags (valid only if rtc_valid).
BootDecision decideBoot(uint8_t reset_reason, const PersistFlags& nvs, bool nvs_ok,
                        const PersistFlags& rtc, bool rtc_valid);

const char* resetReasonName(uint8_t r);

}  // namespace ignis
