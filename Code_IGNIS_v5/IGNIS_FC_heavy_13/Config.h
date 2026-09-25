/*
  ================================================================
  IGNIS v2 Flight Computer — Config.h
  Ignitia Rocket Lab — LASC 2026 (Mission ID 22)
  ================================================================
  PSRAM RESERVED PINS — never assign these to anything, ever:
    IO35, IO36, IO37  (Octal PSRAM data/strapping)
    IO45 must float (strapping pin)

  Pins below marked CONFIRMED were read off your schematic images.
  Pins marked TODO are NOT in anything you've shared with me — they
  are wired to trigger a compile error until you fill them in from
  the schematic. This is deliberate, especially for the pyro pins.
  ================================================================
*/

#pragma once

// ---------------- CONFIRMED PINS (from schematic) ----------------
#define SD_MOSI_PIN       11   // net: SNS_SPI_MOSI
#define SD_MISO_PIN       13   // net: SNS_SPI_MISO
#define SD_SCK_PIN        12   // net: SNS_SPI_SCK
#define SD_CS_PIN         42   // net: SD_CS
#define SD_DETECT_PIN     41   // net: SD_DETECT

// CONFIRMED from full MCU pinout sheet (Microcontroller - ESP32-S3).
#define BUZZER_PIN        14   // net: BUZZER — also verified working standalone

// GPS UART pair — CONFIRMED pins from schematic, direction INFERRED from
// device-centric net naming convention (GPS_UART_TX = GPS module's own TX
// pin, GPS_UART_RX = GPS module's own RX pin — consistent with how
// RFM95W_CS/RFM95W_RESET etc. are named after the device side, not the
// ESP32 side). This means ESP32 RX = IO17, ESP32 TX = IO18 — the OPPOSITE
// of the previous placeholder. GPS is non-critical to flight logic
// (initSensors() doesn't require it), so verify on the bench: if no fix /
// no NMEA chatter, try swapping.
#define GPS_RX_PIN        17   // ESP32 RX <- GPS TX (net GPS_UART_TX)
#define GPS_TX_PIN        18   // ESP32 TX -> GPS RX (net GPS_UART_RX)

// ---------------- TODO PINS — fill in from schematic ----------------
#define I2C_SDA_PIN       21   // CONFIRMED: net SNS_I2C_SDA
#define I2C_SCL_PIN       38   // CONFIRMED: net SNS_I2C_SCL (NOT IO37 — verify against your board silk if unsure)

#define WS2812_DATA_PIN    6   // CONFIRMED: net WS2812B_DATA — drives 3 chained WS2812B LEDs (see StatusIndicator.h)

#define PYRO1_FIRE_PIN     4   // CONFIRMED: net PYRO_CH1_GATE
#define PYRO2_FIRE_PIN     5   // CONFIRMED: net PYRO_CH2_GATE
#define PYRO1_CONT_PIN     1   // CONFIRMED: net PYRO_CH1_CONT (digital, opto per schematic)
#define PYRO2_CONT_PIN     2   // CONFIRMED: net PYRO_CH2_CONT

// NOT APPLICABLE on this hardware — CONFIRMED with Emilio: arming is
// purely physical, via the RBF pull-pin that cuts/restores battery power
// (ECS 7.2.2/9.1.6). There is no GPIO arm signal to read. By the time this
// firmware is running at all, the board is already live — see IGNIS_FC.ino,
// which now boots directly into STATE_ARMED at the end of setup() instead
// of polling a button that doesn't exist.

#define VBAT_MON_PIN       3   // CONFIRMED: net VBAT_MON

#define RFM95_CS_PIN      10   // CONFIRMED: net RFM95W_CS
#define RFM95_RST_PIN      8   // CONFIRMED: net RFM95W_RESET
#define RFM95_DIO0_PIN     7   // CONFIRMED: net RFM95W_INT
// CONFIRMED from schematic: RFM95W shares the SD SPI bus (SNS_SPI_MISO/
// MOSI/SCK, same nets/pull-ups R24-27 feed both the SD slot and the LoRa
// module) with its own CS pin. See DataLogger.h — SPI.begin() (the global
// SPI object) must run unconditionally regardless of SD card presence.
#define RFM95_SHARES_SD_SPI_BUS  true

