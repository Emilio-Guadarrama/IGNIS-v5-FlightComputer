// =============================================================================
// IGNIS v5 SINGLE-DEPLOY: pyro_sequencer.h  (pure C++, host-tested)
// SAFETY-CRITICAL. PENDING SIGN-OFF.
//
// Replaces the one-shot PyroLogic. One deployment event, two charges:
//   CH1 (primary) fires when the FSM commands deployment (apogee).
//   CH2 (backup charge, same parachute) fires as soon as CH1's pulse ends
//       (PYRO_BACKUP_DELAY_MS >= pulse), ALWAYS, not only if CH1 failed.
//   Retry: if a channel still reads continuity after its pulse + settle time,
//       the e-match did not burn open (ignition not detected) -> re-fire it,
//       up to PYRO_MAX_ATTEMPTS per channel, PYRO_RETRY_GAP_MS apart.
// Invariants (each covered by host tests):
//   - Never two gates on at the same time (no simultaneous pyro current).
//   - Nothing fires before commandDeploy() (apogee) or resumeAfterReboot().
//   - Nothing new starts unless the caller passes fire_permitted (FSM: post-
//     apogee; after a reboot also "still airborne").
//   - Retries need a VALID continuity reading that still shows the e-match
//     intact; an invalid/unknown reading never triggers a fire.
//   - A retry needs the channel to have shown continuity before it was fired
//     (an open-on-the-pad channel gives no information).
//   - No new attempt after PYRO_SEQUENCE_WINDOW_MS from the deploy command /
//     the reboot (bounds pyro activity near the ground).
//   - lockout() (RECOVERY) is one-way; gates drop immediately.
//   - The attempt count is persisted by the caller after every start, so the
//     cap holds across reboots.
// The reference behavior this replaces: one-shot latch (Pyro.h:116-146),
// SINGLE_EVENT_MODE "CH2 never fired" (Config.h:227-237), 1000 ms pulse
// (Config.h:240), 1500 ms drogue->main stagger (Config.h:200-204).
// =============================================================================
#pragma once
#include <stdint.h>
#include "flight_types.h"

namespace ignis {

enum PyroChannel : uint8_t { PYRO_CH1_PRIMARY = 0, PYRO_CH2_BACKUP = 1 };

struct PyroSeqConfig {
  uint32_t pulse_ms;
  uint32_t backup_delay_ms;   // from CH1 first start to CH2 first start; must be >= pulse_ms
  uint32_t settle_ms;         // after a pulse ends, before reading continuity
  uint32_t retry_gap_ms;      // from a pulse end to the next attempt on that channel
  uint32_t window_ms;         // no new attempt after this long from command/reboot
  uint8_t  max_attempts;      // per channel, including the first
  bool     backup_enabled;
};

class PyroSequencer {
 public:
  void begin(const PyroSeqConfig& c) { *this = PyroSequencer(); cfg_ = c; }

  void lockout() { locked_ = true; on_[0] = on_[1] = false; }
  bool lockedOut() const { return locked_; }

  // Attempts already made before this boot (from NVS/RTC).
  void restore(uint8_t att1, uint8_t att2) { att_[0] = att1; att_[1] = att2; }

  // Normal flight: deployment commanded at apogee (once).
  void commandDeploy(uint32_t now, const Continuity& c) {
    if (locked_ || active_) return;
    active_ = true;
    start_ms_ = now;
    pre_ok_[0] = c.valid && c.ch1;
    pre_ok_[1] = c.valid && c.ch2;
    want_first_[0] = (att_[0] == 0);
    ch1_first_ms_ = now;
    ch1_started_ = (att_[0] > 0);
  }

