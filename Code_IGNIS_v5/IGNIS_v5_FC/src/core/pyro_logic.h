// =============================================================================
// IGNIS v5: pyro_logic.h  (pure C++, host-testable)  SAFETY-CRITICAL
// Decision layer under pyro_hw: one-shot latch, recovery lockout, and
// non-blocking pulse timing. pyro_hw only mirrors gateOn() onto the GPIO.
//
// Ported from reference Pyro.h:116-160:
//   - fired flag latches THE INSTANT the gate goes high (FLIGHT035 lesson)
//   - pulse end handled by millis() bookkeeping, never delay()
//   - one-shot: a channel can be commanded at most once per boot
// v5 additions:
//   - lockout(): one-way latch set on a RECOVERY boot (requirement A). After
//     it, request() always returns false. There is no unlock.
//   - persisted "already fired" bits block a re-fire even if the FSM asked.
// =============================================================================
#pragma once
#include <stdint.h>

namespace ignis {

enum PyroChannel : uint8_t { PYRO_CH1_DROGUE = 0, PYRO_CH2_MAIN = 1 };

class PyroLogic {
 public:
  explicit PyroLogic(uint32_t pulse_ms) : pulse_ms_(pulse_ms) {}

  void lockout() { locked_out_ = true; }
  bool lockedOut() const { return locked_out_; }

  // Mark a channel as already spent (e.g. restored from persisted state).
  void markSpent(PyroChannel ch) { fired_[ch] = true; }

  // Returns true if the gate must be driven now (first and only time).
  bool request(PyroChannel ch, uint32_t now_ms) {
    if (locked_out_) return false;
    if (fired_[ch]) return false;
    fired_[ch] = true;           // latch before anything else
    active_[ch] = true;
    start_ms_[ch] = now_ms;
    last_event_ms_ = now_ms;
    any_event_ = true;
    return true;
  }

  // Call every tick. Returns a bitmask of channels whose pulse just ended.
  uint8_t service(uint32_t now_ms) {
    uint8_t ended = 0;
    for (int i = 0; i < 2; i++) {
      if (active_[i] && (now_ms - start_ms_[i] >= pulse_ms_)) {
        active_[i] = false;
        ended |= (uint8_t)(1u << i);
      }
    }
    return ended;
  }

  bool gateOn(PyroChannel ch) const { return active_[ch] && !locked_out_; }
  bool fired(PyroChannel ch) const { return fired_[ch]; }
  bool anyEvent() const { return any_event_; }
  uint32_t lastEventMs() const { return last_event_ms_; }

  // Post-pyro forced-flush window (reference DataLogger.h:222-225).
  bool inFlushWindow(uint32_t now_ms, uint32_t window_ms) const {
    return any_event_ && (now_ms - last_event_ms_ < window_ms);
  }

 private:
  uint32_t pulse_ms_;
  bool locked_out_ = false;
  bool fired_[2] = {false, false};
  bool active_[2] = {false, false};
  uint32_t start_ms_[2] = {0, 0};
  uint32_t last_event_ms_ = 0;
  bool any_event_ = false;
};

}  // namespace ignis
