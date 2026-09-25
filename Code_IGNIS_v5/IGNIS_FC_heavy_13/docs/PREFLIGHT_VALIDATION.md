# v11 bench validation and pre-flight checklist

Ignitia Rocket Lab — LASC 2026, Mission ID 22
Departure for Iacanga: **26 August 2026**

Tests **3, 5 and 6** are the ones that satisfy REC 8.4.2 (successful ground
test of all recovery mechanisms, **mandatory annex to the Mission Report,
complete with dates and signatures**) and REC 8.4.3 (recovery electronics
demonstrate the ability to fire an electronic match). Record them properly —
photos, serial captures, signatures. Do not leave this for Brazil.

---

## Phase 0 — before any code runs

| # | Task | Pass criterion | Done |
|---|---|---|---|
| 0.1 | Multimeter across CH1 and CH2 terminals with e-match connected | Establishes **which channel the e-match is physically on**. FLIGHT072 fired CH1 with `pyro1_cont = 0`; CH2 read continuity and was never fired | ☐ |
| 0.2 | Confirm `Pyro.h` continuity polarity against the physical board | `cont = 1` with a known-good load, `cont = 0` with the loop open. If it does not track, the sense circuit is broken and every continuity reading is meaningless | ☐ |
| 0.3 | 330 Ω series resistor bodged onto `PYRO_CH1_GATE` and `PYRO_CH2_GATE` | Gate current < 15 mA measured | ☐ |
| 0.4 | MPU6050 `AD0` jumpered to GND on every board | Suspected cause of the FLIGHT073 IMU loss (`imu_valid = 0`, 12,970 rows) | ☐ |
| 0.5 | UART programming jig built (CP2102/CH340 on IO43/IO44) | Every board flashable without USB-C | ☐ |
| 0.6 | **Move the e-match to CH1** | `pyro1_cont` reads 1 at boot. It has read 0 across 23,196 rows | ☐ |
| 0.7 | Confirm `GROUND_TEST_MODE = false` and `SINGLE_EVENT_MODE = true` in the flashed build | Boot banner shows `ground_test:off  single_event:ON` | ☐ |

**Do not proceed past 0.1 and 0.2.** Everything downstream assumes you know
which channel is wired and that continuity readings mean something.

---

## Phase 1 — firmware bring-up

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 1.1 | Compile v11 with the toolchain in `LIBRARIES.md` | Clean build, no warnings that touch the pyro or FSM paths | ☐ |
| 1.2 | Flash, open serial at 115200 | `boot_count` prints and increments by exactly 1 per power cycle | ☐ |
| 1.3 | Disconnect one e-match, reboot | NO-GO banner prints **and** `playNoGoTone()` sounds 3× with red LEDs | ☐ |
| 1.4 | Reconnect, reboot | Normal `ARMED_MELODY`, no NO-GO | ☐ |

---

## Phase 2 — pyro (live e-match, flight battery)

> Treat every powered board as live pyro. Eye protection. Clear the bench.

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 2.1 | Fire a real e-match ×5, flight 2S3P LiPo, SD logging active | **5/5 ignition, 0 reboots.** `boot_count` unchanged across all five | ☐ |
| 2.2 | Inspect the SD log around each fire | Rows present continuously through and after the fire event — no 1.2 s hole | ☐ |
| 2.3 | Log VBAT during fire (scope on VBAT if available) | No excursion below 6.5 V (AMS1117-5.0 dropout) | ☐ |

If 2.1 shows any reboot, **stop**. Set `PYRO_GATE_REDUCED_DRIVE` to `false`,
confirm the 330 Ω bodge is present, and repeat. If it still reboots with a
resistor fitted and the loop open, the problem is not the gate drive and
needs a scope on the 3V3 rail before anything else happens.

If 2.1 shows a **misfire** (opto failed to switch), that is the reduced drive
strength. Set `PYRO_GATE_REDUCED_DRIVE` to `false` and repeat.

---