  // Resumed descent after an unwanted reboot: re-evaluate both channels now.
  void resumeAfterReboot(uint32_t now) {
    if (locked_ || active_) return;
    active_ = true;
    start_ms_ = now;
    ch1_first_ms_ = now - cfg_.backup_delay_ms;  // backup (if never fired) is due immediately
    ch1_started_ = (att_[0] > 0);
    want_first_[0] = (att_[0] == 0);
    for (int ch = 0; ch < 2; ch++) {
      if (att_[ch] > 0 && att_[ch] < cfg_.max_attempts) {
        // Continuity present NOW means the e-match is intact: evaluate at once.
        pre_ok_[ch] = true;
        check_[ch] = true;
        end_ms_[ch] = now - cfg_.settle_ms;
      }
    }
  }

  // Every tick. Returns a bitmask of channels whose gate turned ON this tick.
  uint8_t service(uint32_t now, const Continuity& c, bool fire_permitted) {
    if (locked_) { on_[0] = on_[1] = false; return 0; }
    // 1) end pulses
    for (int ch = 0; ch < 2; ch++) {
      if (on_[ch] && now - on_ms_[ch] >= cfg_.pulse_ms) {
        on_[ch] = false;
        end_ms_[ch] = now;
        check_[ch] = true;
      }
    }
    if (!active_) return 0;
    // 2) post-pulse continuity check -> schedule retry
    for (int ch = 0; ch < 2; ch++) {
      if (!check_[ch] || now - end_ms_[ch] < cfg_.settle_ms) continue;
      check_[ch] = false;
      bool intact = c.valid && (ch == 0 ? c.ch1 : c.ch2);
      retry_[ch] = intact && pre_ok_[ch] && att_[ch] < cfg_.max_attempts;
      retry_at_[ch] = end_ms_[ch] + cfg_.retry_gap_ms;
    }
    // 3) start at most one attempt, only with every gate off
    if (on_[0] || on_[1] || !fire_permitted) return 0;
    if (now - start_ms_ >= cfg_.window_ms) return 0;
    int pick = -1;
    if (want_first_[0]) {
      pick = 0;
    } else if (cfg_.backup_enabled && ch1_started_ && att_[1] == 0 &&
               now - ch1_first_ms_ >= cfg_.backup_delay_ms) {
      pick = 1;
    } else {
      bool r0 = retry_[0] && (int32_t)(now - retry_at_[0]) >= 0;
      bool r1 = retry_[1] && (int32_t)(now - retry_at_[1]) >= 0;
      if (r0 && r1) pick = ((int32_t)(retry_at_[1] - retry_at_[0]) < 0) ? 1 : 0;
      else if (r0) pick = 0;
      else if (r1) pick = 1;
    }
    if (pick < 0) return 0;
    if (att_[pick] >= cfg_.max_attempts) { want_first_[pick] = false; retry_[pick] = false; return 0; }
    att_[pick]++;
    on_[pick] = true;
    on_ms_[pick] = now;
    retry_[pick] = false;
    if (pick == 0 && want_first_[0]) { want_first_[0] = false; ch1_started_ = true; ch1_first_ms_ = now; }
    last_event_ms_ = now;
    any_event_ = true;
    return (uint8_t)(1u << pick);
  }

  bool gateOn(PyroChannel ch) const { return on_[ch] && !locked_; }
  uint8_t attempts(PyroChannel ch) const { return att_[ch]; }
  bool active() const { return active_; }
  bool inFlushWindow(uint32_t now, uint32_t window_ms) const {
    return any_event_ && (now - last_event_ms_ < window_ms);
  }

 private:
  PyroSeqConfig cfg_{};
  bool locked_ = false, active_ = false, any_event_ = false;
  bool ch1_started_ = false;
  bool on_[2] = {false, false};
  bool check_[2] = {false, false};
  bool retry_[2] = {false, false};
  bool pre_ok_[2] = {false, false};
  bool want_first_[2] = {false, false};
  uint8_t att_[2] = {0, 0};
  uint32_t on_ms_[2] = {0, 0}, end_ms_[2] = {0, 0}, retry_at_[2] = {0, 0};
  uint32_t start_ms_ = 0, ch1_first_ms_ = 0, last_event_ms_ = 0;
};

PyroSeqConfig defaultPyroSeqConfig();

}  // namespace ignis