// NOT WIRED INTO FIRMWARE YET — present on schematic, unused by this code:
//   MPU6050_INT -> IO9   (interrupt-driven IMU reads instead of polling)
//   FIX_GPS     -> IO35  *** CONFLICT: IO35 is a reserved Octal PSRAM pin
//                          on your N16R8 module. Do NOT connect/use this
//                          until you've confirmed with the schematic
//                          author whether this net actually needs moving. ***
//   BOOT        -> IO0   (standard strapping pin, not a general GPIO here)

#if (I2C_SDA_PIN < 0) || (I2C_SCL_PIN < 0)
#error "Config.h: set I2C_SDA_PIN / I2C_SCL_PIN from your schematic before compiling."
#endif
#if (WS2812_DATA_PIN < 0)
#error "Config.h: set WS2812_DATA_PIN from your schematic before compiling."
#endif
#if (PYRO1_FIRE_PIN < 0) || (PYRO2_FIRE_PIN < 0) || (PYRO1_CONT_PIN < 0) || (PYRO2_CONT_PIN < 0)
#error "Config.h: pyro pins are NOT SET. Confirm every pyro pin against the schematic yourself before compiling — this is not something to guess."
#endif
#if (VBAT_MON_PIN < 0)
#error "Config.h: set VBAT_MON_PIN from your schematic before compiling."
#endif
#if (RFM95_CS_PIN < 0) || (RFM95_RST_PIN < 0) || (RFM95_DIO0_PIN < 0)
#error "Config.h: set RFM95_CS_PIN / RFM95_RST_PIN / RFM95_DIO0_PIN from your schematic before compiling."
#endif

// ---------------- I2C addresses ----------------
#define MPU6050_ADDR      0x68   // AD0 tied GND
#define BMP180_ADDR       0x77
// SHT40 removed — not used on this build.

// ---------------- BMP180 raw-register driver ----------------
// Absolute altitude reference. AGL is NOT affected by this — AGL is now
// computed directly from the pressure ratio against a measured ground
// pressure (see FlightState.h), not from this constant. This only
// affects the logged/telemetered *absolute* altitude field. Update for
// launch-day QNH if that number needs to mean something to you.
#define SEA_LEVEL_PRESSURE_PA   101325.0f

// Oversampling trades read latency for resolution (datasheet approx.):
// OSS0 ~4.5ms, OSS1 ~7.5ms, OSS2 ~13.5ms, OSS3 ~25.5ms per pressure read.
// A single BMP180 read at OSS3 (~26ms) alone exceeds our 20ms sensor-loop
// budget, so we adapt: max precision while sitting on the pad (not time
// critical), faster/lower-latency once flying (apogee timing IS time
// critical, and the added resolution from OSS3 doesn't meaningfully
// improve a monotonic-falling trend detector the way lower latency does).
#define BMP180_OSS_IDLE          3   // PAD_IDLE / ARMED — ultra-high-res
#define BMP180_OSS_FLIGHT        1   // BOOST and beyond — high-res, ~8ms
#define GROUND_PRESSURE_CAL_SAMPLES  20   // valid (non-corrupt) samples averaged for ground reference

// ---------------- MPU6050 raw-register driver ----------------
// Accel+gyro bias calibration at boot, board assumed stationary
// (consistent with the existing ground-pressure-capture assumption).
// Iterative convergence (see mpuCalibrateBias() in Sensors.h) rather
// than a single pass — deadzones are in physical units, scaled to
// whatever range the sensor is actually configured for.
#define ACCEL_GYRO_CAL_MAX_ITERATIONS     8
#define ACCEL_GYRO_CAL_SAMPLES_PER_ITER  100
#define ACCEL_CAL_DEADZONE_G            0.01f   // ~10 mg
#define GYRO_CAL_DEADZONE_DPS           0.05f

