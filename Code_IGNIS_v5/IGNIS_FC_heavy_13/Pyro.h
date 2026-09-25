/*
  ================================================================
  IGNIS v2 Flight Computer — Pyro.h
  Dual-channel pyro control: drogue (apogee) + main (450 m AGL) via
  Tender Descender. Missile Works RRC3 is your independent COTS
  backup altimeter — this module is the SRAD primary only.

  Safety interlocks enforced here:
    - A channel will only fire if the FC is in STATE_ARMED or later
      (never from STATE_PAD_IDLE).
    - A channel will only fire once (latched via *_fired flag).
    - Continuity is checked and logged, but firing is NOT blocked
      on missing continuity — a dead squib reading right before the
      fire command doesn't mean don't-try, it means the backup
      altimeter is now your primary. Continuity is for pre-flight
      go/no-go and telemetry, not a flight-time firing gate.

  CONFIRMED against schematic (Igniter #1/Main sheet):
    - Continuity sense IS digital, not ADC. EMATCH_CH1_B feeds an opto
      (U7/PC817) whose output pulls PYRO_CH1_CONT HIGH through VCC_3V3
      when the loop is intact, R19 pulls it LOW when open. digitalRead()
      below is correct as-is.
    - HARDWARE FLAG (not fixed by firmware): PYRO_CHx_GATE appears to
      drive the fire-side opto (U8) LED with NO series resistor in that
      leg — every other leg of the igniter circuit has one (R16-R20),
      this one doesn't. An ESP32-S3 GPIO driving straight into a PC817
      LED with no limiting resistor can pull ~80mA through a ~25ohm
      GPIO driver, well above both the opto's rated LED current and the
      pin's absolute max. This is a strong candidate for "works on one
      FC, not on others" — it's tolerance/wear-dependent, not a clean
      pass/fail. Add a 220-470ohm series resistor on the gate opto leg
      on the next PCB rev. Firmware cannot compensate for this.

  NON-BLOCKING FIRE PULSE (changed after FLIGHT035): firing used to be
  digitalWrite(HIGH) -> delay(1000ms) -> digitalWrite(LOW) -> set fired
  flag, all blocking. On FLIGHT035, physical evidence (recovered
  e-matches, both confirmed fired) showed drogue AND main both fired,
  but the SD log shows neither — because the flag that would have been
  logged wasn't set until AFTER the 1000ms blocking delay returned, and
  power was lost (avionics bay damage from the Eagle CO2 device's
  inadequately-mounted bracket) before either delay() call completed.
  The forensic record of a real ignition event was lost specifically
  because logging was gated behind a blocking delay.

  Now: the fired flag latches THE INSTANT the pin goes HIGH, on the same
  loop tick as the fire decision — so it reaches the very next SD/
  telemetry write regardless of what happens during the pulse itself.
  Turning the pin back LOW is handled separately via millis() tracking
  in maintainPyroPulses(), called once per loop() — sensors, GPS, and
  telemetry keep running through the pulse instead of stalling.

  Behavioral note: because firing is no longer blocking, if AGL is
  already below MAIN_DEPLOY_ALT_AGL_M at the moment drogue fires (true
  on every bench/pulley test, and on any real flight whose apogee is
  below that altitude), main will now fire within one loop tick (~20ms)
  of drogue instead of ~1-2s later. On a real flight with apogee above
  450m AGL this doesn't apply — main still won't fire until genuine
  descent reaches that altitude, tens of seconds after drogue.
  ================================================================
*/

#pragma once
#include <Arduino.h>
#include "driver/gpio.h"
#include "Config.h"
#include "FlightData.h"

static unsigned long pyro1_fireStartMs = 0;
static unsigned long pyro2_fireStartMs = 0;
static bool pyro1_pulseActive = false;
static bool pyro2_pulseActive = false;

// Set false to restore the ESP32-S3 default drive strength (GPIO_DRIVE_CAP_2).
// See the long comment inside initPyro() before changing this.
#define PYRO_GATE_REDUCED_DRIVE  true

