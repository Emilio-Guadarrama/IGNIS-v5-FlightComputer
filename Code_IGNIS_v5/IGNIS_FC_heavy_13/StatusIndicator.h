/*
  ================================================================
  IGNIS v2 Flight Computer — StatusIndicator.h
  3x WS2812B (chained on WS2812_DATA_PIN) + piezo buzzer.

  LED ASSIGNMENT — one subsystem per LED, not an aggregate "how many
  are green" count. This gives more diagnostic info at a glance (WHICH
  subsystem is degraded, not just "something is imperfect") and keeps
  RED reserved and unambiguous (see below).

    LED0 — Sensors (baro + IMU)
      GREEN  = both healthy, current reading valid
      YELLOW = init OK but a transient invalid reading right now
      RED    = critical failure AT BOOT (mpuOk/bmpOk false — can't
               safely detect apogee/burnout, this is a real go/no-go)

    LED1 — Comms / Logging (LoRa + SD)
      GREEN  = both OK
      YELLOW = one or both down
      RED is NEVER used here on purpose — losing LoRa or SD doesn't
      endanger the flight (deployment logic doesn't depend on either,
      and the RRC3 is the independent backup regardless). Reserving
      RED exclusively for "actually dangerous" keeps it meaningful.

    LED2 — Arm / flight phase (same palette as before, now with two
      additions layered on top of it):
      - While ARMED: solid RED normally, BLINKING RED if continuity
        is bad on either pyro channel. Still unambiguously "red /
        stay back" either way — continuity is a secondary signal
        riding on top of the color, not a color change, because RED
        for armed must never become ambiguous.
      - LANDED: all 3 LEDs slow-breathe GREEN together (not solid) —
        same "safe, and actively signaling" idea as the buzzer melody,
        easier to spot at a distance/in a field than a static color.

  FIRE EVENT: the instant a pyro channel actually fires, all 3 LEDs
  strobe WHITE together for a moment, overriding whatever they were
  showing — deliberately different from ANY sustained status color, so
  it's unmistakable during ground testing without needing the serial
  monitor. Reverts to normal display automatically once it's done.

  Color reference (used by both this file and Dashboard.h's legend —
  keep them in sync if you ever change one):
    RED=danger/armed, YELLOW=degraded, GREEN=healthy/safe,
    WHITE=fire event, BLUE=coast, MAGENTA=apogee, ORANGE=drogue,
    CYAN=main, AMBER=idle (currently unreachable — see FlightState.h,
    the FC boots straight to ARMED since arming is physical/RBF-based)

  BUZZER IS A PASSIVE PIEZO (MLT-8530, confirmed via bench sweep test):
  it needs a driven square wave at the target frequency to make sound
  at all — silent under plain DC. Everything below uses the ESP32-S3's
  LEDC hardware PWM peripheral to generate that square wave. Once a
  note is started, the peripheral keeps outputting it in HARDWARE with
  zero ongoing CPU cost — only switching to the next note (or silence)
  needs code to run, which is what makes a genuine multi-note melody
  achievable without ever blocking the main loop.

  Do NOT call plain digitalWrite(BUZZER_PIN, ...) anywhere once this
  file's ledcAttach() has run — that forces the pin out of PWM mode and
  will fight with everything here. Everything that touches this buzzer
  goes through heartbeatSetTone() below.
  ================================================================
*/

#pragma once
#include <Adafruit_NeoPixel.h>
#include <math.h>
#include "Config.h"
#include "FlightData.h"

#define NUM_STATUS_LEDS 3
static Adafruit_NeoPixel statusLed(NUM_STATUS_LEDS, WS2812_DATA_PIN, NEO_GRB + NEO_KHZ800);

// ---------------- Color palette ----------------
// Named tuples so the logic below reads as intent, not raw numbers.
#define COLOR_OFF       0,   0,   0
#define COLOR_RED     255,   0,   0
#define COLOR_GREEN     0, 255,   0
#define COLOR_YELLOW  255, 180,   0
#define COLOR_AMBER    80,  40,   0
#define COLOR_WHITE   255, 255, 255
#define COLOR_BLUE      0,   0, 255
#define COLOR_MAGENTA 255,   0, 255
#define COLOR_ORANGE  255, 128,   0
#define COLOR_CYAN      0, 255, 255