// ---------------- LoRa (rocket FC link) ----------------
#define LORA_FREQ_MHZ     915.0
#define LORA_SYNC_WORD    0x12
#define LORA_SF           9
#define LORA_BW_KHZ       125.0
#define LORA_CR_DENOM     5      // 4/5
#define LORA_TX_POWER_DBM 17     // PA_BOOST

// ---------------- Power ----------------
#define VBAT_DIVIDER_RATIO   0.3007f   // per R5=43k substitution
#define ADC_REF_V             3.3f
#define ADC_MAX_COUNTS        4095.0f  // 12-bit ADC

// ---------------- Flight profile constants ----------------
#define TARGET_APOGEE_M         1000.0f
#define MAIN_DEPLOY_ALT_AGL_M    450.0f   // REC 8.1.4: main deployment <= 500 m AGL. Compliant.

// ---- LIFTOFF DETECTION - REVISED after FLIGHT072 (2026-08-15) ----
// Old value was 3.0f, and the FSM compares against RAW accel_total_g.
// FLIGHT072 peaked at accel_total_g = 3.222 g. Margin over threshold: 0.222 g
// (7%). A motor 7% weaker would have left the FSM in STATE_ARMED for the
// entire flight and nothing would ever have fired. With propellant
// manufactured locally in Brazil, that margin is not acceptable.
//
// New value justified against 380 s of measured pad data from FLIGHT072:
//   mean 1.0001 g | p99.9 1.148 g | absolute max 1.543 g | samples > 1.8 g: 0
#define BOOST_ACCEL_THRESHOLD_G   1.8f

// *** REVISED AGAIN after the FLIGHT016 / FLIGHT018 pulley tests. ***
// 40 ms was far too short. BOTH pulley tests triggered BOOST off a hand
// jerk, and FLIGHT018 then fired the drogue at agl = -0.80 m. On the ground.
// Measured pre-BOOST handling noise in those tests peaked at 1.931 g and
// 2.169 g - i.e. ABOVE this threshold. Level alone cannot discriminate.
//
// Duration can. Longest continuous run above 1.8 g:
//   FLIGHT072 (real KNSU motor) : 2111 ms
//   FLIGHT016 (pulley jerk)     :   80 ms
//   FLIGHT018 (pulley jerk)     :   93 ms
// Two orders of magnitude apart. 250 ms rejects handling transients with
// ~2.7x margin and accepts a real burn with ~8x margin.
#define BOOST_DEBOUNCE_MS          250

// Barometric liftoff backup - triggers BOOST even with a dead IMU.
// FLIGHT073 proved this failure case is real, not theoretical: after impact
// the MPU6050 stopped validating (imu_valid = 0 across all 12,970 rows) while
// the BMP180 on the same I2C bus kept working normally.
// Threshold set above the measured pad climb-rate noise peak of 9.88 m/s.
#define BOOST_BARO_CLIMB_MPS      15.0f
#define BOOST_BARO_SUSTAIN_MS      150
#define BARO_VELOCITY_WINDOW_MS    200    // finite-difference baseline age

// Coast->apogee: motor burnout detected when total accel drops back near 1g.
#define BURNOUT_ACCEL_THRESHOLD_G  1.3f

// Apogee detection: N consecutive falling altitude samples after burnout.
#define APOGEE_FALLING_SAMPLES        4
#define APOGEE_MIN_SAMPLE_INTERVAL_MS 50

// ---- FAILSAFE TIMERS (new in v11) ----
// If the barometric detection chain never closes, deploy anyway. These are
// the only thing standing between you and a ballistic recovery if the BMP180
// fails mid-flight.
//
// *** RE-TUNE APOGEE_TIMEOUT_MS BEFORE EVERY FLIGHT ***
// Set it from your OpenRocket burnout->apogee time PLUS margin. For the
// nominal 1000 m profile that is ~14 s, hence 20 s. If the Brazilian
// propellant gives a different apogee, this number MUST change with it.
// Too short = premature deployment at high speed = shredded parachute.
#define BURNOUT_TIMEOUT_MS        4000    // force COAST if BOOST persists this long
#define APOGEE_TIMEOUT_MS        20000    // measured from burnout, then force APOGEE
#define APOGEE_TIMEOUT_ENABLED    true

