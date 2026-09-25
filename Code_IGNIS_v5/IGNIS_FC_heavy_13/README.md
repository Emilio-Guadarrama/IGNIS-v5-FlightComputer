# IGNIS v2 Flight Computer — firmware v13

Ignitia Rocket Lab · Tecnológico de Monterrey, Campus Guadalajara
LASC 2026 · Rocket Challenge, Mission ID 22

Post-FLIGHT051 / FLIGHT052 revision. FLIGHT052 is the flight where the FC
reset mid-descent and lost the entire post-apogee trace that the RRC3 backup
altimeter still has — this revision's centerpiece is a resume mechanism so
that doesn't happen again, plus the pyro-circuit correction the netlists
turned up. Every change is traced to a number in the flight logs or to the
schematic/netlists. Read `docs/CHANGELOG.md` first (v13 section is at the
top).

```
IGNIS_FC_heavy_13/
├── IGNIS_FC_heavy_13.ino    main sketch (boot counter, NO-GO tone + banner, RTC resume)
├── Config.h                 thresholds, interlocks, mode flags, pin map, RTC/I2C/flush tuning
├── FlightData.h             shared struct + lastPyroEventMs + RtcFlightState (v13 resume struct)
├── FlightState.h            FSM, baro velocity, tilt, failsafes, interlock, resume/sync (v13)
├── Pyro.h                   gate drive limiting, fire-event timestamp, RTC fired-flag persistence (v13)
├── DataLogger.h             post-pyro bounded flush, forensic CSV columns + `resumed` (v13), resume-reopen
├── Telemetry.h              98-byte v11/v12 packet — UNCHANGED this revision, see note below
├── Sensors.h                readBattery() extracted for the boot NO-GO, I2C timeout + runtime recovery (v13)
├── StatusIndicator.h        NOGO_MELODY, preflightNoGo(), persistent LED2
├── Dashboard.h              MODE / FSM / BOOT rows, NO-GO warnings (no longer cites a past flight — see CHANGELOG)
├── docs/
│   ├── CHANGELOG.md              <- read this first (v13, then v12, then v11)
│   ├── PREFLIGHT_VALIDATION.md   <- REC 8.4.2 test plan
│   ├── LIBRARIES.md              versions, UART jig, compile flags
│   └── install_libraries.sh
└── groundstation/
    ├── GroundStation_TelemetryPacket_v12.h   <- still named v12: packet layout is unchanged, see note below
    └── telemetry_parser_v12.py
```

**Groundstation files are still named `_v12` on purpose.** v13 only adds a
column to the SD CSV (`resumed`) — it does not touch `TelemetryPacket`, the
radio packet struct. Renaming files that carry no actual change would just
make history harder to follow; rename them the revision the packet itself
changes.

## Flight-configuration flags — CHECK THESE EVERY TIME

| Flag | Flight value | Effect if wrong |
|---|---|---|
| `GROUND_TEST_MODE` | **false** | If true: altitude interlock drops to 1.5 m — a bump can fire the drogue on the ground |
| `SINGLE_EVENT_MODE` | `true` unless BOTH channels read `cont:OK` | If false with an unwired channel: fires into an open loop, which is a reset risk |
| `APOGEE_TIMEOUT_MS` | Burnout→apogee time + margin | Too short: deployment at high speed, shredded parachute |
| `MIN_APOGEE_ALT_M` | 30 m | Too high: no deployment on an underperforming flight |
| `RTC_RESUME_ENABLED` (new, v13) | **true** | If false: an in-flight reset goes back to the FLIGHT051/052 behavior — reboots cold into `STATE_ARMED` and loses the rest of the flight |
| `I2C_TIMEOUT_MS` / `I2C_FAIL_RECOVERY_THRESHOLD` (new, v13) | 50 ms / 10 | Too low: spurious bus-recovery resets on a momentarily slow but otherwise fine bus |
| `PYRO_WINDOW_FLUSH_INTERVAL_MS` (new, v13) | 100 ms | Too low: back to near-every-row blocking SD flushes right after a fire, one of this revision's own watchdog-reset suspects |

The boot banner prints all of these (plus `last_op` and, on a resume boot, a
`RESUME:` banner line). Read it.

## Before you flash anything

1. **This has not been compiled.** Brace and parenthesis balance was verified
   programmatically on every file; that is not a clean build. Compile first
   and fix trivial errors rather than assuming a patch is wrong.
2. **Verify `Wire.setTimeOut()` against your installed ESP32 Arduino core
   version.** It's a real API on recent cores; confirm it before flashing
   rather than discovering a compile error on the pad.
3. **Bench-test the RTC resume path itself, in `GROUND_TEST_MODE`.** Force a
   reset mid-flight-simulation (e.g. power-cycle or trigger the watchdog
   deliberately) and confirm the FC comes back in the correct phase, with
   `resumed=1` in the next CSV rows and the same log file continuing rather
   than a new one. This is the core new mechanism in v13 and it has not been
   flight- or bench-verified yet.
4. **Determine why `pyro1_cont` reads 0.** Still open — this needs a
   physical continuity check on the CH1 e-match loop, it is not something
   firmware can diagnose or fix.
5. ~~Fit the 330 Ω gate resistors.~~ **Resolved — not actually missing.**
   The netlists (`Pyros.NET`/`Pyros2.NET`) confirm `R34` (100 Ω) is present
   in series with the fire-side opto LED, and ignition current is switched
   by an isolated MOSFET (`Q5`), not the opto. See `docs/CHANGELOG.md`
   v13 "Closed from v12."
6. **Re-tune `APOGEE_TIMEOUT_MS`** to the predicted profile. It is a
   deployment trigger, not a comment.
7. **Update the ground station in the same commit, if you change the radio
   packet.** Not needed for v13 itself (see the groundstation note above),
   but the packet is 98 bytes and the struct is packed — a mismatch decodes
   silently into garbage the moment it does change.

## The two sentences that matter

FLIGHT051→FLIGHT052: the flight computer reset mid-coast — confirmed
`reset_reason=5` (interrupt watchdog), battery steady at ~8.0 V throughout,
before any pyro command existed — then rebooted cold into `STATE_ARMED` and
detected the entire rest of the flight (apogee, descent, landing) as if it
were a new, stationary boot. The RRC3 backup altimeter has the full descent
trace; the FC's own SD log does not. v13's RTC resume mechanism exists
specifically so a reset like this keeps the flight computer *in* the flight
instead of restarting it.

FLIGHT018 (carried forward from v12, still true): the flight software
commanded a drogue fire at 0.80 m *below* the launch reference, off a hand
jerk on a 3-metre pulley. The v12 altitude interlock exists so that cannot
happen again, and v13 does not change it.