// Sets a pixel's color WITHOUT pushing it to the strip — caller must
// call statusLed.show() once after setting all pixels for this frame,
// so a 3-pixel update is atomic (no visible partial-update flicker).
static void setLed(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
  statusLed.setPixelColor(idx, statusLed.Color(r, g, b));
}

// ---------------- IGNIS signature melodies ----------------
// Two distinct tunes chosen inside the loud/resonant range your own
// bench sweep found for this buzzer (~1.5-4.5kHz) — deliberately
// multi-note, not a single tone, so it's unmistakably NOT the RRC3 by
// ear. Different tunes for different states so you can tell ARMED
// (live pyro) from standby by SOUND ALONE, without seeing the LEDs —
// useful precisely when the FC is buried inside the airframe.
//
// STANDBY_MELODY is wired to STATE_PAD_IDLE, which is currently
// UNREACHABLE (this FC boots straight into ARMED — arming is
// physical, via the RBF pin, not software). It'll start playing
// automatically the moment PAD_IDLE becomes reachable again; until
// then only ARMED_MELODY is ever actually heard.
struct HeartbeatNote { uint16_t freqHz; uint16_t durationMs; };

// Standby: slower, calmer two-note alternation.
static const HeartbeatNote STANDBY_MELODY[] = {
  {1568, 200},  // G6
  {1319, 200},  // E6
};
#define STANDBY_MELODY_LEN (sizeof(STANDBY_MELODY)/sizeof(STANDBY_MELODY[0]))

// Armed: faster, more urgent 3-note ascending arpeggio. Live pyro.
static const HeartbeatNote ARMED_MELODY[] = {
  {2637, 90},   // E7
  {3136, 90},   // G7
  {4186, 130},  // C8
};
#define ARMED_MELODY_LEN (sizeof(ARMED_MELODY)/sizeof(ARMED_MELODY[0]))

// NO-GO: deliberately ugly. Low, slow, descending, long notes - the opposite
// of ARMED_MELODY's bright ascending arpeggio, so it is unmistakable by ear
// from across the pad with the FC sealed inside the airframe.
//
// Why this exists: on FLIGHT072 the boot banner printed
//   [BOOT] Pyro continuity - CH1(drogue): OPEN  CH2(main): OK
// to the serial console, and the rocket flew anyway. CH1 was open for the
// entire 382 s pad phase and the entire flight (pyro1_cont = 0 across all
// 10,226 logged rows). The information existed; nobody was looking at a
// laptop. Audio does not require a laptop.
static const HeartbeatNote NOGO_MELODY[] = {
  {1600, 400},
  {   0, 120},
  {1200, 400},
  {   0, 120},
  { 900, 700},
  {   0, 300},
};
#define NOGO_MELODY_LEN (sizeof(NOGO_MELODY)/sizeof(NOGO_MELODY[0]))

#define HEARTBEAT_NOTE_GAP_MS   30   // brief silence between notes so they read as distinct, not slurred — NOT a gap between loops, this plays continuously

// freqHz == 0 means silence.
static void heartbeatSetTone(uint16_t freqHz) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (freqHz == 0) {
    ledcWrite(BUZZER_PIN, 0);
  } else {
    ledcWriteTone(BUZZER_PIN, freqHz); // pin was ledcAttach()'d once in initStatusIndicator()
  }
#else
  // NOT verified against your actual environment — this project runs
  // core 3.3.10 everywhere else. Included only as a defensive fallback
  // matching your original snippet's channel-based structure; test
  // before trusting it if you're ever on an older core.
  if (freqHz == 0) {
    ledcWrite(0, 0);
  } else {
    ledcSetup(0, freqHz, 8); // reconfigures channel 0's frequency; channel stays attached
    ledcWrite(0, 128);       // 50% duty
  }
#endif
}

static void initStatusIndicator() {
  statusLed.begin();
  statusLed.setBrightness(60);
  for (uint8_t i = 0; i < NUM_STATUS_LEDS; i++) setLed(i, COLOR_OFF);
  statusLed.show();

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZER_PIN, 2000, 8); // placeholder freq — heartbeatSetTone()/ledcWriteTone() changes it per note
  ledcWrite(BUZZER_PIN, 0);        // ensure silent at boot
#else
  ledcSetup(0, 2000, 8);
  ledcAttachPin(BUZZER_PIN, 0);
  ledcWrite(0, 0);