// Minimum stagger between drogue and main. Without this, any flight whose
// apogee is below MAIN_DEPLOY_ALT_AGL_M fires BOTH channels within one 20 ms
// loop tick, doubling peak current draw at the worst possible instant.
// FLIGHT072's 74 m apogee was exactly that case.
#define MAIN_MIN_DELAY_AFTER_DROGUE_MS 1500

// ---- MINIMUM ALTITUDE INTERLOCK (v12) ----
// The vehicle must actually have GAINED altitude before ANY pyro decision
// is permitted. This is NOT a "fire at altitude X" gate - that class of
// logic is what would have suppressed FLIGHT072 entirely. This is a
// minimum-altitude-EVER-REACHED interlock, evaluated against the peak AGL
// seen since liftoff.
//
// FLIGHT072 peaked at 74.08 m, so 30 m still deploys it with 2.4x margin.
// A 3 m pulley test does not. A bump on the walk to the pad does not.
//
// Trade accepted, stated plainly: a vehicle that fails below 30 m AGL gets
// no deployment. At that altitude a parachute does not change the outcome.
#define MIN_APOGEE_ALT_M          30.0f

// Ground testing: lowers BOTH the altitude interlock and the boost debounce
// so the pyro chain can be exercised on a pulley or in a vacuum chamber.
// *** MUST BE false FOR FLIGHT. Verified at boot - see the banner. ***
#define GROUND_TEST_MODE          false
#define GROUND_TEST_MIN_ALT_M      1.5f
#define GROUND_TEST_DEBOUNCE_MS     40

// ---- SINGLE-EVENT RECOVERY (v12) ----
// REC 8.1.2 exempts vehicles below 1500 m AGL from dual-event, so a single
// deployment at apogee is compliant at the 1000 m target.
//
// When true, PYRO_CH2 (main) is NEVER fired. That matters on this hardware:
// firing a channel into an open loop is itself a reset risk (unprotected
// gate opto - see the Pyro.h header and the FLIGHT072 mid-air reset), so an
// unused channel must be disabled in firmware, not just left unwired.
//
// Set to false ONLY when both channels are wired AND both read cont:OK.
#define SINGLE_EVENT_MODE         true

// Pyro fire pulse duration.
#define PYRO_FIRE_PULSE_MS   1000

// Force an SD flush on every row for this long after any pyro fire event.
// FLIGHT072: zero rows survived between the drogue fire at 388.116 s and the
// reset ~1.2 s later, because the next scheduled flush never happened.
#define PYRO_FORCE_FLUSH_MS  4000

// ---- ATTITUDE TRACKING - DIAGNOSTIC / TELEMETRY ONLY ----
// LASC's 15-degree attitude limit is a fail-safe requirement for missions
// with an Attitude Control System (ACS). This vehicle has no ACS, so that
// requirement does not apply. tilt_deg is recorded for post-flight analysis
// and for trajectory evidence at the Launch Operation Debriefing (FLT
// 4.2.11) ONLY. It is never read by any pyro decision path, and it must not
// be: an integrated-gyro estimate drifts badly (FLIGHT072 accumulated 538 deg
// of roll in 3.5 s), and a tumbling vehicle at apogee is precisely when you
// most need the parachute out. A tilt-based deployment inhibit would convert
// a recoverable abnormal trajectory into a ballistic entry.
#define TILT_WARN_DEG        15.0f
#define BOOST_AXIS_IS_X      true   // confirmed: accel_x = -0.999 g at rest on pad

// Landed detection: altitude change below this for LANDED_STABLE_MS.
// ---- BATTERY PRE-FLIGHT GO/NO-GO (v12) ----
// There was no battery threshold anywhere in this file before v12.
// battery_v was logged and transmitted, and nothing evaluated it.
// FLIGHT072 flew at 7.73 V (~45% SoC on a 2S pack) and the FC reset
// mid-flight during the pyro pulse. Whether those two facts are causally
// linked is still an open question - the pulley tests that did NOT reset
// ran at 8.10-8.12 V, so they do not settle it. Flying at half charge is
// not acceptable either way.
//
// ADVISORY ONLY. Like continuity, this never inhibits firing. VBAT_MON is
// a resistor divider into an ADC; a bad reading must not be able to cancel
// a good flight. The veto belongs to the crew and the ground station.
#define VBAT_NOGO_V            7.80f
#define VBAT_WARN_V            8.00f

