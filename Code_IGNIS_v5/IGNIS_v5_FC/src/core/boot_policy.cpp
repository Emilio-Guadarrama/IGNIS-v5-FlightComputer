#include "boot_policy.h"
#include "../config/config_v5.h"

namespace ignis {

BootDecision decideBoot(uint8_t reset_reason, const PersistFlags& nvs, bool nvs_ok,
                        const PersistFlags& rtc, bool rtc_valid) {
  BootDecision d;
  if (reset_reason == RESET_REASON_POWERON_VALUE) {
    // True power-on (RBF power cycle): the only path that clears the latch.
    d.mode = BootMode::NORMAL;
    d.clear_flags = (nvs_ok && nvs.any()) || (rtc_valid && rtc.any()) || !nvs_ok;
    d.why = "power-on reset: flags cleared";
    return d;
  }
  if (nvs_ok && nvs.post_fire) { d.mode = BootMode::RECOVERY; d.why = "post_fire flag (NVS)"; return d; }
  if (nvs_ok && nvs.flight_in_progress) { d.mode = BootMode::RECOVERY; d.why = "flight_in_progress flag (NVS)"; return d; }
  if (rtc_valid && rtc.any()) { d.mode = BootMode::RECOVERY; d.why = "flag in RTC mirror"; return d; }
  if (!nvs_ok) { d.mode = BootMode::RECOVERY; d.why = "NVS unreadable on non-power-on reset (fail closed)"; return d; }
  d.mode = BootMode::NORMAL;
  d.why = "non-power-on reset, no flight flags";
  return d;
}

// Values verified against Arduino-ESP32 3.3.12 / ESP-IDF 5.5 esp_system.h.
const char* resetReasonName(uint8_t r) {
  switch (r) {
    case 0:  return "UNKNOWN";
    case 1:  return "POWERON";
    case 2:  return "EXT";
    case 3:  return "SW";
    case 4:  return "PANIC";
    case 5:  return "INT_WDT";
    case 6:  return "TASK_WDT";
    case 7:  return "WDT";
    case 8:  return "DEEPSLEEP";
    case 9:  return "BROWNOUT";
    case 10: return "SDIO";
    case 11: return "USB";
    case 12: return "JTAG";
    case 13: return "EFUSE";
    case 14: return "PWR_GLITCH";
    case 15: return "CPU_LOCKUP";
    default: return "OTHER";
  }
}

}  // namespace ignis
