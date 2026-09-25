/*
  ================================================================
  IGNIS v2 Flight Computer — FlightState.h
  State machine + apogee/landing detection logic.

  Detection approach is deliberately simple barometric + accel
  thresholding rather than a full Kalman/velocity estimator — the
  RRC3 is your redundant COTS altimeter, so the SRAD FC doesn't need
  to be the single point of failure for recovery.
  ================================================================
*/

#pragma once
#include <Arduino.h>
#include "Config.h"
#include "FlightData.h"
#include "Pyro.h"

static float groundPressurePa = 0;
static bool  groundPressureCaptured = false;

static float lastAltSamples[APOGEE_FALLING_SAMPLES];
static int   altSampleCount = 0;
static unsigned long lastAltSampleMs = 0;

static unsigned long boostAboveThresholdSinceMs = 0;
static unsigned long baroClimbSinceMs = 0;
static unsigned long landedStableSinceMs = 0;
static float lastLandedCheckAlt = 0;

// --- failsafe timers (v11) ---
static unsigned long boostEnteredMs = 0;
static unsigned long coastEnteredMs = 0;
static unsigned long drogueFiredMs  = 0;

// --- barometric velocity estimator (v11) ---
static float baroVelBaselineAlt = 0;
static unsigned long baroVelBaselineMs = 0;
static float baroVelocityMps = 0;

// --- tilt integrator (v11, diagnostic only) ---
static unsigned long lastTiltUpdateMs = 0;

// --- minimum-altitude interlock (v12) ---
// Peak AGL seen since liftoff. Gates every pyro decision. See Config.h.
static float maxAglSinceBoost = 0;

// Resolved once, here, so the two modes can never disagree between call sites.
static inline float minApogeeAltM() {
  return GROUND_TEST_MODE ? GROUND_TEST_MIN_ALT_M : MIN_APOGEE_ALT_M;
}
static inline unsigned long boostDebounceMs() {
  return GROUND_TEST_MODE ? GROUND_TEST_DEBOUNCE_MS : BOOST_DEBOUNCE_MS;
}

// Call repeatedly while STATE_PAD_IDLE / STATE_ARMED, before liftoff, to
// establish the AGL pressure reference. Only averages VALID readings.
static void captureGroundPressure(const FlightData& d) {
  static double sum = 0;
  static int n = 0;
  if (!d.baro_reading_valid) return;
  sum += d.pressure_pa;
  n++;
  if (n >= GROUND_PRESSURE_CAL_SAMPLES) {
    groundPressurePa = sum / n;
    groundPressureCaptured = true;
  }
}

static void pushAltitudeSample(float alt) {
  for (int i = APOGEE_FALLING_SAMPLES - 1; i > 0; i--) {
    lastAltSamples[i] = lastAltSamples[i - 1];
  }
  lastAltSamples[0] = alt;
  if (altSampleCount < APOGEE_FALLING_SAMPLES) altSampleCount++;
}

static bool isMonotonicFalling() {
  if (altSampleCount < APOGEE_FALLING_SAMPLES) return false;
  for (int i = 0; i < APOGEE_FALLING_SAMPLES - 1; i++) {
    if (lastAltSamples[i] >= lastAltSamples[i + 1]) return false; // [0]=newest
  }
  return true;
}

// Coarse finite-difference climb rate over a ~200 ms baseline. Deliberately
// not a Kalman filter. Used ONLY as a redundant liftoff trigger - never for
// apogee, where BMP180 noise makes a differentiated velocity useless.
static void updateBaroVelocity(const FlightData& d) {
  if (!d.baro_reading_valid) return;
  if (baroVelBaselineMs == 0) {
    baroVelBaselineAlt = d.agl_m;
    baroVelBaselineMs  = d.timestamp_ms;
    return;
  }
  unsigned long dt = d.timestamp_ms - baroVelBaselineMs;
  if (dt >= BARO_VELOCITY_WINDOW_MS) {
    baroVelocityMps    = (d.agl_m - baroVelBaselineAlt) / (dt / 1000.0f);
    baroVelBaselineAlt = d.agl_m;
    baroVelBaselineMs  = d.timestamp_ms;
  }
}

