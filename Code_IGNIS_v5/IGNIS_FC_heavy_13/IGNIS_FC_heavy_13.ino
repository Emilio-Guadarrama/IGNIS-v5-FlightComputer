/*
  ================================================================
  IGNIS v2 Flight Computer — Main Sketch
  Ignitia Rocket Lab — LASC 2026, Rocket Challenge (Mission ID 22)
  ================================================================
  BEFORE THIS COMPILES you must fill in every TODO pin in Config.h:
  I2C_SDA/SCL, WS2812_DATA, PYRO1/2_FIRE, PYRO1/2_CONT, VBAT_MON,
  RFM95_CS/RST/DIO0. Confirm each against your schematic — especially
  the pyro pins. Do not fly on guessed pins.

  Libraries required (Library Manager):
    TinyGPSPlus, RadioHead, Adafruit NeoPixel
  (MPU6050 and BMP180 are raw-register drivers in Sensors.h — no
   Adafruit_MPU6050 / Adafruit_BMP085 library needed for those two.
   SHT40 removed — not used on this build.)

  Flow: (power applied via RBF pull-pin = physically armed) -> boot ->
        STATE_ARMED -> BOOST -> COAST -> APOGEE (drogue fires) ->
        DROGUE_DESCENT -> (450m AGL, main fires) -> MAIN_DESCENT -> LANDED
        ...UNLESS this boot is a RESUME (v13, see below): in that case
        setup() skips straight into whatever phase the RTC record says
        the vehicle was already in, instead of starting this flow over
        from STATE_ARMED.

  v13 (2026-09-22) — post-FLIGHT051/052 revision. See docs/CHANGELOG.md:
    - RTC-persisted resume across an in-flight reset: setup() previously
      had no concept of "we already launched," so ANY in-flight reset
      (including the confirmed ESP_RST_INT_WDT reset between FLIGHT051
      and FLIGHT052) restarted the whole state machine from STATE_ARMED,
      requiring boost/apogee to be re-detected from scratch. Now a
      resumed boot skips MPU calibration (also wrong to redo on a moving
      body), skips re-arming, and reopens the SAME SD file instead of
      starting a new one.
    - pyro1_fired/pyro2_fired are now part of that persisted record —
      previously RAM-only, meaning a reset after a successful fire could
      let a later boot try to re-fire an already-spent (burned-open)
      channel.
    - Wire.setTimeOut() + runtime I2C bus recovery (Sensors.h), and a
      bounded post-pyro SD flush interval instead of flushing every row
      (DataLogger.h) — both aimed at the confirmed watchdog-reset root
      cause, not just its symptoms.
    - RTC "last operation" breadcrumb: any future reset now shows not
      just THAT it happened (reset_reason) but WHAT the FC was doing
      right before (I2C read / SD write / SD mount / radio send / pyro
      fire / MPU calibration) — see FlightData.h's RtcFlightState.
    - Removed the hardcoded FLIGHT072 callouts from the LIVE dashboard
      (Dashboard.h) — kept once, here, in the boot-time NO-GO banner,
      instead of repeating on every dashboard refresh in flight.

  v11 (2026-08-15) — post-FLIGHT072 revision. See CHANGELOG_v11.md. Summary:
    - Liftoff threshold 3.0 g -> 1.8 g (FLIGHT072 peaked at 3.222 g)
    - Barometric liftoff backup path (survives a dead IMU)
    - Burnout + apogee failsafe timeouts
    - Minimum drogue->main stagger
    - Post-pyro forced SD flush window
    - NVS boot counter + reset reason (mid-flight reboot detection)
    - Audible NO-GO on open pyro continuity
    - Integrated-gyro tilt estimate (LOGGED ONLY, never gates pyro)

  v12 (2026-08-15) - post-FLIGHT016/018 pulley tests. See CHANGELOG_v12.md:
    - Boost debounce 40 -> 250 ms (pulley jerks were triggering BOOST)
    - MIN_APOGEE_ALT_M interlock: no pyro decision below 30 m peak AGL
    - GROUND_TEST_MODE for bench/pulley work
    - SINGLE_EVENT_MODE: CH2 never fired (unused channel disabled, not just
      unwired - firing into an open loop is itself a reset risk)
    - Battery NO-GO folded into the persistent indicators, not just boot
    - reset_reason decode corrected: ESP_RST_BROWNOUT is 9, not 6

  ARMING IS PHYSICAL, NOT SOFTWARE: this board arms via the RBF pull-pin
  cutting/restoring battery power (ECS 7.2.2/9.1.6). There is no GPIO arm
  button — CONFIRMED with Emilio. By the time this firmware is running,
  the board is already live, so setup() transitions straight to
  STATE_ARMED once init is complete. Handle a powered board as LIVE PYRO
  at all times, including on the bench.
  ================================================================
*/