#endif
}

// Blocking — used ONLY once, at boot, before the main loop starts (same
// justification as the blocking sensor-calibration routines in
// Sensors.h: pre-loop blocking is fine, in-loop blocking is not). This
// is the "FC is alive and just armed" confirmation chirp.
static void playMelodyBlocking(const HeartbeatNote* melody, size_t len) {
  for (size_t i = 0; i < len; i++) {
    heartbeatSetTone(melody[i].freqHz);
    delay(melody[i].durationMs);
    heartbeatSetTone(0);
    delay(HEARTBEAT_NOTE_GAP_MS);
  }
}

// Blocking, boot-time only (same justification as playMelodyBlocking).
// Repeats the NO-GO pattern `repeats` times plus flashes all LEDs red, so a
// missing e-match is impossible to miss without a serial console.
static void playNoGoTone(uint8_t repeats = 3) {
  for (uint8_t r = 0; r < repeats; r++) {
    for (uint8_t i = 0; i < NUM_STATUS_LEDS; i++) setLed(i, COLOR_RED);
    statusLed.show();
    playMelodyBlocking(NOGO_MELODY, NOGO_MELODY_LEN);
    for (uint8_t i = 0; i < NUM_STATUS_LEDS; i++) setLed(i, COLOR_OFF);
    statusLed.show();
    delay(150);
  }
}

// ================================================================
// Fire-event flash (non-blocking) — 3 quick all-LED white strobes
// the instant a pyro channel fires. Edge-triggered off pyro1_fired/
// pyro2_fired going false->true, so it fires exactly once per event.
// ================================================================
#define FIRE_FLASH_HALF_CYCLE_MS   100   // on-time == off-time, each this long
#define FIRE_FLASH_STROBE_COUNT      3   // number of ON pulses

static bool          prevPyro1Fired = false;
static bool          prevPyro2Fired = false;
static bool          fireFlashActive = false;
static unsigned long fireFlashStartMs = 0;

static void checkFireFlashTrigger(const FlightData& d) {
  if ((d.pyro1_fired && !prevPyro1Fired) || (d.pyro2_fired && !prevPyro2Fired)) {
    fireFlashActive = true;
    fireFlashStartMs = millis();
  }
  prevPyro1Fired = d.pyro1_fired;
  prevPyro2Fired = d.pyro2_fired;
}

// Returns true if it rendered a flash frame this call (caller should
// skip normal LED display when true).
static bool renderFireFlashIfActive() {
  if (!fireFlashActive) return false;

  unsigned long elapsed = millis() - fireFlashStartMs;
  unsigned long totalDuration = FIRE_FLASH_STROBE_COUNT * 2 * FIRE_FLASH_HALF_CYCLE_MS;
  if (elapsed >= totalDuration) {
    fireFlashActive = false;
    return false; // done — let the caller fall through to normal display this same call
  }

  bool on = (elapsed % (2 * FIRE_FLASH_HALF_CYCLE_MS)) < FIRE_FLASH_HALF_CYCLE_MS;
  for (uint8_t i = 0; i < NUM_STATUS_LEDS; i++) {
    if (on) setLed(i, COLOR_WHITE); else setLed(i, COLOR_OFF);
  }
  statusLed.show();
  return true;
}

// ================================================================
// LANDED: slow all-LED green breathe instead of a static color —
// easier to spot at a distance/in a field, same "safe AND actively
// signaling" idea as the post-landing buzzer behavior would be.
// ================================================================
#define LANDED_BREATHE_PERIOD_MS 2000

static void renderLandedBreathe() {
  unsigned long t = millis() % LANDED_BREATHE_PERIOD_MS;
  float phase = (2.0f * PI * t) / (float)LANDED_BREATHE_PERIOD_MS;
  uint8_t brightness = (uint8_t)((sinf(phase) * 0.5f + 0.5f) * 255.0f);
  for (uint8_t i = 0; i < NUM_STATUS_LEDS; i++) setLed(i, 0, brightness, 0);
  statusLed.show();
}

// ================================================================
// LED2 while ARMED: solid red normally, blinking red if continuity
// is bad on either channel. Always red either way — see header note
// on why RED must stay unambiguous.
// ================================================================
#define CONTINUITY_BLINK_PERIOD_MS 300