// Integrated-gyro tilt from launch attitude.
//
// THIS DRIFTS. FLIGHT072 integrated 538 deg of roll in 3.5 s, and any
// residual gyro bias accumulates linearly with time. The value is written to
// the SD log and the telemetry packet so you have an attitude record for
// post-flight analysis and for the Launch Operation Debriefing. It is NEVER
// read by any pyro decision path. Do not change that - see the ACS / 15-degree
// note in Config.h.
static void updateTiltEstimate(FlightData& d) {
  if (!d.imu_reading_valid) return;
  if (lastTiltUpdateMs == 0 || d.state < STATE_BOOST) {
    lastTiltUpdateMs = d.timestamp_ms;
    if (d.state < STATE_BOOST) d.tilt_deg = 0;   // reference held at zero until liftoff
    return;
  }
  float dt = (d.timestamp_ms - lastTiltUpdateMs) / 1000.0f;
  lastTiltUpdateMs = d.timestamp_ms;
  if (dt <= 0 || dt > 0.5f) return;              // reject gaps / rollover
#if BOOST_AXIS_IS_X
  float offAxisRate = sqrtf(d.gyro_y_dps * d.gyro_y_dps + d.gyro_z_dps * d.gyro_z_dps);
#else
  float offAxisRate = sqrtf(d.gyro_x_dps * d.gyro_x_dps + d.gyro_y_dps * d.gyro_y_dps);
#endif
  d.tilt_deg += offAxisRate * dt;
}

static void enterApogee(FlightData& d, uint8_t trigger) {
  d.deploy_trigger = trigger;
  d.state = STATE_APOGEE;
  Serial.printf("[FSM] -> APOGEE (trigger=%s) firing drogue\n",
                trigger == 2 ? "TIMEOUT-FAILSAFE" : "baro");
  fireDrogue(d);
  drogueFiredMs = d.timestamp_ms;
  d.state = STATE_DROGUE_DESCENT;
  Serial.println("[FSM] -> DROGUE_DESCENT");
}

// v13: called once from setup(), only when the resume check in
// IGNIS_FC_heavy_13.ino determined this is a mid-flight reset rather
// than a fresh boot. Restores just enough state to skip re-detecting
// boost/apogee from scratch. Every phase timer is re-armed against the
// NEW millis() clock rather than reconstructed across the reset
// boundary — see the long comment on RtcFlightState in FlightData.h for
// why that's a deliberate, safe simplification and not a gap.
static void resumeFlightState(FlightData& d) {
  unsigned long now = millis();

  maxAglSinceBoost       = rtcState.maxAglSinceBoost;
  groundPressurePa       = rtcState.groundPressurePa;
  groundPressureCaptured = rtcState.groundPressureCaptured;
  d.pyro1_fired = rtcState.pyro1_fired;
  d.pyro2_fired = rtcState.pyro2_fired;

  FlightState_t phase = (FlightState_t)rtcState.phase;

  if (phase == STATE_BOOST) {
    d.state = STATE_BOOST;
    boostEnteredMs = now;

  } else if (phase == STATE_COAST || phase == STATE_APOGEE) {
    // APOGEE is a same-tick transient in normal operation — enterApogee()
    // always advances straight to DROGUE_DESCENT within the same call, so
    // it should never actually be the persisted value. If it somehow is,
    // treat it as COAST: re-arm the falling-sample window and let the
    // existing detector re-confirm apogee cleanly, rather than assuming
    // the fire already happened from a phase value alone.
    d.state = STATE_COAST;
    coastEnteredMs = now;
    altSampleCount = 0;

  } else if (phase == STATE_DROGUE_DESCENT) {
    d.state = STATE_DROGUE_DESCENT;
    // Only affects the main-deploy stagger check below (MAIN_MIN_DELAY_
    // AFTER_DROGUE_MS) — the pyro2_fired latch restored above is what
    // actually prevents a re-fire, independent of this timer.
    drogueFiredMs = now;

  } else if (phase == STATE_MAIN_DESCENT || phase == STATE_LANDED) {
    // Re-run landed detection fresh rather than trusting a persisted
    // LANDED call — cheap (at most one new LANDED_STABLE_MS window) and
    // avoids ever closing the SD log file based on stale data.
    d.state = STATE_MAIN_DESCENT;
    landedStableSinceMs = 0;
    lastLandedCheckAlt  = d.baro_alt_m;

  } else {
    // Unknown/unreachable phase value — fail safe to the pre-v13 behavior.
    d.state = STATE_ARMED;
  }
}