static void initPyro() {
  pinMode(PYRO1_FIRE_PIN, OUTPUT);
  pinMode(PYRO2_FIRE_PIN, OUTPUT);
  digitalWrite(PYRO1_FIRE_PIN, LOW);
  digitalWrite(PYRO2_FIRE_PIN, LOW);

#if PYRO_GATE_REDUCED_DRIVE
  // PARTIAL mitigation for the hardware flag documented in this file's
  // header: PYRO_CHx_GATE drives the fire-side PC817 LED with NO series
  // resistor. Lowering the ESP32-S3 drive capability raises the output
  // driver's impedance and limits current into that unprotected LED.
  //
  // FLIGHT072 evidence that this matters: CH1 had NO continuity for the
  // entire flight (pyro1_cont = 0 across all 10,226 rows), so firing it drew
  // no pyro current at all - yet the FC still reset within ~1.2 s of the gate
  // going HIGH, in mid-air, roughly 65 m above ground and 2.5 s before impact.
  // With the pyro loop open, the gate drive is the only load that changed.
  //
  // *** THIS IS NOT THE FIX. ***
  // The fix is a 330 ohm series resistor on each gate leg. Under-driving an
  // opto can also make it FAIL TO SWITCH, which is the more dangerous
  // failure direction. If you enable this you MUST verify on the bench that a
  // real e-match fires reliably, 5 times out of 5, on the flight battery.
  // If that test does not pass cleanly, set PYRO_GATE_REDUCED_DRIVE to false
  // and do the resistor bodge instead.
  gpio_set_drive_capability((gpio_num_t)PYRO1_FIRE_PIN, GPIO_DRIVE_CAP_1);  // ~10 mA
  gpio_set_drive_capability((gpio_num_t)PYRO2_FIRE_PIN, GPIO_DRIVE_CAP_1);
#endif

  pinMode(PYRO1_CONT_PIN, INPUT);
  pinMode(PYRO2_CONT_PIN, INPUT);
}

// Call before arming — surfaces a pre-flight go/no-go, does not gate firing.
static void checkPyroContinuity(FlightData& d) {
  d.pyro1_continuity = digitalRead(PYRO1_CONT_PIN);
  d.pyro2_continuity = digitalRead(PYRO2_CONT_PIN);
}

static void fireChannel(uint8_t pin, bool& firedFlag, bool& pulseActiveFlag,
                         unsigned long& fireStartMs, const char* label) {
  if (firedFlag) return; // one-shot latch
  rtcSetLastOp(OP_PYRO_FIRE);   // v13 — see FlightData.h's RtcFlightState
  Serial.printf("[PYRO] Firing %s\n", label);
  digitalWrite(pin, HIGH);
  fireStartMs = millis();
  pulseActiveFlag = true;
  // Latched IMMEDIATELY, not after the pulse — see header comment. This
  // is the actual fix: firedFlag being true here means the very next
  // logFlightData()/sendTelemetry() call (same loop tick) will record
  // that firing happened, before the pulse has even finished, let alone
  // before anything downstream of the pulse (like a structural failure)
  // has a chance to cut power first.
  //
  // v13: firedFlag itself (d.pyro1_fired / d.pyro2_fired, passed by
  // reference from fireDrogue()/fireMain()) is now ALSO persisted to RTC
  // and restored on a resume boot (see IGNIS_FC_heavy_13.ino's setup()) —
  // this latch used to live only in RAM, so an in-flight reset after a
  // successful fire could let the FSM call this function again on the
  // next boot. That's not just a logging gap: a channel that already
  // fired has a burned-open bridgewire, so re-asserting the gate here
  // would drive Q5 into a guaranteed-open loop — the same electrical
  // signature already implicated as a reset candidate elsewhere.
  firedFlag = true;
  // Opens the forced-flush window in DataLogger.h so every row for the next
  // PYRO_FORCE_FLUSH_MS is committed to the card immediately. FLIGHT072 lost
  // the entire post-fire window because the next scheduled flush never came.
  lastPyroEventMs = fireStartMs;
  rtcSetLastOp(OP_IDLE);
}

// Call once per loop() — turns off any pin whose pulse duration has
// elapsed. This replaces the old blocking delay(PYRO_FIRE_PULSE_MS).
static void maintainPyroPulses() {
  unsigned long now = millis();
  if (pyro1_pulseActive && (now - pyro1_fireStartMs >= PYRO_FIRE_PULSE_MS)) {
    digitalWrite(PYRO1_FIRE_PIN, LOW);
    pyro1_pulseActive = false;
  }
  if (pyro2_pulseActive && (now - pyro2_fireStartMs >= PYRO_FIRE_PULSE_MS)) {
    digitalWrite(PYRO2_FIRE_PIN, LOW);
    pyro2_pulseActive = false;
  }
}

static void fireDrogue(FlightData& d) {
  if (d.state < STATE_ARMED) return;
  fireChannel(PYRO1_FIRE_PIN, d.pyro1_fired, pyro1_pulseActive, pyro1_fireStartMs, "DROGUE");
}

static void fireMain(FlightData& d) {
  if (d.state < STATE_ARMED) return;
  fireChannel(PYRO2_FIRE_PIN, d.pyro2_fired, pyro2_pulseActive, pyro2_fireStartMs, "MAIN");
}
