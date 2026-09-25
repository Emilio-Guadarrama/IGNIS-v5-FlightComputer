# IGNIS v2 Flight Computer — firmware v13

**Date:** 2026-09-22
**Baseline:** `IGNIS_FC_heavy_12`
**Trigger:** FLIGHT051 / FLIGHT052 — the FC reset mid-flight and lost the
entire descent trace that the RRC3 backup altimeter still has; a follow-up
pyro-circuit review with E. Guadarrama (schematic image + `Pyros.NET` /
`Pyros2.NET` netlists + `Pyros.pdf`) plus direct analysis of the
`reset_reason` column in both CSVs.

---

## What FLIGHT051/FLIGHT052 proved

Both logs were pulled apart directly (`reset_reason`, `boot_count`,
`battery_v`, `state`, `pyro1_fired`, `timestamp_ms`).

1. **The reset is confirmed `reset_reason = 5` (`ESP_RST_INT_WDT`, the
   interrupt watchdog), not brownout.** `battery_v` sits at a steady
   8.00–8.06 V across the entire window — there is no voltage sag to
   correlate with the reset. The brownout hypothesis is closed for this
   specific event.
2. **It happened mid-`STATE_COAST`, before any pyro fire command existed
   in either log.** That rules out the gate-drive-overcurrent theory *for
   this reset* — nothing had fired yet — and it means the FC rebooted,
   re-ran full cold-start init (including MPU calibration and SD
   remount), and came back up in `STATE_ARMED` with zero memory of ever
   having flown. Apogee and the entire descent were then detected and
   logged as if this were a brand-new, stationary boot — which is why the
   RRC3 backup has a full descent trace and the FC does not.
3. **The pyro circuit itself is not the flaw it was thought to be.** The
   netlists confirm `R34` (100 Ω) *is* present in series with the
   fire-side opto LED, and ignition current does not flow through the
   opto at all — it's switched by an isolated N-MOSFET (`Q5`,
   NCE6005AR, 60 V / 5 A) driven through a push-pull BJT level-shifter
   (`Q4`/`Q6`). The "no series resistor on the gate opto leg" theory in
   `Pyro.h`'s v11/v12 comments was based on a misreading of the code
   comments against the schematic, not the actual board. **Walking that
   theory back** — see "Closed from v12" below.

What v13 does NOT resolve: what caused the *second* reset, ~434 ms after
`STATE_ARMED` re-fires the pyro command near the end of FLIGHT052.
`battery_v` at that point has not been isolated from the ignition current
step, so a VBAT-rail-sag-from-ignition cause is still the leading
hypothesis for that one — untested on the bench as of this revision. See
`FLIGHT051-052.md` analysis notes.

---

## Changes

### 1. RTC-persisted flight-state resume across an in-flight reset — `FlightData.h`, `Config.h`, `FlightState.h`, `.ino`

The actual fix for the FLIGHT051/052 finding above. `RTC_DATA_ATTR`
slow-memory (survives SW/panic/watchdog resets, not necessarily a full
brownout power-cycle) now holds a `RtcFlightState` breadcrumb: flight
phase, both pyro `_fired` flags, `maxAglSinceBoost`, ground-pressure
calibration, MPU bias, and the active log filename, tagged with a magic
number so a genuinely fresh cold boot is never mistaken for a resume.

`setup()` now checks `reset_reason != POWERON && rtcState.magic valid &&
phase >= BOOST` and, if true, skips straight to `resumeFlightState()`
instead of the cold-start `STATE_ARMED` path — the FC comes back up
already in the flight phase it was in, not stationary-armed, so descent
detection and pyro logic keep running instead of re-arming from scratch.

Deliberately **not** attempting to reconstruct absolute elapsed
flight time across the reset — `millis()` resets to 0 on every boot, and
back-computing a fake absolute timestamp is a worse failure mode than
simply re-arming each phase's own timers fresh on resume. This is a
conservative simplification, not an oversight: every timer that matters
(coast/drogue/main timeouts) re-starts cleanly from the resume moment.

### 2. Pyro fired-flag persistence — `Pyro.h`, `FlightData.h`, `.ino`

`d.pyro1_fired` / `d.pyro2_fired` are now part of the RTC breadcrumb and
are restored in `setup()` before the FSM resumes. Before v13 this latch
lived only in RAM — an in-flight reset after a real fire could let the
FSM call `fireChannel()` again on the next boot. That is not just a
logging gap: a channel that already fired has a burned-open bridgewire,
so re-asserting the gate drives `Q5` into a guaranteed-open loop — the
same electrical signature already implicated as a reset candidate
elsewhere in this project's history. Closed.