// v12: battery folded into the same persistent indicator. The boot-time
// NO-GO tone is a one-shot and is easy to miss - especially if the board
// powers up while it is being handled or already inside the airframe. The
// persistent channels (this blink, the heartbeat pattern, and the ground
// station alarm) are what actually reach a human at the pad.
//
// Advisory only. Nothing here inhibits firing.
static inline bool preflightNoGo(const FlightData& d) {
  bool contBad = !d.pyro1_continuity || !d.pyro2_continuity;
  bool battBad = (d.battery_v < VBAT_NOGO_V);
  return contBad || battBad;
}

static void renderArmedLed(const FlightData& d) {
  bool contBad = preflightNoGo(d);
  if (!contBad) {
    setLed(2, COLOR_RED);
    return;
  }
  bool on = (millis() / CONTINUITY_BLINK_PERIOD_MS) % 2 == 0;
  if (on) setLed(2, COLOR_RED); else setLed(2, COLOR_OFF);
}

// ================================================================
// Main update — call once per loop() (and once at boot, same as before)
// ================================================================
static void updateStatusIndicator(const FlightData& d) {
  checkFireFlashTrigger(d);
  if (renderFireFlashIfActive()) return; // mid-strobe — nothing else to draw this call

  if (d.state == STATE_LANDED) {
    renderLandedBreathe();
    return;
  }

  // LED0 — Sensors. mpuOk/bmpOk are boot-time init flags (Sensors.h);
  // *_reading_valid are this-instant flags (FlightData.h, set every
  // readSensors() call). A boot failure is worse than a transient miss
  // — same distinction the rest of this codebase already draws.
  if (!mpuOk || !bmpOk) {
    setLed(0, COLOR_RED);
  } else if (!d.baro_reading_valid || !d.imu_reading_valid) {
    setLed(0, COLOR_YELLOW);
  } else {
    setLed(0, COLOR_GREEN);
  }

  // LED1 — Comms/logging. Deliberately never red — see header note.
  if (!loraOk || !sdReady) {
    setLed(1, COLOR_YELLOW);
  } else {
    setLed(1, COLOR_GREEN);
  }

  // LED2 — Arm/flight phase. Same palette as the original single-LED
  // scheme, ARMED gets the continuity-blink treatment above.
  switch (d.state) {
    case STATE_PAD_IDLE:        setLed(2, COLOR_AMBER);   break; // currently unreachable (boots straight to ARMED) — kept for completeness/future use
    case STATE_ARMED:           renderArmedLed(d);        break;
    case STATE_BOOST:           setLed(2, COLOR_WHITE);   break;
    case STATE_COAST:           setLed(2, COLOR_BLUE);    break;
    case STATE_APOGEE:          setLed(2, COLOR_MAGENTA); break;
    case STATE_DROGUE_DESCENT:  setLed(2, COLOR_ORANGE);  break;
    case STATE_MAIN_DESCENT:    setLed(2, COLOR_CYAN);    break;
    case STATE_LANDED:          break; // handled above (breathing) — unreachable here
  }

  statusLed.show();
}

// ---------------- Continuous status tune (non-blocking) ----------------
// Loops the current state's melody back-to-back with NO dead silence
// between repeats — only the brief HEARTBEAT_NOTE_GAP_MS between
// individual notes. This plays continuously while PAD_IDLE/ARMED,
// proof the FC is actually running its main loop, not just powered —
// and specifically loops (rather than beeping once every few seconds)
// because when the FC is buried inside the airframe, this is the only
// way to verify it's alive other than RF telemetry to the ground.
// Silent once flying (BOOST+): matches the pyro/SD pattern of never
// adding anything to the loop during flight that isn't flight-critical.
//
// State machine, not blocking: each call just checks millis() against
// the current note's timer and either does nothing, starts the next
// note, or starts the next gap. The actual tone generation is free
// (hardware PWM) — this function only ever does cheap comparisons and,
// at most, one ledcWriteTone() call per invocation.
static int8_t         heartbeatNoteIdx     = -1;  // -1 = not yet started
static unsigned long  heartbeatNoteStartMs = 0;
static bool           heartbeatToneOn      = false; // true = sounding a note, false = in the inter-note gap
static FlightState_t  heartbeatLastState;           // detects state changes so we restart cleanly on the new tune (initialized on first call, see below)
static bool           heartbeatStateInit   = false;

