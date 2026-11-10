#include "boot_policy.h"
#include "flight_fsm.h"
#include "../config/config_v5.h"

namespace ignis {

const char* bootModeName(BootMode m) {
  switch (m) {
    case BootMode::NORMAL: return "NORMAL";
    case BootMode::RESUME_ASCENT: return "RESUME_ASCENT";
    case BootMode::RESUME_DESCENT: return "RESUME_DESCENT";
    case BootMode::RECOVERY: return "RECOVERY";
  }
  return "?";
}

static uint8_t mx(uint8_t a, uint8_t b) { return a > b ? a : b; }

PersistedFlight mergePersisted(const PersistedFlight& nvs, bool nvs_ok,
                               const PersistedFlight& rtc, bool rtc_valid) {
  PersistedFlight m;
  if (nvs_ok) m = nvs;
  if (rtc_valid) {
    m.phase = mx(m.phase, rtc.phase);
    m.attempts[0] = mx(m.attempts[0], rtc.attempts[0]);
    m.attempts[1] = mx(m.attempts[1], rtc.attempts[1]);
    if (!(m.ground_pa > 0) && rtc.ground_pa > 0) m.ground_pa = rtc.ground_pa;
  }
  // Any attempt implies at least the descent phase.
  if ((m.attempts[0] || m.attempts[1]) && m.phase < PHASE_DESCENT) m.phase = PHASE_DESCENT;
  return m;
}

BootDecision decideBoot(uint8_t reset_reason, const PersistedFlight& nvs, bool nvs_ok,
                        const PersistedFlight& rtc, bool rtc_valid) {
  BootDecision d;
  if (reset_reason == RESET_REASON_POWERON_VALUE) {
    // True power-on (RBF power cycle): the only path that clears the state.
    d.mode = BootMode::NORMAL;
    d.clear_state = !nvs_ok || nvs.any() || (rtc_valid && rtc.any());
    d.why = "power-on reset: persisted flight state cleared";
    return d;
  }
  if (!nvs_ok && !rtc_valid) {
    d.mode = BootMode::RECOVERY;
    d.why = "NVS and RTC unreadable on a non-power-on reset (fail closed)";
    return d;
  }
  d.merged = mergePersisted(nvs, nvs_ok, rtc, rtc_valid);
  const PersistedFlight& m = d.merged;
  if (!m.any()) { d.mode = BootMode::NORMAL; d.why = "non-power-on reset, no flight recorded"; return d; }
  bool ground_ok = m.ground_pa > 30000.0f && m.ground_pa < 110000.0f;
  switch (m.phase) {
    case PHASE_BOOST:
    case PHASE_COAST:
      d.mode = ground_ok ? BootMode::RESUME_ASCENT : BootMode::RECOVERY;
      d.why = ground_ok ? "reset during ascent: resume, deploy at apogee"
                        : "reset during ascent without ground reference";
      return d;
    case PHASE_DESCENT:
      d.mode = ground_ok ? BootMode::RESUME_DESCENT : BootMode::RECOVERY;
      d.why = ground_ok ? "reset after deployment: retry/backup only if airborne and e-match intact"
                        : "reset after deployment without ground reference";
      return d;
    case PHASE_LANDED:
      d.mode = BootMode::RECOVERY;
      d.why = "reset after landing";
      return d;
    default:
      d.mode = BootMode::RECOVERY;
      d.why = "unknown persisted phase";
      return d;
  }
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