#include <Preferences.h>
#include "esp_system.h"
#include "Config.h"
#include "FlightData.h"
#include "DataLogger.h"     // must run its SPI.begin() before Telemetry.h uses the radio
#include "Sensors.h"
#include "Pyro.h"
#include "FlightState.h"
#include "Telemetry.h"
#include "StatusIndicator.h"
#include "Dashboard.h"      // must be included last — reads static state from the modules above

static FlightData fd;
static unsigned long lastSensorLoopMs = 0;
static unsigned long lastTelemetryMs = 0;

// Arming is physical (RBF pull-pin), not a GPIO — see header comment.
// No armButtonPressed() needed; setup() transitions to STATE_ARMED directly.

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("=== IGNIS v2 Flight Computer (v13) — boot ===");
  Serial.println("!!! Board is physically armed via RBF pull-pin. If power is applied, pyro is LIVE. !!!");

  // ---- BROWNOUT / RESET FORENSICS (v11) ----
  // FLIGHT072 rebooted in mid-air roughly 65 m above ground, ~2.5 s before
  // impact, within ~1.2 s of the drogue gate going HIGH. The ONLY reason we
  // know that is that the ground station happened to see the packet sequence
  // counter reset to 0. The FC itself had no record. Now it does: boot_count
  // is persisted in NVS and written into every SD row and telemetry packet,
  // so a mid-flight reboot is visible directly in the flight log.
  {
    Preferences prefs;
    prefs.begin("ignis", false);
    fd.boot_count = prefs.getUShort("boots", 0) + 1;
    prefs.putUShort("boots", fd.boot_count);
    prefs.end();
  }
  fd.reset_reason = (uint8_t)esp_reset_reason();

  // ---- RESUME DETECTION (v13) ----
  // Confirmed via FLIGHT051/052 telemetry: FLIGHT051 ended mid-STATE_COAST
  // (before any pyro fire) with the next boot's reset_reason = 5
  // (ESP_RST_INT_WDT) — a real in-flight reset, not a bench/pad reset. A
  // resume is only attempted when BOTH of these hold: this was NOT a clean
  // power-on (rules out a normal fresh flight, or a deliberate bench power
  // cycle), AND the RTC record's magic number is valid with a phase that
  // shows the vehicle was already at/past STATE_BOOST (rules out treating
  // an unrelated reset while just sitting on the pad as a resume). If the
  // RTC domain itself lost power (a brownout severe enough to fully cycle
  // it), rtcState.magic will simply not match and this falls back to the
  // old cold-start behavior automatically — no special-casing needed.
  bool rtcValid  = (rtcState.magic == RTC_FLIGHT_MAGIC);
  bool wasFlying = rtcValid && (rtcState.phase >= (uint8_t)STATE_BOOST);
  bool isResume  = RTC_RESUME_ENABLED &&
                    (fd.reset_reason != ESP_RST_POWERON) &&
                    wasFlying;
  fd.resumed_this_boot = isResume;

  Serial.printf("[BOOT] boot_count=%u  reset_reason=%u %s  last_op=%s\n",
                (unsigned)fd.boot_count, (unsigned)fd.reset_reason,
                (fd.reset_reason == ESP_RST_BROWNOUT) ? "*** BROWNOUT ***" :
                (fd.reset_reason == ESP_RST_PANIC)    ? "*** PANIC ***"    :
                (fd.reset_reason == ESP_RST_WDT ||
                 fd.reset_reason == ESP_RST_TASK_WDT ||
                 fd.reset_reason == ESP_RST_INT_WDT)  ? "*** WATCHDOG ***"  : "",
                rtcValid ? rtcLastOpName(rtcState.lastOp) : "n/a (no RTC record)");

  if (isResume) {
    Serial.println("################################################################");
    Serial.println("###  RESUME: this looks like an in-flight reset, not a fresh  ###");
    Serial.printf ("###  boot. Resuming from phase=%-14s instead of re-arming. ###\n",
                   flightStateName((FlightState_t)rtcState.phase));
    Serial.println("################################################################");
  }

  bool sensorsOk = initSensors(isResume);
  Serial.printf("[BOOT] Sensors: %s\n", sensorsOk ? "OK" : "DEGRADED");

  initPyro();
  if (isResume) {
    // Restore the one-shot fire latches BEFORE anything else can call
    // fireDrogue()/fireMain() — see Pyro.h's fireChannel() header comment
    // on why a RAM-only latch is a re-fire risk across a reset.
    fd.pyro1_fired = rtcState.pyro1_fired;
    fd.pyro2_fired = rtcState.pyro2_fired;
  }
  checkPyroContinuity(fd);
  Serial.printf("[BOOT] Pyro continuity — CH1(drogue): %s  CH2(main): %s\n",
                fd.pyro1_continuity ? "OK" : "OPEN",
                fd.pyro2_continuity ? "OK" : "OPEN");
  bool pyroNoGo = (!fd.pyro1_continuity || !fd.pyro2_continuity);
  readBattery(fd);   // populate battery_v before the NO-GO evaluation below
  bool battNoGo = (fd.battery_v < VBAT_NOGO_V);
  Serial.printf("[BOOT] VBAT: %.2f V  %s\n", fd.battery_v,
                battNoGo ? "*** BELOW NO-GO THRESHOLD ***" :
                (fd.battery_v < VBAT_WARN_V) ? "(low - consider a fresh pack)" : "OK");

  bool sdOk = initDataLogger(isResume);
  Serial.printf("[BOOT] SD logging: %s\n", sdOk ? "OK" : "DISABLED (continuing)");

  bool loraInitOk = initTelemetry();
  Serial.printf("[BOOT] LoRa telemetry: %s\n", loraInitOk ? "OK" : "DISABLED (continuing)");

  initStatusIndicator();

  // ---- AUDIBLE PRE-FLIGHT NO-GO (v11) ----
  // Must come AFTER initStatusIndicator(), which is where the buzzer PWM
  // channel and the WS2812 strip are actually brought up. See the NOGO_MELODY
  // comment in StatusIndicator.h for why this exists.
