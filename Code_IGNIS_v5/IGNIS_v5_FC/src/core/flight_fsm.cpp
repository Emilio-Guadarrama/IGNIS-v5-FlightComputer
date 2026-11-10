// =============================================================================
// IGNIS v5 SINGLE-DEPLOY: flight_fsm.cpp  SAFETY-CRITICAL
// Every threshold/interlock below is a port of the reference
// IGNIS_FC_heavy_13/FlightState.h. Reference line numbers are cited inline.
// Differences from the reference are marked "v5 CHANGE" and listed in
// CHANGES.md for sign-off.
// =============================================================================
#include "flight_fsm.h"
#include <math.h>
#include "pyro_sequencer.h"
#include "../config/config_v5.h"

namespace ignis {

const char* flightStateName(FlightState s) {
  switch (s) {
    case FlightState::SAFE:           return "SAFE";
    case FlightState::ARMED:          return "ARMED";
    case FlightState::BOOST:          return "BOOST";
    case FlightState::COAST:          return "COAST";
    case FlightState::APOGEE:         return "APOGEE";
    case FlightState::DROGUE_DESCENT: return "DESCENT";
    case FlightState::MAIN_DESCENT:   return "MAIN_DESCENT(unused)";
    case FlightState::LANDED:         return "LANDED";
    case FlightState::RECOVERY:       return "RECOVERY";
  }
  return "UNKNOWN";
}

float altitudeFromPressure(float pressure_pa, float reference_pa) {
  // reference Sensors.h:251-253
  return 44330.0f * (1.0f - powf(pressure_pa / reference_pa, 0.19029495f));
}

FsmConfig defaultFsmConfig() {
  FsmConfig c{};
  c.boost_accel_g = BOOST_ACCEL_THRESHOLD_G;
  // Resolved once here so the modes can never disagree (reference FlightState.h:48-54).
  c.boost_debounce_ms = GROUND_TEST_MODE ? GROUND_TEST_DEBOUNCE_MS : BOOST_DEBOUNCE_MS;
  c.min_apogee_alt_m  = GROUND_TEST_MODE ? GROUND_TEST_MIN_ALT_M : MIN_APOGEE_ALT_M;
  c.resume_min_agl_m  = GROUND_TEST_MODE ? GROUND_TEST_MIN_ALT_M : RESUME_MIN_AGL_M;
  c.boost_baro_climb_mps = BOOST_BARO_CLIMB_MPS;
  c.boost_baro_sustain_ms = BOOST_BARO_SUSTAIN_MS;
  c.baro_vel_window_ms = BARO_VELOCITY_WINDOW_MS;
  c.burnout_accel_g = BURNOUT_ACCEL_THRESHOLD_G;
  c.burnout_timeout_ms = BURNOUT_TIMEOUT_MS;
  c.apogee_falling_samples = APOGEE_FALLING_SAMPLES;
  c.apogee_min_sample_interval_ms = APOGEE_MIN_SAMPLE_INTERVAL_MS;
  c.apogee_timeout_ms = APOGEE_TIMEOUT_MS;
  c.apogee_timeout_enabled = APOGEE_TIMEOUT_ENABLED;
  c.landed_alt_delta_m = LANDED_ALT_DELTA_M;
  c.landed_stable_ms = LANDED_STABLE_MS;
  c.ground_cal_samples = GROUND_PRESSURE_CAL_SAMPLES;
  c.safe_return_debounce_ms = SAFE_RETURN_DEBOUNCE_MS;
  return c;
}

PyroSeqConfig defaultPyroSeqConfig() {
  PyroSeqConfig p{};
  p.pulse_ms = PYRO_FIRE_PULSE_MS;
  p.backup_delay_ms = PYRO_BACKUP_DELAY_MS;
  p.settle_ms = PYRO_RETRY_SETTLE_MS;
  p.retry_gap_ms = PYRO_RETRY_GAP_MS;
  p.window_ms = PYRO_SEQUENCE_WINDOW_MS;
  p.max_attempts = PYRO_MAX_ATTEMPTS;
  p.backup_enabled = true;
  return p;
}

void FlightFsm::begin(const FsmConfig& cfg, bool recovery_boot) {
  *this = FlightFsm();
  cfg_ = cfg;
  if (cfg_.apogee_falling_samples < 2) cfg_.apogee_falling_samples = 2;
  if (cfg_.apogee_falling_samples > kMaxFalling) cfg_.apogee_falling_samples = kMaxFalling;
  recovery_boot_ = recovery_boot;
  state_ = recovery_boot ? FlightState::RECOVERY : FlightState::SAFE;
}

// v5 CHANGE: resume after an unwanted reboot. Like the reference
// (FlightState.h:148-193) every phase timer is re-armed on the new clock,
// which can only make a failsafe later, never earlier.
void FlightFsm::beginResume(const FsmConfig& cfg, uint8_t phase, float ground_pa) {
  begin(cfg, false);
  resumed_ = true;
  need_resume_timers_ = true;
  ground_pressure_pa_ = ground_pa;
  ground_captured_ = ground_pa > 0;
  if (!ground_captured_) { recovery_boot_ = true; state_ = FlightState::RECOVERY; return; }
  if (phase == PHASE_BOOST) {
    state_ = FlightState::BOOST;
  } else if (phase == PHASE_COAST) {
    state_ = FlightState::COAST;
  } else if (phase == PHASE_DESCENT) {
    state_ = FlightState::DROGUE_DESCENT;
    resumed_descent_ = true;
    deploy_commanded_ = true;   // never a second apogee command; the sequencer resumes
  } else {
    recovery_boot_ = true;      // LANDED / unknown: no firing of any kind
    state_ = FlightState::RECOVERY;
  }
}

void FlightFsm::setState(FlightState s, FsmOutput& out) {
  if (s != state_) {
    state_ = s;
    out.state_changed = true;
  }
}

// reference FlightState.h:58-68: cumulative mean of VALID samples only.
void FlightFsm::captureGround(const FsmInput& in) {
  if (!in.baro_valid) return;
  ground_sum_ += in.pressure_pa;
  if (ground_n_ < 0xFFFF) ground_n_++;
  else { ground_sum_ = (double)ground_pressure_pa_ * ground_n_; }  // saturate: keep the mean stable
  if (ground_n_ >= cfg_.ground_cal_samples) {
    ground_pressure_pa_ = (float)(ground_sum_ / ground_n_);
    ground_captured_ = true;
  }
}

// reference FlightState.h:89-102: coarse finite difference, liftoff backup only.
void FlightFsm::updateBaroVelocity(const FsmInput& in) {
  if (!in.baro_valid) return;
  if (!baro_vel_have_base_) {
    baro_vel_base_alt_ = agl_m_;
    baro_vel_base_ms_ = in.now_ms;
    baro_vel_have_base_ = true;
    return;
  }
  uint32_t dt = in.now_ms - baro_vel_base_ms_;
  if (dt >= cfg_.baro_vel_window_ms) {
    baro_vel_mps_ = (agl_m_ - baro_vel_base_alt_) / (dt / 1000.0f);
    baro_vel_base_alt_ = agl_m_;
    baro_vel_base_ms_ = in.now_ms;
  }
}

// reference FlightState.h:70-84 ([0] = newest)
void FlightFsm::pushAlt(float alt) {
  for (int i = cfg_.apogee_falling_samples - 1; i > 0; i--) alt_samples_[i] = alt_samples_[i - 1];
  alt_samples_[0] = alt;
  if (alt_count_ < cfg_.apogee_falling_samples) alt_count_++;
}

bool FlightFsm::isMonotonicFalling() const {
  if (alt_count_ < cfg_.apogee_falling_samples) return false;
  for (int i = 0; i < cfg_.apogee_falling_samples - 1; i++) {
    if (alt_samples_[i] >= alt_samples_[i + 1]) return false;
  }
  return true;
}

// reference FlightState.h:130-139. Commands deployment and advances to
// DESCENT in the same tick.
void FlightFsm::enterApogee(uint32_t, uint8_t trigger, FsmOutput& out) {
  if (recovery_boot_ || state_ != FlightState::COAST) return;   // defensive re-check
  if (max_agl_ < cfg_.min_apogee_alt_m) return;
  deploy_trigger_ = trigger;
  if (!deploy_commanded_) {
    deploy_commanded_ = true;
    out.deploy_command = true;
  }
  out.persist_phase = PHASE_DESCENT;
  setState(FlightState::DROGUE_DESCENT, out);
}

FsmOutput FlightFsm::step(const FsmInput& in) {
  FsmOutput out;

  // RECOVERY: terminal, no outputs of any kind.
  if (recovery_boot_ || state_ == FlightState::RECOVERY) {
    state_ = FlightState::RECOVERY;
    if (in.baro_valid) last_baro_alt_m_ = in.baro_alt_m;
    return out;
  }
  if (need_resume_timers_) {
    need_resume_timers_ = false;
    boost_entered_ms_ = in.now_ms;
    coast_entered_ms_ = in.now_ms;
    alt_count_ = 0;
  }

  // reference FlightState.h:212-222
  if (ground_captured_ && in.baro_valid) {
    agl_m_ = altitudeFromPressure(in.pressure_pa, ground_pressure_pa_);
  }
  if (in.baro_valid) last_baro_alt_m_ = in.baro_alt_m;
  updateBaroVelocity(in);
  if (!isPadState(state_) && agl_m_ > max_agl_) max_agl_ = agl_m_;

  switch (state_) {
    case FlightState::SAFE:
    case FlightState::ARMED: {
      captureGround(in);  // keep refining until liftoff (reference :226-231)

      // v5 CHANGE: SAFE/ARMED mirror the hardware arming signal (continuity
      // via the pyro optocouplers). Fail closed: invalid reading == armed.
      bool armedNow = continuityIndicatesArmed(in.cont);
      if (state_ == FlightState::SAFE && armedNow) {
        setState(FlightState::ARMED, out);
        no_cont_ = false;
      } else if (state_ == FlightState::ARMED) {
        if (armedNow) {
          no_cont_ = false;
        } else {
          if (!no_cont_) { no_cont_ = true; no_cont_since_ms_ = in.now_ms; }
          if (in.now_ms - no_cont_since_ms_ >= cfg_.safe_return_debounce_ms) {
            setState(FlightState::SAFE, out);
            no_cont_ = false;
          }
        }
      }

      // Liftoff detection runs in BOTH pad states, so a bad continuity read
      // can never cancel a flight (reference philosophy, Pyro.h:11-16).
      bool accelTrigger = false;  // Path A (reference :233-240)
      if (in.imu_valid && in.accel_total_g >= cfg_.boost_accel_g) {
        if (!boost_above_) { boost_above_ = true; boost_above_since_ms_ = in.now_ms; }
        if (in.now_ms - boost_above_since_ms_ >= cfg_.boost_debounce_ms) accelTrigger = true;
      } else {
        boost_above_ = false;
      }
      bool baroTrigger = false;   // Path B (reference :242-249)
      if (ground_captured_ && baro_vel_mps_ >= cfg_.boost_baro_climb_mps) {
        if (!baro_climb_) { baro_climb_ = true; baro_climb_since_ms_ = in.now_ms; }
        if (in.now_ms - baro_climb_since_ms_ >= cfg_.boost_baro_sustain_ms) baroTrigger = true;
      } else {
        baro_climb_ = false;
      }
      if ((accelTrigger || baroTrigger) && ground_captured_) {
        setState(FlightState::BOOST, out);
        out.persist_phase = PHASE_BOOST;
        boost_entered_ms_ = in.now_ms;
        if (agl_m_ > max_agl_) max_agl_ = agl_m_;
      } else if (accelTrigger || baroTrigger) {
        // v5 CHANGE: no ground reference yet (< GROUND_PRESSURE_CAL_SAMPLES valid
        // baro samples since boot). Enter BOOST anyway for logging; AGL and the
        // altitude gate then use the first in-flight sample (conservative: the
        // gate can only become harder to pass).
        if (in.baro_valid) { ground_pressure_pa_ = in.pressure_pa; ground_captured_ = true; }
        setState(FlightState::BOOST, out);
        out.persist_phase = PHASE_BOOST;
        boost_entered_ms_ = in.now_ms;
      }
      break;
    }

    case FlightState::BOOST:  // reference :259-267
      if (in.accel_total_g <= cfg_.burnout_accel_g ||
          (in.now_ms - boost_entered_ms_ >= cfg_.burnout_timeout_ms)) {
        setState(FlightState::COAST, out);
        out.persist_phase = PHASE_COAST;
        coast_entered_ms_ = in.now_ms;
        alt_count_ = 0;  // reset apogee window at burnout
      }
      break;

    case FlightState::COAST: {  // reference :269-298
      bool altitudeGatePassed = (max_agl_ >= cfg_.min_apogee_alt_m);
      if (!have_alt_sample_time_ ||
          in.now_ms - last_alt_sample_ms_ >= cfg_.apogee_min_sample_interval_ms) {
        have_alt_sample_time_ = true;
        last_alt_sample_ms_ = in.now_ms;
        if (in.baro_valid) {  // only VALID readings enter the window
          pushAlt(in.baro_alt_m);
          if (altitudeGatePassed && isMonotonicFalling()) {
            enterApogee(in.now_ms, DEPLOY_BARO_APOGEE, out);
            break;
          }
        }
      }
      // Failsafe, still behind the altitude interlock (reference :289-296)
      if (altitudeGatePassed && cfg_.apogee_timeout_enabled &&
          (in.now_ms - coast_entered_ms_ >= cfg_.apogee_timeout_ms)) {
        enterApogee(in.now_ms, DEPLOY_TIMEOUT_FAILSAFE, out);
      }
      break;
    }

    case FlightState::APOGEE:
      setState(FlightState::COAST, out);  // never persists; treat as COAST
      break;

    case FlightState::MAIN_DESCENT:       // not used in SINGLE-DEPLOY
      setState(FlightState::DROGUE_DESCENT, out);
      break;

    case FlightState::DROGUE_DESCENT:     // descent under the parachute
      // Landed detection (reference :328-339, moved here: single deployment)
      if (fabsf(last_baro_alt_m_ - last_landed_alt_) < cfg_.landed_alt_delta_m) {
        if (!landed_timing_) { landed_timing_ = true; landed_since_ms_ = in.now_ms; }
        if (in.now_ms - landed_since_ms_ >= cfg_.landed_stable_ms) {
          setState(FlightState::LANDED, out);
          out.persist_phase = PHASE_LANDED;
        }
      } else {
        landed_timing_ = false;
        last_landed_alt_ = last_baro_alt_m_;
      }
      break;

    case FlightState::LANDED:   // terminal: keep logging/telemetry alive
    case FlightState::RECOVERY:
      break;
  }

  // Pyro activity is permitted only in DESCENT, only after a deployment
  // command, and after a reboot only while provably airborne.
  if (state_ == FlightState::DROGUE_DESCENT && deploy_commanded_) {
    if (resumed_descent_) {
      out.fire_permitted = in.baro_valid && ground_captured_ && agl_m_ >= cfg_.resume_min_agl_m;
    } else {
      out.fire_permitted = max_agl_ >= cfg_.min_apogee_alt_m;
    }
  }
  return out;
}

}  // namespace ignis