### 3. I2C bus hardening — `Sensors.h`

`Wire.setTimeOut(I2C_TIMEOUT_MS)` (50 ms) added so a stuck I2C bus can no
longer hang the sensor read indefinitely — a plausible contributor to an
interrupt-watchdog trip. Runtime bus recovery (previously only ever
invoked once, at boot) is now re-invoked from the flight loop itself
after `I2C_FAIL_RECOVERY_THRESHOLD` (10) consecutive failed reads, so a
bus that wedges mid-flight has a chance to recover without waiting for
the next reboot.

### 4. SD: reopen the existing log file on resume — `DataLogger.h`

`attemptSDMount()` takes an `isResume` flag. On a genuine resume with a
valid RTC-remembered filename, it reopens that **same** `/FLIGHTxxx.CSV`
(Arduino `SD`'s `FILE_WRITE` opens at EOF — append, not truncate) instead
of allocating a new one. This is what turns a pre-reset + post-reset
flight back into one continuous file instead of the FLIGHT051 /
FLIGHT052 split this revision exists to fix.

### 5. Bounded post-pyro flush interval — `Config.h`, `DataLogger.h`

The v11 "flush every row for `PYRO_FORCE_FLUSH_MS` after any fire event"
behavior is itself a watchdog-reset candidate — up to ~50 blocking SD
writes/second during the highest-vibration part of the flight. v13 keeps
the same forensic intent (bound worst-case data loss right after a fire
to well under a second) but flushes on a bounded
`PYRO_WINDOW_FLUSH_INTERVAL_MS` (100 ms) interval instead of every row,
far fewer blocking SD ops for effectively the same guarantee.

### 6. RTC "last operation" breadcrumb — `FlightData.h`, threaded through `Sensors.h` / `DataLogger.h` / `Pyro.h` / `Telemetry.h`

`rtcSetLastOp()` brackets I2C reads, SD mount, SD write, pyro fire, and
radio send with an `OP_*` marker persisted in the same RTC struct. If a
future reset happens mid-operation, the very next boot's banner prints
what the FC was doing when it went down (`last_op=...`) — this is the
single biggest lever for diagnosing *which* operation trips the
interrupt watchdog next time, since FLIGHT051/052 alone couldn't localize
it beyond "sometime in `STATE_COAST`."

### 7. New `resumed` CSV column — `DataLogger.h`

`resumed_this_boot` (1 if this boot restored flight state from RTC, 0 on
a genuine cold start) is appended to every log row. Lets post-flight
analysis immediately see where a resume happened in the row stream,
instead of inferring it from a `boot_count` jump.

### 8. Dashboard live output no longer names a past flight — `Dashboard.h`

The two `Serial.println` lines citing FLIGHT072 specifics
("`*** FLIGHT072 flew with CH1 OPEN`", "`*** FLIGHT072 flew at 7.73 V`")
are removed from `printDashboard()`'s live NO-GO block — a recurring
serial print is the wrong place for a one-off historical citation once
the underlying threshold check (open continuity / low VBAT) is doing the
actual work. The same FLIGHT072 references are **kept** in the one-time
`setup()` boot banner, unchanged — that context is worth remembering, just
not re-printing every dashboard cycle in flight.

---

## Closed from v12

**"Missing series resistor on the pyro gate opto leg" — was wrong for
this board.** The `Pyros.NET` / `Pyros2.NET` netlists (plus the
schematic image and `Pyros.pdf`) confirm `R34` = 100 Ω is present in
series with the fire-side opto LED, and that ignition current is
switched by an isolated N-MOSFET (`Q5`, NCE6005AR), not the opto itself.
`PYRO_GATE_REDUCED_DRIVE` and the "fit a 330 Ω resistor" note in `Pyro.h`
and prior changelogs described a hardware flaw that does not exist on the
as-built board. Left the code comments in place with a correction rather
than deleting the history — see `Pyro.h`'s header.

## Confirmed unrelated to this revision (no change needed)

- **Booting straight to `STATE_ARMED` from power-on is correct as-is.**
  The vehicle powers up already armed via the Remove-Before-Flight pin, by
  design and by rule (no additional action should be required to start
  the flight once the pin is pulled). This is unrelated to the resume
  fix above and was not changed.
- **Telemetry/logging were never state-gated to begin with.** `loop()`
  calls `logFlightData()` / `sendTelemetry()` unconditionally regardless
  of `d.state`, so GPS and full sensor data already keep transmitting
  through descent and landing on a boot that never reset. The actual gap
  FLIGHT051/052 exposed was the FSM not *reaching* the descent states
  after a reset in the first place — which is what item 1 above fixes.

---

## Still open after v13

| Item | Owner |
|---|---|
| ~~330 Ω series resistor on both pyro gate legs~~ — netlist-confirmed not needed; R34 (100 Ω) already present, MOSFET-switched | closed, see above |
| Determine why `pyro1_cont` reads 0 across FLIGHT051/052 (CH1 wiring) | hardware, physical inspection |
| Second FLIGHT052 reset (~434 ms after the late-flight pyro fire) — VBAT-rail-sag-under-ignition-load hypothesis, untested | test — bench-fire on the actual flight battery with high-rate voltage logging |
| `rf95.send()` has no audited internal timeout; shares the SD's SPI bus and is a plausible watchdog-reset candidate if a marginal radio module ever stalls | firmware, flagged not fixed — see `Telemetry.h` |
| Verify `Wire.setTimeOut()` compiles/behaves as expected against the installed ESP32 Arduino core version | test, before flashing v13 |
| Bench-test the RTC resume path itself: force a reset mid-`GROUND_TEST_MODE` run and confirm the FC comes back in the correct phase with `resumed=1` in the log | test, before flight |
| MPU6050 AD0 tied to GND | hardware |
| BMP180 CSB/VDDIO → VCC_3V3, SDO → GND | hardware, next PCB rev |
| `tilt_deg` is misnamed — it accumulates angular travel, never decreases | firmware |
| 4-hour rail standby (ECS 9.1.3) — light sleep, GPS backup mode | firmware |
| RRC3 backup "Drogue Delay" — recommend lowering from 3 s to 1 s (RRC3's minimum) against the 30 m/s drogue rating; not a firmware change, configured on the RRC3 itself | hardware/ops, see project's RRC3 doc |

---

---

# IGNIS v2 Flight Computer — firmware v12

**Date:** 2026-08-15
**Baseline:** `IGNIS_FC_heavy_11`
**Trigger:** FLIGHT016 / FLIGHT018 pulley tests + a correction from E. Guadarrama on battery state of charge

---

## What the pulley tests proved

Two 3-metre hand-pulley lifts against the avionics tower. Both ran the full
state machine `1 → 2 → 3 → 5 → 6 → 7` with `deploy_trigger = 1`.

**Three things worked, and they are worth recording:**

1. **CH2 fired a real e-match, with electrical proof.** `pyro2_cont` dropped
   from 1 to 0 exactly 24 ms after the fire command in both tests — the
   bridgewire opening. The channel switches real load and the CH2 continuity
   sense is functional. (CH1 remains stuck at 0.)
2. **No reset during firing.** `boot_count` held at 46 and 49 throughout, with
   456 and 587 rows logged *after* the fire. The v11 post-pyro forced flush
   works; there is no forensic hole any more.
3. **The stagger held exactly.** 1.524 s and 1.500 s measured against the
   1500 ms configured.

**And one thing failed badly.**

| | Peak AGL | BOOST detected | AGL at drogue fire |
|---|---|---|---|
| FLIGHT016 | 4.47 m @ t=95.04 | t=95.88 (0.84 s **after** peak) | +1.02 m |
| FLIGHT018 | 2.52 m @ t=21.54 | t=23.05 (1.51 s **after** peak) | **−0.80 m** |

In both tests BOOST triggered *after* the high point — the accelerometer fired
on the braking jerk of the descent, not on the lift. FLIGHT018 then commanded
the drogue **0.80 m below the launch reference**.

Operationally: an armed rocket carried to the pad and bumped could fire the
drogue in someone's hands.

Measured pre-BOOST handling noise peaked at **1.931 g** (F016) and **2.169 g**
(F018) — above the v11 threshold of 1.8 g. That threshold was calibrated
against FLIGHT072 pad data with the vehicle sitting still on a rail
(max 1.543 g over 380 s), which is not the same scenario as handling. **The
v11 threshold recommendation was wrong for that reason.**

---

## Changes

### 1. Boost debounce: 40 → 250 ms — `Config.h`

Level cannot discriminate a bump from a burn. **Duration can.**

Longest continuous run above 1.8 g:

| Log | Sustained |
|---|---|
| FLIGHT072 (real KNSU motor) | **2111 ms** |
| FLIGHT016 (pulley jerk) | 80 ms |
| FLIGHT018 (pulley jerk) | 93 ms |

Two orders of magnitude apart. 250 ms rejects handling transients with ~2.7×
margin and accepts a real burn with ~8× margin.

### 2. Minimum-altitude interlock — `Config.h`, `FlightState.h`

`MIN_APOGEE_ALT_M = 30.0f`. No pyro decision of any kind — neither barometric
apogee nor the timeout failsafe — is permitted until peak AGL since liftoff
has exceeded this.

This is **not** a "fire at altitude X" gate. That class of logic is what would
have suppressed FLIGHT072 entirely. This is a minimum-altitude-*ever-reached*
interlock evaluated against `maxAglSinceBoost`. FLIGHT072 peaked at 74.08 m,
so 30 m still deploys it with 2.4× margin.

Trade accepted, stated plainly: a vehicle that fails below 30 m AGL gets no
deployment. At that altitude a parachute does not change the outcome.

### 3. `GROUND_TEST_MODE` — `Config.h`, `FlightState.h`, `.ino`

Lowers the interlock to 1.5 m and the debounce to 40 ms so the pyro chain can
still be exercised on a pulley. Prints an unmissable boot banner when on.

**Must be `false` for flight.** With it off, pulley tests will no longer fire —
that is the correct consequence. The pulley validates the electrical chain;
the vacuum chamber validates flight logic.

### 4. `SINGLE_EVENT_MODE` — `Config.h`, `FlightState.h`

When true, `PYRO_CH2` is never fired. REC 8.1.2 exempts vehicles below 1500 m
AGL from dual-event, so single deployment at apogee is compliant at the 1000 m
target.

This matters beyond simplification: firing a channel into an open loop is
itself a reset risk on this hardware (unprotected gate opto — see the `Pyro.h`
header and the FLIGHT072 mid-air reset). **An unused channel must be disabled
in firmware, not merely left unwired.**

Set to `false` only when both channels are wired and both read `cont:OK`.

Cost of single-event at 1000 m: descent under the main from apogee at 7.5 m/s
is ~133 s, which is roughly 1.3 km of drift at the 10 m/s launch wind limit
(FLT 4.3.3). That is a team decision, not a firmware one.

### 5. Battery NO-GO — `Config.h`, `Sensors.h`, `.ino`, `StatusIndicator.h`, `Dashboard.h`

There was no battery threshold anywhere in the codebase before v12.
`battery_v` was logged and transmitted, and nothing evaluated it.

`VBAT_NOGO_V = 7.80`, `VBAT_WARN_V = 8.00`.

`readBattery()` extracted from `readSensors()` in `Sensors.h` so `setup()` can
evaluate it before the first sensor loop.

Critically, the battery is folded into the **persistent** indicators, not just
the boot tone: `preflightNoGo()` now drives the LED2 blink and the dashboard
status text. A one-shot boot tone is easy to miss if the board powers up while
being handled or already inside the airframe.

**Advisory only.** Like continuity, this never inhibits firing — `VBAT_MON` is
a resistor divider into an ADC, and a bad reading must not be able to cancel a
good flight. The veto belongs to the crew and the ground station.

### 6. `reset_reason` decode corrected — `Dashboard.h`, `groundstation/*`

The v11 tables mapped **6** to BROWNOUT. In `esp_reset_reason_t`, 6 is
`ESP_RST_TASK_WDT` and BROWNOUT is **9**. The `.ino` was always correct
because it used the named constants; the display paths were not.

Full table now decoded in all three places (dashboard, GS header, Python
parser), and the parser distinguishes crash reasons (4, 5, 6, 7, 15) from
brownout (9) — they point at different root causes.

### 7. Ground station alarm thresholds — `groundstation/telemetry_parser_v12.py`

Raised to match `Config.h`. The old 7.0 V warning would have shown FLIGHT072
(7.73 V) as green.

Verified against FLIGHT072's exact conditions — CH1 open, VBAT 7.73 V, reset
reason 4 — the parser now raises all three alarms.

---

## An open question this version does NOT close

The FLIGHT072 mid-air reset is still unexplained.

The v11 changelog implied the pulley tests ruled out a power-related cause.
**They did not.** Those tests ran at 8.10–8.12 V; FLIGHT072 flew at 7.73 V
(~45% SoC). That is an uncontrolled variable, not a negative result.

Three candidate mechanisms remain:

| Hypothesis | Expected `reset_reason` |
|---|---|
| A. GPIO overload from the unprotected gate opto (~80 mA into a 40 mA pin) | 4 (PANIC) or 15 (CPU_LOCKUP) |
| B. Genuine brownout | **9** |
| C. Firmware crash in the fire path, independent of battery | 4, 5, 6 or 7 |

Note that a pure VBAT-sag mechanism does not survive arithmetic: reaching the
AMS1117-5.0 dropout from 7.73 V needs ~1.5 V of droop, which at a 2S pack's
60–100 mΩ would require 15–25 A. FLIGHT072's CH1 was **open** — no pyro
current flowed at all. The only load step was the gate opto, ~80 mA, worth
about 8 mV. Something else is going on.

**The deciding test:** discharge a pack to 7.73 V — matching, not
approximating — and repeat the pulley sequence five times with
`GROUND_TEST_MODE = true` and a real e-match. From v11 onward the FC records
`reset_reason` itself, so the next reset will name its own cause.

---

## Still open after v12

| Item | Owner |
|---|---|
| 330 Ω series resistor on both pyro gate legs | hardware |
| Move the e-match to CH1 (currently on CH2) | hardware |
| MPU6050 AD0 tied to GND | hardware |
| BMP180 CSB/VDDIO → VCC_3V3, SDO → GND | hardware, next PCB rev |
| `tilt_deg` is misnamed — it accumulates angular travel, never decreases. Rename to `angular_travel_deg` or implement true quaternion tilt | firmware |
| Repeat the pulley test at 7.73 V to close the reset question | test |
| 30-minute idle soak to check whether `boot_count` self-increments | test |
| 4-hour rail standby (ECS 9.1.3) | firmware |
| Pad antenna geometry — RSSI −123.6 dBm is below the SF9 sensitivity floor | ground ops |

---

---

# IGNIS v2 Flight Computer — firmware v11

**Date:** 2026-08-15
**Baseline:** `IGNIS_FC_heavy_10`
**Trigger:** FLIGHT072 / FLIGHT073 post-mortem (Ignitia Rocket Lab, Mission ID 22)
**Author of changes:** avionics review, pending bench validation by E. Guadarrama

> Every change below is traced to a specific number in the FLIGHT072 SD log,
> the FLIGHT073 SD log, or GSLOG003. Nothing here is a guess. Where a value
> still needs to be tuned by the team, it is marked **TUNE**.

---

## What actually happened on FLIGHT072

| Event | FC clock | AGL |
|---|---|---|
| Armed | 2.4 s | 0 m |
| Liftoff detected (STATE_BOOST) | 384.63 s | 4.42 m |
| Burnout (STATE_COAST) | 386.39 s | 48.1 m |
| Apogee (true) | 387.93 s | **74.08 m** |
| Drogue fire commanded (STATE_DROGUE_DESCENT) | 388.12 s | 72.2 m |
| **FC reboot** | ~388.1–389.3 s | ~65 m |
| Ground impact (computed) | ~391.8 s | 0 m |

**The flight software worked.** Apogee was detected 188 ms after the true
peak — better than the 4-sample / 50 ms specification implies. The failure
was not in the detection logic.

**Three findings drove this revision:**

1. **CH1 (drogue) had no continuity — ever.** `pyro1_cont = 0` across all
   10,226 rows of FLIGHT072 and all 12,970 rows of FLIGHT073. `pyro2_cont = 1`
   across both. Per the polarity documented in `Pyro.h` (opto U7/PC817 pulls
   the sense line HIGH through VCC_3V3 when the loop is intact; R19 pulls it
   LOW when open), `cont = 0` means **open**. The FC commanded a fire into an
   open channel. The boot banner printed `CH1(drogue): OPEN` before the
   flight and nobody was looking at a serial console.

2. **The FC rebooted in mid-air, not on impact.** Reconstructed from GSLOG003:
   the last in-flight packet was `seq 377 / fc_t 387648`; the next packet was
   `seq 0 / fc_uptime 2084`. Free fall from 74 m takes 3.88 s, placing impact
   at ~391.8 s. The reset happened 2.5–3.7 s earlier, roughly 65 m above
   ground, within ~1.2 s of the pyro gate going HIGH. **Because CH1 was open,
   no pyro current flowed** — which rules out VBAT sag and points at the
   unprotected gate opto documented in `Pyro.h`'s own header.

3. **The liftoff threshold had 0.22 g of margin.** `BOOST_ACCEL_THRESHOLD_G`
   was 3.0 g; peak `accel_total_g` was 3.222 g. A motor 7 % weaker would have
   left the FSM in `STATE_ARMED` for the entire flight.

---

## Changes

### 1. Liftoff threshold: 3.0 g → 1.8 g — `Config.h`

Justified against 380 s of measured pad data from FLIGHT072:

| Metric (STATE_ARMED, t < 383.5 s) | Value |
|---|---|
| mean `accel_total_g` | 1.0001 g |
| p99.9 | 1.148 g |
| absolute max | 1.543 g |
| samples above 1.8 g | **0** |

Margin goes from 0.22 g to 1.42 g. Debounce raised 30 → 40 ms (2 loop ticks).

### 2. Barometric liftoff backup — `Config.h`, `FlightState.h`

New OR-path into `STATE_BOOST`: sustained baro climb rate ≥ 15.0 m/s for
≥ 150 ms. Survives a dead IMU.

FLIGHT073 proved this case is real, not hypothetical: after impact the
MPU6050 stopped validating (`imu_valid = 0` across all 12,970 rows) while the
BMP180 on the same I2C bus kept working. Threshold sits above the measured
pad climb-rate noise peak of 9.88 m/s.

The accel path now also requires `imu_reading_valid` — previously a stale
`accel_total_g` from a failed read could in principle satisfy the threshold.

### 3. Failsafe timeouts — `Config.h`, `FlightState.h`

| Constant | Value | Purpose |
|---|---|---|
| `BURNOUT_TIMEOUT_MS` | 4000 | force COAST if BOOST persists |
| `APOGEE_TIMEOUT_MS` | 20000 | **TUNE** — force APOGEE if the baro chain never closes |

`APOGEE_TIMEOUT_MS` is measured from burnout, not from liftoff. 20 s suits the
nominal 1000 m profile (~14 s burnout-to-apogee, plus margin).

> **TUNE THIS BEFORE EVERY FLIGHT.** Set it from the OpenRocket
> burnout-to-apogee time plus margin. Too short means a deployment at high
> speed and a shredded parachute. If the Brazilian propellant gives a
> different apogee, this number changes with it.

`deploy_trigger` records which path fired: `1` = barometric apogee,
`2` = timeout failsafe. If a flight comes back with `2`, the barometer chain
needs investigation regardless of whether recovery succeeded.

### 4. Drogue → main stagger — `Config.h`, `FlightState.h`

`MAIN_MIN_DELAY_AFTER_DROGUE_MS = 1500`.

Previously, any flight whose apogee was below `MAIN_DEPLOY_ALT_AGL_M` fired
both channels within one 20 ms loop tick (documented in the old `Pyro.h`
header as expected behaviour). FLIGHT072's 74 m apogee was exactly that case.
Simultaneous firing doubles peak current draw at the worst possible instant.

### 5. Post-pyro forced SD flush — `Config.h`, `FlightData.h`, `DataLogger.h`

Zero rows survived between the drogue fire (388.116 s) and the reset ~1.2 s
later. At a 20–40 ms loop that is roughly 40 lost rows — exactly the window
with diagnostic value. The state-change `forceFlush` committed the fire row
itself; the next scheduled flush (`LOG_FLUSH_INTERVAL_MS = 1000`) never
arrived before power was lost.

Every row within `PYRO_FORCE_FLUSH_MS` (4000 ms) of any fire event is now
committed immediately. `lastPyroEventMs` is declared in `FlightData.h`, not
`Pyro.h`, because `DataLogger.h` is included *before* `Pyro.h` in the sketch.

### 6. Boot counter and reset reason — `.ino`, `FlightData.h`, `DataLogger.h`, `Telemetry.h`

`boot_count` persists in NVS (`Preferences`, namespace `ignis`, key `boots`)
and is written into every SD row and telemetry packet, alongside
`esp_reset_reason()`.

A mid-flight reboot is now visible directly in the flight log instead of
requiring cross-reference against a ground station packet sequence counter.
`reset_reason = 6` is `ESP_RST_BROWNOUT`.

### 7. Audible NO-GO on open continuity — `StatusIndicator.h`, `.ino`, `Dashboard.h`

New `NOGO_MELODY` (low, slow, descending) and `playNoGoTone()`. Plays three
times at boot with all LEDs red if either channel reads open, after
`initStatusIndicator()` has brought up the buzzer PWM channel and the WS2812
strip.

The FLIGHT072 information existed on the serial console. Audio does not
require a laptop.

### 8. Gate drive-strength mitigation — `Pyro.h`

`gpio_set_drive_capability(..., GPIO_DRIVE_CAP_1)` (~10 mA) on both pyro gate
pins, guarded by `PYRO_GATE_REDUCED_DRIVE`.

**This is not the fix.** The fix is a 330 Ω series resistor on each gate leg,
per the hardware flag already documented in `Pyro.h`'s header (the PC817 LED
is driven with no series resistor; an ESP32-S3 GPIO into that load can pull
~80 mA through a ~25 Ω driver).

Under-driving an opto can also make it **fail to switch**, which is the more
dangerous failure direction. If you enable this you must verify on the bench
that a real e-match fires 5 times out of 5 on the flight battery. If that test
does not pass cleanly, set `PYRO_GATE_REDUCED_DRIVE` to `false` and do the
resistor bodge instead.

### 9. Tilt estimate — logged only — `Config.h`, `FlightState.h`

Integrated off-boost-axis gyro rate, zeroed until `STATE_BOOST`.

**It never gates a pyro decision, and it must not.** LASC's 15° attitude limit
is a fail-safe requirement for missions with an Attitude Control System; this
vehicle has no ACS, so it does not apply. An integrated-gyro estimate drifts
badly — FLIGHT072 accumulated 538° of roll in 3.5 s — and a tumbling vehicle
at apogee is precisely when the parachute is most needed. A tilt-based
deployment inhibit would convert a recoverable abnormal trajectory into a
ballistic entry.

The field exists for post-flight analysis and for trajectory evidence at the
Launch Operation Debriefing (FLT 4.2.11).

---

## Telemetry packet — BREAKING CHANGE

Four fields appended at the end of `TelemetryPacket`.

| Version | Payload | ToA @ SF9 / BW125 / CR4-5 |
|---|---|---|
| v10 | 90 B | 545.8 ms |
| **v11** | **98 B** | **566.3 ms** |

`TELEMETRY_INTERVAL_MS_FLT` is 600 ms, so v11 still fits — but the margin is
34 ms. If you need it back, drop `mpu_temp_c` from the packet (it stays in the
SD log regardless).

> Note: the v10 header comment estimated ~98 B for the v10 packet. The
> measured `struct.calcsize` value is 90 B. The ToA figures above are computed
> from Semtech AN1200.22 with explicit header, 8-symbol preamble, CRC on.

**You must update `IGNIS_GroundStation.ino` and the Python parser in the same
commit.** See `groundstation/`. The struct is `__attribute__((packed))`; any
misalignment decodes silently into garbage rather than failing loudly.

---

## Not changed, deliberately

- **Apogee detection** (`APOGEE_FALLING_SAMPLES = 4`, `APOGEE_MIN_SAMPLE_INTERVAL_MS = 50`).
  It delivered 188 ms latency on FLIGHT072. Leave it alone.
- **Single vs dual event.** REC 8.1.2 exempts vehicles below 1500 m AGL from
  dual-event, so a single deployment is permitted at the 1000 m target. The
  code remains dual-event with the Tender Descender. This is a team decision,
  not a firmware one.
- **`MAIN_DEPLOY_ALT_AGL_M = 450`.** Compliant with REC 8.1.4 (≤ 500 m AGL).
- **RSSI/SNR on the FC side.** Still left at 0; the radio only transmits.

---

## Open items this firmware does NOT fix

| Item | Owner |
|---|---|
| 330 Ω series resistor on both pyro gate legs | hardware |
| Determine which channel the e-match is physically wired to | hardware |
| MPU6050 AD0 tied to GND (floating; suspected FLIGHT073 IMU loss) | hardware |
| BMP180 CSB/VDDIO → VCC_3V3, SDO → GND | hardware, next PCB rev |
| 40.2 % telemetry packet loss; pad RSSI −123.6 dBm, SNR −12.9 dB | ground ops |
| 4-hour rail standby (ECS 9.1.3) — light sleep, GPS backup mode | firmware, not in v11 |