#if GROUND_TEST_MODE
  Serial.println("################################################################");
  Serial.println("###  GROUND_TEST_MODE IS ON - THIS IS NOT FLIGHT CONFIG       ###");
  Serial.printf ("###  min apogee alt lowered to %.1f m, debounce to %lu ms      ###\n",
                 (double)GROUND_TEST_MIN_ALT_M, (unsigned long)GROUND_TEST_DEBOUNCE_MS);
  Serial.println("###  Set GROUND_TEST_MODE to false in Config.h before flight. ###");
  Serial.println("################################################################");
#endif

  if (pyroNoGo || battNoGo) {
    Serial.println("################################################################");
    if (pyroNoGo) {
      Serial.println("###  NO-GO: PYRO CONTINUITY OPEN                             ###");
      Serial.printf ("###  CH1(drogue): %-6s   CH2(main): %-6s               ###\n",
                     fd.pyro1_continuity ? "OK" : "OPEN",
                     fd.pyro2_continuity ? "OK" : "OPEN");
      Serial.println("###  FLIGHT072 flew with CH1 OPEN and never ignited.         ###");
    }
    if (battNoGo) {
      Serial.printf ("###  NO-GO: VBAT %.2f V (threshold %.2f V)                 ###\n",
                     fd.battery_v, (double)VBAT_NOGO_V);
      Serial.println("###  FLIGHT072 flew at 7.73 V (~45% SoC).                    ###");
    }
    Serial.println("###  ADVISORY ONLY - the FC will still arm and still fire.    ###");
    Serial.println("###  Resolve this BEFORE installing the board in the airframe.###");
    Serial.println("################################################################");
    playNoGoTone(3);
  }

  if (isResume) {
    // v13: skip the ARMED re-arm entirely — resumeFlightState() (FlightState.h)
    // sets fd.state directly from the persisted phase, restores
    // maxAglSinceBoost/ground-pressure calibration, and re-arms the
    // relevant phase timers against the new clock. See its header comment
    // and RtcFlightState in FlightData.h for what is and isn't preserved.
    resumeFlightState(fd);
    updateStatusIndicator(fd);
    Serial.printf("=== Boot complete — RESUMED into %s (LIVE PYRO). ===\n",
                  flightStateName(fd.state));
  } else {
    // No software arm step exists on this hardware — the RBF pull-pin is
    // the sole arm/safe gate, and it already happened before this code ran.
    // Go straight to ARMED so the LED/buzzer correctly reflect LIVE PYRO
    // from the moment boot completes.
    fd.state = STATE_ARMED;
    updateStatusIndicator(fd); // red: ARMED, LIVE PYRO
    playMelodyBlocking(ARMED_MELODY, ARMED_MELODY_LEN); // boot-safe blocking (pre-loop) — see StatusIndicator.h
    Serial.println("=== Boot complete — ARMED (LIVE PYRO). ===");
  }
}