#define LANDED_ALT_DELTA_M     2.0f
#define LANDED_STABLE_MS       5000

// ---- RTC-PERSISTED RESUME ACROSS AN IN-FLIGHT RESET (v13) ----
// See FlightData.h (RtcFlightState) and IGNIS_FC_heavy_13.ino's setup().
// Added after FLIGHT051/052 confirmed an ESP_RST_INT_WDT reset mid-COAST
// that forced the whole state machine to restart from STATE_ARMED,
// losing the descent as one continuous record.
#define RTC_RESUME_ENABLED        true

// ---- I2C HARDENING (v13) ----
// Bounds Wire so a stuck I2C transaction returns an error instead of
// blocking the loop indefinitely — the leading candidate for the
// confirmed ESP_RST_INT_WDT reset between FLIGHT051 and FLIGHT052.
// Requires an ESP32 Arduino core new enough to provide
// Wire::setTimeOut(); if your installed core predates it, upgrade the
// core rather than deleting the call in Sensors.h — this line is
// specifically the fix for a reset we have hard evidence for.
#define I2C_TIMEOUT_MS              50

// If BOTH the IMU and the barometer read invalid for this many
// consecutive sensor-loop ticks, the bus itself is probably wedged (not
// just one bad sample) — re-run the same bus-recovery sequence used at
// boot from inside the flight loop instead of hammering a stuck bus
// every 20 ms until the watchdog does it for us. See Sensors.h.
#define I2C_FAIL_RECOVERY_THRESHOLD  10

// ---- SD FLUSH DURING THE POST-PYRO FORENSIC WINDOW (v13) ----
// PYRO_FORCE_FLUSH_MS (above) used to mean "flush every single row" for
// the whole window — up to ~50 blocking SD writes/sec during exactly the
// highest-vibration part of the flight, itself a watchdog-reset
// candidate. Flushing on this bounded interval instead still keeps the
// worst-case data-loss window well under a second.
#define PYRO_WINDOW_FLUSH_INTERVAL_MS  100

// ---------------- Timing ----------------
#define SENSOR_LOOP_INTERVAL_MS     20    // ~50 Hz sensor/state loop
#define TELEMETRY_INTERVAL_MS_PAD  1000    // 1 Hz on the pad — comfortably above ToA (~530ms), no change needed
// Was 200ms/5Hz — physically impossible for this packet size (~98 bytes)
// at SF9/BW125/CR4-5: measured time-on-air is ~530ms, not the "tens of
// ms" an earlier comment assumed. See Telemetry.h header for the math.
// 600ms is a starting point (~1.6Hz attempt rate, close to what ToA
// alone allows) — sendTelemetry() is non-blocking and skips a cycle if
// the previous send hasn't finished, so this is an upper bound on
// attempts, not a guarantee. Lower SF (shorter range) or a smaller
// packet would allow a faster rate if you want to trade for that.
#define TELEMETRY_INTERVAL_MS_FLT   600
#define LOG_FLUSH_INTERVAL_MS      1000
#define DASHBOARD_INTERVAL_MS       500    // serial status block refresh rate
#define SD_MOUNT_RETRY_INTERVAL_MS 10000   // retry SD mount on this timer if not currently logging

// Continuous status tune — mirrors the RRC3's own "I'm alive and
// actually running, not just powered" heartbeat, but plays CONTINUOUSLY
// (loops back-to-back, no dead silence) rather than beeping once every
// few seconds — needed because when the FC is buried inside the
// airframe, this is the only way to verify it's alive other than RF
// telemetry. Only active while PAD_IDLE/ARMED (see StatusIndicator.h)
// — silent once flying. Tunes (STANDBY_MELODY / ARMED_MELODY, arrays
// of {freq, duration} notes) are defined in StatusIndicator.h, not
// here — no timer constant needed here anymore since there's no gap
// between loops to configure.