## Phase 3 — recovery system, vacuum chamber → REC 8.4.2 / 8.4.3 annex

Sealed jar plus a manual vacuum pump. Simulating 1000 m AGL from a ~1400 m
site needs roughly a 117 hPa reduction from ambient.

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 3.1 | Full profile: ground → simulated apogee → descent, e-match connected | `deploy_trigger = 1`, drogue fires < 300 ms after peak | ☐ |
| 3.2 | Main deployment on the descent leg | Fires at ≤ 450 m AGL **and** ≥ 1500 ms after drogue (stagger holds) | ☐ |
| 3.3 | Repeat 3.1 with the BMP180 disconnected mid-coast | `deploy_trigger = 2` at `APOGEE_TIMEOUT_MS` after burnout | ☐ |
| 3.4 | Repeat with MPU6050 disconnected, shake vertically | BOOST entered via `BARO-BACKUP` (check the serial print) | ☐ |
| 3.5 | Tap/shock test with AD0 grounded | `imu_valid` stays 1 throughout | ☐ |

**Capture for the annex:** date, time, operators, board serial, firmware
version (`v11`), the SD log file, and signatures. Photos of the setup.

---

## Phase 3b — the open question: reset at low battery (v12)

The FLIGHT072 mid-air reset is **not** explained. The pulley tests that did
not reset ran at 8.10–8.12 V; FLIGHT072 flew at 7.73 V. That is an
uncontrolled variable, not a negative result.

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 3b.1 | Discharge a pack to **7.73 V** — match it, do not approximate | Measured at rest, not under load | ☐ |
| 3b.2 | Repeat the pulley sequence ×5, `GROUND_TEST_MODE = true`, real e-match | `boot_count` does not increment in 5/5 | ☐ |
| 3b.3 | If it does increment, read `reset_reason` | 9 = brownout · 4/15 = GPIO overload · 4/5/6/7 = firmware crash | ☐ |
| 3b.4 | Scope on VBAT and 3V3, triggered on the gate pin | Settles it in one shot instead of five | ☐ |
| 3b.5 | 30-minute idle soak, board untouched | `boot_count` does not self-increment | ☐ |

## Phase 4 — system

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 4.1 | 4-hour standby on the bench, VBAT logged | ECS 9.1.3. **Expected to fail as-is** — FLIGHT073 dropped 7.75 → 7.54 V in 464 s (~1.6 V/h). Measure it properly, then decide on light-sleep work | ☐ |
| 4.2 | Ground station updated to the v11 packet, end-to-end | 98-byte packets decode; `tilt_deg` / `boot_count` / `deploy_trigger` display correctly | ☐ |
| 4.3 | RF range test in flight configuration, antenna as mounted | Pad RSSI better than −115 dBm. FLIGHT072 sat at −123.6 dBm with 40.2 % loss — about 2 dB from total link failure | ☐ |
| 4.4 | RRC3 configured, independent battery, independent RBF | Drogue channel repurposed as backup main; main terminals unwired | ☐ |
| 4.5 | `APOGEE_TIMEOUT_MS` re-tuned to the actual predicted profile | Matches OpenRocket burnout→apogee plus margin | ☐ |
| 4.6 | `MIN_APOGEE_ALT_M` sanity-checked against the predicted apogee | 30 m must be well below the expected peak | ☐ |

---

## Pad procedure — every launch, no exceptions

1. Power the board. **Listen.** `ARMED_MELODY` = go. `NOGO_MELODY` = stop and
   find out why.
2. Confirm on the ground station: continuity green on the wired channel,
   `boot_count` noted, and **`battery_v` ≥ 7.80 V** — FLIGHT072 flew at
   7.73 V and that should have been a pad no-go.
3. Write the `boot_count` on the launch card. If it differs after recovery,
   the FC rebooted in flight.
4. Arm the RRC3 on its own pull-pin, independently.

FLIGHT072 was lost because step 1 existed only as a line of serial text that
nobody was reading. That is the whole reason the NO-GO tone was added.