void loop() {
  unsigned long now = millis();

  // ---- Main sensor / state loop (~50 Hz) ----
  if (now - lastSensorLoopMs >= SENSOR_LOOP_INTERVAL_MS) {
    lastSensorLoopMs = now;

    FlightState_t stateBefore = fd.state;

    readSensors(fd);
    checkPyroContinuity(fd);
    // RSSI/SNR intentionally NOT read here — CONFIRMED via bench test to
    // always read the register reset value (-157dBm / 0.0dB, unvarying)
    // because this radio only ever transmits; RegRssiValue/RegPktSnrValue
    // only update in RX mode. Real link-quality numbers live on the
    // ground station (IGNIS_GroundStation.ino), which is the actual
    // receiver. fd.rssi_dbm/snr_db are left at 0 — see FlightData.h.

    updateFlightState(fd);
    maintainPyroPulses(); // non-blocking pulse-off — see Pyro.h header for why this replaced delay()
    rtcSyncFlightState(fd); // v13 — keeps the resume record current every tick, see FlightState.h

    bool stateChanged = (fd.state != stateBefore);
    logFlightData(fd, stateChanged); // force flush on any state transition
    updateStatusIndicator(fd);
    maintainHeartbeatBeep(fd); // slow standby beep while PAD_IDLE/ARMED — silent once flying
  }

  // ---- SD retry-on-timer (no-op if already logging or session ended) ----
  maintainSDConnection(fd.state);

  // ---- Telemetry loop (rate depends on phase) ----
  unsigned long telInterval = (fd.state <= STATE_ARMED)
                                 ? TELEMETRY_INTERVAL_MS_PAD
                                 : TELEMETRY_INTERVAL_MS_FLT;
  if (now - lastTelemetryMs >= telInterval) {
    lastTelemetryMs = now;
    sendTelemetry(fd);
  }

  // ---- Terminal state: keep logging/telemetry alive, close file once stable ----
  static bool logClosed = false;
  if (fd.state == STATE_LANDED && !logClosed) {
    closeLog();
    logClosed = true;
    Serial.println("[SD] Log closed — safe for recovery.");
  }

  // ---- Live status dashboard (self-throttled, see Dashboard.h) ----
  updateDashboard(fd, now);
}