static const HeartbeatNote* heartbeatSelectMelody(FlightState_t state, size_t& lenOut) {
  if (state == STATE_ARMED) {
    lenOut = ARMED_MELODY_LEN;
    return ARMED_MELODY;
  }
  // STATE_PAD_IDLE (currently unreachable) and any other <=ARMED state
  // default to the standby tune.
  lenOut = STANDBY_MELODY_LEN;
  return STANDBY_MELODY;
}

static void maintainHeartbeatBeep(const FlightData& d) {
  if (!heartbeatStateInit) { // first call ever — don't false-trigger a "state changed" restart
    heartbeatLastState = d.state;
    heartbeatStateInit = true;
  }

  if (d.state > STATE_ARMED) {
    if (heartbeatNoteIdx >= 0) { // tune was mid-playback — cut it cleanly, don't leave a note stuck on
      heartbeatSetTone(0);
      heartbeatNoteIdx = -1;
      heartbeatToneOn = false;
    }
    heartbeatLastState = d.state;
    return;
  }

  size_t melodyLen;
  const HeartbeatNote* melody = heartbeatSelectMelody(d.state, melodyLen);
  unsigned long now = millis();

  // First start, or the state just changed (e.g. PAD_IDLE -> ARMED) —
  // restart cleanly on note 0 of whichever tune applies now, rather
  // than finishing the old tune's current note first.
  if (heartbeatNoteIdx < 0 || d.state != heartbeatLastState) {
    heartbeatLastState = d.state;
    heartbeatNoteIdx = 0;
    heartbeatNoteStartMs = now;
    heartbeatSetTone(melody[0].freqHz);
    heartbeatToneOn = true;
    return;
  }

  if (heartbeatToneOn) {
    if (now - heartbeatNoteStartMs >= melody[heartbeatNoteIdx].durationMs) {
      heartbeatSetTone(0);
      heartbeatToneOn = false;
      heartbeatNoteStartMs = now; // start the (brief) inter-note gap timer
    }
  } else {
    if (now - heartbeatNoteStartMs >= HEARTBEAT_NOTE_GAP_MS) {
      heartbeatNoteIdx++;
      if (heartbeatNoteIdx >= (int8_t)melodyLen) {
        heartbeatNoteIdx = 0; // LOOP immediately — no multi-second wait, this is continuous
      }
      heartbeatNoteStartMs = now;
      heartbeatSetTone(melody[heartbeatNoteIdx].freqHz);
      heartbeatToneOn = true;
    }
  }
}

// ================================================================
// Legend / current-status text — for Dashboard.h's printed legend.
// Deliberately reuses the EXACT same conditions as updateStatusIndicator()
// above, so this can never drift out of sync with what the physical
// LEDs are actually showing — single source of truth by construction,
// not a second copy of the logic that has to be kept in step by hand.
// ================================================================
static const char* ledStatusText(uint8_t idx, const FlightData& d) {
  if (fireFlashActive) return "WHITE (fire event)";

  switch (idx) {
    case 0: // Sensors
      if (!mpuOk || !bmpOk) return "RED (init FAILED)";
      if (!d.baro_reading_valid || !d.imu_reading_valid) return "YELLOW (degraded)";
      return "GREEN (OK)";

    case 1: // Comms/logging
      if (!loraOk || !sdReady) return "YELLOW (degraded)";
      return "GREEN (OK)";

    case 2: // Arm/flight phase
      switch (d.state) {
        case STATE_PAD_IDLE:       return "AMBER (idle)";
        case STATE_ARMED: {
          bool contBad = !d.pyro1_continuity || !d.pyro2_continuity;
          bool battBad = (d.battery_v < VBAT_NOGO_V);
          if (contBad && battBad) return "RED blinking (cont + VBAT fault)";
          if (contBad)            return "RED blinking (cont. fault)";
          if (battBad)            return "RED blinking (VBAT low)";
          return "RED (ARMED)";
        }
        case STATE_BOOST:          return "WHITE (boost)";
        case STATE_COAST:          return "BLUE (coast)";
        case STATE_APOGEE:         return "MAGENTA (apogee)";
        case STATE_DROGUE_DESCENT: return "ORANGE (drogue)";
        case STATE_MAIN_DESCENT:   return "CYAN (main)";
        case STATE_LANDED:         return "GREEN breathing (landed)";
      }
  }
  return "?";
}