// v13: keeps the RTC-persisted mirror current so any future reset has
// something recent to resume from. Cheap — RTC SRAM, not flash, no
// wear-out concern — so this just runs once per sensor-loop tick rather
// than trying to track every individual field-change call site. Call
// from IGNIS_FC_heavy_13.ino's loop(), after updateFlightState() and
// maintainPyroPulses().
static void rtcSyncFlightState(const FlightData& d) {
  rtcState.magic                  = RTC_FLIGHT_MAGIC;
  rtcState.phase                  = (uint8_t)d.state;
  rtcState.pyro1_fired            = d.pyro1_fired;
  rtcState.pyro2_fired            = d.pyro2_fired;
  rtcState.maxAglSinceBoost       = maxAglSinceBoost;
  rtcState.groundPressurePa       = groundPressurePa;
  rtcState.groundPressureCaptured = groundPressureCaptured;
}

static void updateFlightState(FlightData& d) {
  // Direct pressure-ratio AGL (see altitudeFromPressure() in Sensors.h).
  if (groundPressureCaptured && d.baro_reading_valid) {
    d.agl_m = altitudeFromPressure(d.pressure_pa, groundPressurePa);
  }
  updateBaroVelocity(d);
  updateTiltEstimate(d);

  // Track peak altitude since liftoff for the minimum-altitude interlock.
  if (d.state >= STATE_BOOST && d.agl_m > maxAglSinceBoost) {
    maxAglSinceBoost = d.agl_m;
  }

  switch (d.state) {

    case STATE_PAD_IDLE:
      captureGroundPressure(d);
      break;

    case STATE_ARMED: {
      captureGroundPressure(d); // keep refining until liftoff

      // --- Path A: accelerometer (primary) ---
      bool accelTrigger = false;
      if (d.imu_reading_valid && d.accel_total_g >= BOOST_ACCEL_THRESHOLD_G) {
        if (boostAboveThresholdSinceMs == 0) boostAboveThresholdSinceMs = d.timestamp_ms;
        if (d.timestamp_ms - boostAboveThresholdSinceMs >= boostDebounceMs()) accelTrigger = true;
      } else {
        boostAboveThresholdSinceMs = 0;
      }

      // --- Path B: barometric climb rate (backup, survives a dead IMU) ---
      bool baroTrigger = false;
      if (groundPressureCaptured && baroVelocityMps >= BOOST_BARO_CLIMB_MPS) {
        if (baroClimbSinceMs == 0) baroClimbSinceMs = d.timestamp_ms;
        if (d.timestamp_ms - baroClimbSinceMs >= BOOST_BARO_SUSTAIN_MS) baroTrigger = true;
      } else {
        baroClimbSinceMs = 0;
      }

      if (accelTrigger || baroTrigger) {
        d.state = STATE_BOOST;
        boostEnteredMs = d.timestamp_ms;
        Serial.printf("[FSM] -> BOOST (%s)\n", accelTrigger ? "accel" : "BARO-BACKUP");
      }
      break;
    }

    case STATE_BOOST:
      if (d.accel_total_g <= BURNOUT_ACCEL_THRESHOLD_G ||
          (d.timestamp_ms - boostEnteredMs >= BURNOUT_TIMEOUT_MS)) {
        d.state = STATE_COAST;
        coastEnteredMs = d.timestamp_ms;
        altSampleCount = 0; // reset apogee window at burnout
        Serial.println("[FSM] -> COAST");
      }
      break;

    case STATE_COAST: {
      // MINIMUM-ALTITUDE INTERLOCK (v12). No pyro decision of any kind is
      // permitted until the vehicle has demonstrably gained altitude.
      // FLIGHT018 fired the drogue at agl = -0.80 m off a hand jerk on a
      // 3 m pulley. That must be impossible in flight configuration.
      bool altitudeGatePassed = (maxAglSinceBoost >= minApogeeAltM());

      if (d.timestamp_ms - lastAltSampleMs >= APOGEE_MIN_SAMPLE_INTERVAL_MS) {
        lastAltSampleMs = d.timestamp_ms;
        // Only feed VALID readings into the apogee window - a single corrupt
        // sample that happened to look like a falling value could otherwise
        // cause a premature drogue fire.
        if (d.baro_reading_valid) {
          pushAltitudeSample(d.baro_alt_m);
          if (altitudeGatePassed && isMonotonicFalling()) {
            enterApogee(d, 1);
            break;
          }
        }
      }
      // FAILSAFE: the barometric chain never closed. Deploy anyway rather
      // than ride it in ballistic. Tune APOGEE_TIMEOUT_MS per flight.
      // Still gated on the interlock - a false BOOST on the ground must not
      // become a ground detonation 20 s later.
      if (altitudeGatePassed && APOGEE_TIMEOUT_ENABLED &&
          (d.timestamp_ms - coastEnteredMs >= APOGEE_TIMEOUT_MS)) {
        enterApogee(d, 2);
      }
      break;
    }

    case STATE_APOGEE:
      // Transient safety net - normally enterApogee() already moved us on.
      fireDrogue(d);
      drogueFiredMs = d.timestamp_ms;
      d.state = STATE_DROGUE_DESCENT;
      break;

    case STATE_DROGUE_DESCENT:
#if SINGLE_EVENT_MODE
      // CH2 is deliberately never fired. Firing a channel into an open loop
      // is itself a reset risk on this hardware (unprotected gate opto), so
      // the unused channel is disabled here rather than merely left unwired.
      d.state = STATE_MAIN_DESCENT;
      Serial.println("[FSM] SINGLE_EVENT_MODE -> MAIN_DESCENT (CH2 not fired)");
#else
      // Stagger enforced so a low-apogee flight cannot dump both charges in
      // the same 20 ms tick. FLIGHT072's 74 m apogee was exactly that case.
      // Verified in FLIGHT016/018: 1.524 s and 1.500 s measured.
      if ((d.agl_m <= MAIN_DEPLOY_ALT_AGL_M) &&
          (d.timestamp_ms - drogueFiredMs >= MAIN_MIN_DELAY_AFTER_DROGUE_MS)) {
        Serial.println("[FSM] Main deploy condition met (firing main)");
        fireMain(d);
        d.state = STATE_MAIN_DESCENT;
        Serial.println("[FSM] -> MAIN_DESCENT");
      }
#endif
      break;

    case STATE_MAIN_DESCENT:
      if (fabsf(d.baro_alt_m - lastLandedCheckAlt) < LANDED_ALT_DELTA_M) {
        if (landedStableSinceMs == 0) landedStableSinceMs = d.timestamp_ms;
        if (d.timestamp_ms - landedStableSinceMs >= LANDED_STABLE_MS) {
          d.state = STATE_LANDED;
          Serial.println("[FSM] -> LANDED");
        }
      } else {
        landedStableSinceMs = 0;
        lastLandedCheckAlt = d.baro_alt_m;
      }
      break;

    case STATE_LANDED:
      // Terminal state - keep logging/telemetry alive for recovery.
      break;
  }
}
