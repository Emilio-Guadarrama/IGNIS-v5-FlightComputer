# IGNIS v5 flight computer firmware, SINGLE-DEPLOY variant: CHANGES (0.2.0-prelim)

Preliminary firmware revision for IGNIS v5 (ESP32-S3-WROOM-1-N16R8). It is a
new code base in `Code_IGNIS_v5/IGNIS_v5_FC/`. The reference firmware
`Code_IGNIS_v5/IGNIS_FC_heavy_13/` was used read-only as a source of proven
building blocks and is unchanged. Every reused block cites its reference file
and line in the code.

**This variant is SINGLE-DEPLOY:** one parachute event at apogee. CH1 is the
primary charge and CH2 a backup charge for the same event. The variant name
appears in the boot banner, `/BOOTLOG.TXT` and the dashboard
(`IGNIS_FW_VARIANT`).

**Nothing in this revision is approved for flight.** Every pyro, battery and
arming-related item below is **PENDING SIGN-OFF** by the Avionics Lead.

---

## 1. What the firmware does

- **Boot:** both pyro gates are driven LOW first. The firmware reads
  `esp_reset_reason()` and the persisted flight state (phase, ground pressure,
  pyro attempts per channel) and picks one of four boot modes: NORMAL,
  RESUME_ASCENT, RESUME_DESCENT or RECOVERY. It logs the version, variant,
  git hash and the SHA-256 of the running app partition.
- **Flight task** (core 1, 50 Hz, task-watchdog subscribed): LSM6DSV32X + DPS368
  → flight FSM → pyro sequencer → gate GPIO → persist → RAM snapshot → SD queue.
  States: `SAFE → ARMED → BOOST → COAST → (APOGEE) → DESCENT → LANDED`, plus
  the terminal `RECOVERY`. Telemetry value 5 is shown as DESCENT; value 6
  (old MAIN_DESCENT) is never used in this variant.
- **Deployment sequence at apogee** (`src/core/pyro_sequencer.h`):
  - **t = 0:** CH1 primary, 500 ms pulse.
  - **t = 500 ms:** CH2 backup, 500 ms pulse. It always fires, starting as CH1's pulse ends.
  - **After each pulse (+250 ms):** if the channel **still reads continuity**, the
    e-match did not burn open, so that channel is retried 500 ms after its pulse
    ended. At most 3 attempts per channel (2 retries).
  - **Limits:** never two gates on at once, and no new attempt more than 10 s
    after apogee.
- **Unwanted reboot** (any reset other than power-on):
  - **During boost or coast:** the flight resumes and deploys at apogee as normal.
  - **During descent:** a channel is re-fired only if it still reads continuity
    and has attempts left. If CH2 never fired, it fires. Both happen only
    while provably airborne: valid barometer, at least 30 m above the saved
    ground level, never after LANDED, and within 10 s of the reboot.
  - **After landing, with no usable record, or with no ground reference:**
    RECOVERY (logging and LoRa only, pyro locked out).
  - Attempt counts are saved to NVS, so the per-channel cap holds across reboots.
- **SAFE/ARMED** follow the hardware arming signal (continuity through the pyro
  optocouplers). An invalid reading counts as ARMED. Liftoff detection runs in
  both pad states.
- **SD task** and **LoRa task** (core 1) share the SPI bus through one mutex.
  The flight task never touches SPI.
- **WiFi task** (core 0, priority 1): the read-only dashboard and the gated OTA
  endpoint. The AP is off unless the BOOT button is held, and it never starts
  after an in-flight reboot.
- **Telemetry:** the same 98-byte packet the existing ground station decodes.

Build (Arduino-ESP32 **3.3.12**, as installed; no third-party libraries):
```
copy secrets_example.h secrets.h     (then edit the passwords)
python -I tools/gen_build_info.py    (optional: embeds the git hash)
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,CDCOnBoot=cdc --warnings all IGNIS_v5_FC
```
`partitions.csv` in the sketch folder is picked up automatically. It has two
4 MB OTA slots. The app is about 1.07 MB.
Host tests: `powershell -File test\host\run_host_tests.ps1` (uses `py -m ziglang c++`).

---

## 2. Requirement to code mapping

| Req | What | Where |
|---|---|---|
| A | Persisted flight state (phase, ground pressure, attempts/channel) in NVS, written on transitions only; RTC mirror written first | `src/hal/nvs_state.cpp` (`persistPhase`, `persistAttempts`) |
| A | Gate GPIO set **before** any NVS write | `src/app/flight_task.cpp` (pyro block) |
| A | Reset reason + state → NORMAL / RESUME_ASCENT / RESUME_DESCENT / RECOVERY; cleared only on `ESP_RST_POWERON` | `src/core/boot_policy.cpp`, `IGNIS_v5_FC.ino` `setup()` |
| A | Reboot never re-fires a channel whose e-match opened; retries only intact channels while airborne (`REBOOT_FIRE_POLICY`) | `config_v5.h`, `flight_fsm.cpp` (`fire_permitted`), `pyro_sequencer.h` (`resumeAfterReboot`) |
| A | RECOVERY: SD + LoRa on, WiFi/OTA off, no arming, no firing | `flight_fsm.cpp` (terminal), `PyroSequencer::lockout()`, `wifi_dashboard.cpp` latch, `ota_gate.cpp` |
| A | reset_reason + boot_count logged every boot, in every SD row and in telemetry | `IGNIS_v5_FC.ino`, `csv_row.cpp`, `telemetry_packet.cpp` |
| A | 98-byte packet unchanged, `static_assert` | `src/core/telemetry_packet.h` |
| A | Watchdogs untouched (sdkconfig TWDT 5 s / INT_WDT 300 ms); post-pyro forced flush kept | `config_v5.h`, `sd_logger.cpp` |
| Single deploy | CH2 = backup charge right after CH1; retry on intact e-match; one gate at a time; 10 s window | `src/core/pyro_sequencer.h`, `config_v5.h` `PYRO_*` |
| B | SoftAP, WPA2 from gitignored `secrets.h`, 8-63 chars `static_assert` | `src/net/wifi_dashboard.cpp`, `src/net/secrets_check.h`, `secrets_example.h` |
| B | Read-only: GET endpoints only, no arm/fire/disarm/mode/disable | `wifi_dashboard.cpp` (`startAp`) |
| B | Shows state, sensors, VBAT, continuity/channel, attempts/channel, boot mode, GPS, LoRa, reset_reason, FW version/variant/hash, SD | `/api/state` in `wifi_dashboard.cpp` |
| B | Off at BOOST and latched until power cycle; backstop from `loop()` | `wifi_dashboard.cpp` (`wifiTask`, `wifiBackstop`) |
| B | Snapshot at 2-5 Hz (`WIFI_SNAPSHOT_HZ`); SD download only in SAFE | `wifi_dashboard.cpp` |
| B | 1 client, HT20, `esp_wifi_set_max_tx_power` configurable | `config_v5.h` `WIFI_*`, `startAp()` |
| B | WiFi task on core 0 at priority 1; flight/LoRa/SD on core 1 above `arduino_events` (19) | `config_v5.h` `TASK_*` |
| B | SPI mutex; SD and LoRa tasks with queues and bounded lock times | `src/hal/spi_bus.*`, `sd_logger.cpp`, `lora_sx1276.cpp` |
| B | Compile-time flags `IGNIS_ENABLE_WIFI`, `IGNIS_ENABLE_OTA` | `config_v5.h` |
| C | Two OTA slots, rollback; image marked valid only after the self-test | `partitions.csv`, `verifyRollbackLater()` in `.ino`, `src/app/selftest.cpp` |
| C | OTA password (digest auth) + SHA-256 header + `esp_ota_end` image check | `src/net/ota_update.cpp` |
| C | Refused on continuity (either channel), invalid/stale continuity, not SAFE, flight, post-fire, recovery; aborted mid-upload | `src/core/ota_gate.cpp`, `ota_update.cpp` |
| C | Version + variant + git hash + app SHA-256 at boot, in SD `BOOTLOG.TXT`, on the dashboard | `IGNIS_v5_FC.ino`, `sd_logger.cpp`, `/api/state` |
| Pins | Single pin/HW header | `src/config/pins_v5.h` |

---

## 3. Safety-critical items for sign-off

None of these are approved. Each needs the Avionics Lead's review and a bench result.

| # | Item | Where | Change vs reference | Status |
|---|---|---|---|---|
| S1 | **CH2 is a backup charge** for the same deployment and always fires, starting when CH1's pulse ends | `pyro_sequencer.h` | **Behavior change**: reference `SINGLE_EVENT_MODE` never fired CH2 (`Config.h:227-237`); the 450 m main event is removed | PENDING SIGN-OFF |
| S2 | **Pulse 1000 → 500 ms; CH1→CH2 1500 → 500 ms** (late deployments broke the shock cord twice) | `config_v5.h` `PYRO_FIRE_PULSE_MS`, `PYRO_BACKUP_DELAY_MS` | **Changed** (`Config.h:240`, `:204`). Gates never overlap (`#error` if backup delay < pulse) | PENDING SIGN-OFF + bench: reliable ignition at 500 ms |
| S3 | **Retry on intact e-match**: continuity still present 250 ms after a pulse → re-fire, max 3 attempts/channel, 500 ms after the pulse ended. Needs a VALID reading that shows the e-match intact, and continuity on that channel before the first fire. Unknown never retries | `pyro_sequencer.h` | **Changed**: replaces the one-shot latch (`Pyro.h:116-146`) | PENDING SIGN-OFF |
| S4 | A burned-open e-match that fuses into a short reads as intact → it is re-fired up to the cap (harmless to the deployment, but extra pyro current pulses) | `pyro_sequencer.h` | New consequence of S3 | PENDING SIGN-OFF |
| S5 | **Reboot in ascent** → resume and deploy at apogee (timers re-armed: failsafe can only fire later) | `boot_policy.cpp`, `flight_fsm.cpp` `beginResume` | Close to reference resume (`FlightState.h:148-193`) | PENDING SIGN-OFF |
| S6 | **Reboot in descent** → retry intact channels / fire a never-fired backup **only while airborne** (valid baro, AGL ≥ 30 m vs saved ground pressure) and within 10 s; never after LANDED. Prevents firing on the ground, e.g. a reset while the crew handles the rocket | `flight_fsm.cpp` (`fire_permitted`), `pyro_sequencer.h` | **New** | PENDING SIGN-OFF |
| S7 | Attempt counts persisted after every start, so the cap holds across reboots; phases persisted at BOOST/COAST/DESCENT/LANDED | `nvs_state.cpp`, `flight_task.cpp` | New | PENDING SIGN-OFF |
| S8 | State cleared only on `ESP_RST_POWERON` (value 1, verified in the IDF 5.5 headers). A brownout deep enough to cause a POR looks the same as an RBF cycle → board boots as on the pad (needs a new boost detection before any fire) | `config_v5.h` `RESET_REASON_POWERON_VALUE` | New | PENDING SIGN-OFF + bench 1.4 |
| S9 | NVS and RTC both unreadable on a non-POR reset → RECOVERY (fail closed). NVS erased only on POR | `boot_policy.cpp`, `nvs_state.cpp` | New | PENDING SIGN-OFF |
| S10 | SAFE/ARMED from continuity; invalid reading = ARMED; return to SAFE after 1 s (`SAFE_RETURN_DEBOUNCE_MS`, new) | `flight_fsm.cpp` | New (reference booted straight to ARMED) | PENDING SIGN-OFF |
| S11 | Liftoff detection from SAFE as well as ARMED; continuity is **not** a deployment gate | `flight_fsm.cpp` | Same philosophy as reference (`Pyro.h:11-16`) | PENDING SIGN-OFF |
| S12 | Interlocks ported unchanged: MIN_APOGEE_ALT_M 30 m on both apogee paths, APOGEE_TIMEOUT_MS 20 s (**re-tune per flight**), BURNOUT_TIMEOUT 4 s, boost 1.8 g/250 ms + baro 15 m/s/150 ms | `config_v5.h`, `flight_fsm.cpp` | Values unchanged | PENDING SIGN-OFF |
| S13 | `GROUND_TEST_MODE` is a compile-time `#error` in a flight build; no runtime toggles | `config_v5.h` | Stricter than reference | PENDING SIGN-OFF |
| S14 | Gates LOW before `pinMode(OUTPUT)`, first line of `setup()`; reduced gate drive kept | `pyro_hw.cpp` | Same as reference `Pyro.h:83-104` | PENDING SIGN-OFF + bench |
| S15 | Task watchdog: flight task subscribed and fed after a full iteration; SD/LoRa deliberately not subscribed | `flight_task.cpp` | New subscription; timeouts unchanged | PENDING SIGN-OFF |
| S16 | OTA gate (fail closed, abort mid-upload); self-test failure → rollback (never after a flight latch) | `ota_gate.cpp`, `ota_update.cpp`, `selftest.cpp` | New | PENDING SIGN-OFF |
| S17 | Battery: NO-GO 7.80 V / warn 8.00 V, advisory only; `VBAT_DIVIDER_RATIO` unconfirmed for v5 | `config_v5.h`, `pins_v5.h` | Thresholds unchanged | PENDING SIGN-OFF |

---

## 4. Pins pending confirmation

Source of the v5 map: `Main.SchDoc` net labels matched to MCU1 pins, plus the
pad numbers in the 25-Sep PCB ECO log. Pins not listed here are tagged
`v5-confirmed` in `pins_v5.h`. That match is by label position, not by netlist
export, so **please confirm the whole map with a netlist export once**.

| Pin / constant | Current value | Why unconfirmed |
|---|---|---|
| `PIN_GPS_EXTINT` | IO15 | Main label `INT_GPS`; RF sheet uses different names (`GPS_SAFEBOOT_N`, `GPS_PPS`). Left as input. |
| `PIN_GPS_RESET_N` | IO16 | Main label `RESET_GPS`; same naming mismatch. Left as input (never driven). |
| `PIN_GPS_PPS` | IO39 | Main `PPS_GPS` vs RF sheet `GPS_PPS`. Unused. |
| `GPS_UART_BAUD` | 9600 | SAM-M10Q default assumed |
| `PIN_IMU_INT1` | IO9 | Main label still `MPU6050_INT`; Sensors sheet has `IMU_INT1/2`. Unused (IMU polled). |
| `PYRO_CONT_ACTIVE_LEVEL` | 1 (HIGH = continuity) | Reference polarity (`Pyro.h:18-22`); v5 Pyros sheet not verified. **Retries depend on it.** |
| `VBAT_DIVIDER_RATIO` | 0.3007 | Reference value (`Config.h:137`); v5 Power sheet resistor values not read |
| `SD_DETECT_ACTIVE_LOW` | 1 | Reference assumption (`DataLogger.h:21`) |
| `BOOT_BUTTON_ACTIVE_LEVEL` | 0 | Button to GND assumed |
| `LSM6DSV32X_ADDR_*` | 0x6A then 0x6B | SA0 strap unknown; both probed |
| `DPS368_ADDR_*` | 0x77 then 0x76 | SDO strap unknown; both probed |

Changed versus the reference pin map: **SD_CS = IO41, SD_DETECT = IO42**
(reference `Config.h:23-24` had them swapped).

---

## 5. Assumptions and deviations (all marked `ASSUMPTION(v5)` or `TODO(v5-verify)` in the code)

1. **No third-party libraries.** RadioHead, TinyGPSPlus and Adafruit NeoPixel
   are not installed on the build machine, and RadioHead's DIO0 ISR does SPI
   transfers, which would get around the SD/LoRa mutex. They are replaced by
   in-tree code: a polled TX-only SX1276 driver, an NMEA parser, and WS2812
   output through the core's RMT API.
2. **RadioHead framing emulated**: a 4-byte header `FF FF 00 00` comes before the
   98-byte payload. Verified offline: a v5-encoded packet decodes correctly with
   the unmodified `telemetry_parser_v12.py`. Over-the-air: still to be checked
   on the bench.
3. **LSM6DSV32X / DPS368 / SX1276 register maps** were written from the
   published register maps. The datasheet PDFs in `PCB_Project_IGNIS-v5/Components`
   could not be text-extracted on this machine, so they were **not
   cross-checked**. Each driver checks the chip ID and fails safe.
4. IMU: ±32 g, ±2000 dps, 480 Hz, gyro bias only (skipped on any resumed boot).
   Baro: 32 Hz × 8 oversampling. I2C at 400 kHz (reference: 100 kHz).
5. LoRa PA: `RegPaConfig = 0x80|0x70|(17-2)` for 17 dBm on PA_BOOST. Measure it.
6. SD: a new file `/V5Fnnnn.CSV` on every boot; boot identity goes in
   `/BOOTLOG.TXT`. After LANDED: 1 Hz rows flushed every row, file kept open
   until power cycle.
7. CSV columns keep the reference order up to `deploy_trigger`, then v5 columns
   (`boot_mode, post_fire, pyro1_attempts, pyro2_attempts, cont_valid, ...`).
   `pyroN_fired` = at least one attempt.
8. Flight/LoRa/SD task priorities 22/21/20 sit above the core-1 `arduino_events`
   task (priority 19, verified in the 3.3.12 sdkconfig).
9. GPS control pins are never driven (unconfirmed nets).
10. If liftoff is detected before 20 valid baro samples exist, the first
    in-flight sample becomes the ground reference (conservative for the
    altitude gate).
11. If a reboot hits between the gate going HIGH and the NVS attempt write
    (a few ms), that attempt is not counted. The RTC mirror is written first
    and usually covers it.

### Telemetry format
Unchanged, 98 bytes, ground-station compatible. RECOVERY is sent as `state = 8`
(older parsers show `UNKNOWN(8)`). `pyroN_fired` = at least one attempt on that
channel. **Not added**, because there is no spare byte: attempt counts, boot
mode and a continuity-valid bit. Proposal for a future packet v12: append
`uint8_t v5_flags` + `uint8_t attempts` (2 bits per channel) → 100 bytes.
Needs your approval and a ground-station update.
`groundstation/telemetry_parser_v5.py` names the v5 states.

### Standby power proposal (flagged for review, not implemented)
- WiFi is **off by default**. It starts only on a 2 s BOOT-button hold on the
  pad, turns off after 10 min with no client, and latches off at BOOST.
- Light sleep and GPS backup mode are **not implemented**
  (`STANDBY_LOW_POWER_ENABLED 0`). The prebuilt Arduino libs have
  `CONFIG_PM_ENABLE` off, and manual light sleep conflicts with 50 Hz liftoff
  detection.
- Proposal: a pad-only low-rate mode (CPU 80 MHz, IMU wake-up interrupt on
  IMU_INT1 to return to full rate, GPS backup via UBX-RXM-PMREQ until ARMED).
  This needs IMU_INT1 and GPS EXTINT confirmed, and bench 8.1 numbers first.

---

## 6. Tested vs assumed

**Tested (on this PC, no hardware, nothing fired):**
- Host tests: **46/46 PASS** (`run_host_tests.ps1`, clang 22 via zig).
  - **FSM + sequencer:** nominal (CH2 exactly 500 ms after CH1, no overlap,
    nothing while climbing), deployment within 400 ms of apogee, retry until
    the cap when the e-match stays intact, no retry for a channel open on the
    pad, the remaining FSM scenarios, millis rollover.
  - **Resume after reboot:** coast resume still deploys; descent resume
    retries an intact channel; no re-fire after the e-match opened; never on
    the ground; never with an invalid baro; cap across reboot; 10 s window;
    LANDED → RECOVERY.
  - **Boot policy:** every phase × reset reason, merge, fail closed.
  - **Sequencer edge cases:** nothing before the command, not-permitted,
    invalid continuity, single gate, lockout, window.
  - **Others:** OTA gate, packet layout, NMEA, CSV, DPS368 math.
- **Mutation check:** removing the altitude interlock, the post-reboot
  airborne gate, the single-gate rule, or the valid-reading requirement for
  retries each makes a test fail.
- **Ground-station compatibility:** confirmed offline with the unmodified
  `telemetry_parser_v12.py`.
- **ESP32-S3 builds** (`--warnings all`, Arduino-ESP32 3.3.12): see the build log.

**Assumed / not run (needs the bench checklist):** anything on hardware
(nothing was flashed). That includes:
- e-match ignition at 500 ms
- continuity polarity and how fast it reads open after an ignition
- driver register maps and the pin map
- LoRa framing over the air and PA power
- the reset_reason that RBF, EN and a brownout actually produce
- OTA end-to-end, rollback, WiFi coexistence, SD timing, standby current

### Build log
| Build | Flags | Result |
|---|---|---|
| FLIGHT | defaults | OK, 0 warnings in `src/`, app 1,068,410 B, RAM 53,172 B |
| No WiFi/OTA | `-DIGNIS_ENABLE_WIFI=0 -DIGNIS_ENABLE_OTA=0` | OK, 0 warnings in `src/`, app 448,715 B, RAM 26,252 B |
| Guard | `-DGROUND_TEST_MODE=1` (flight build) | Fails with `#error "GROUND_TEST_MODE must be 0 when IGNIS_FLIGHT_BUILD is 1."` (host-clang preprocessing of `config_v5.h`) |
| Guard | `-DIGNIS_ENABLE_WIFI=0` alone | Fails with `#error "IGNIS_ENABLE_OTA requires IGNIS_ENABLE_WIFI."` (host clang) |
| Not built | GROUND build (`-DIGNIS_FLIGHT_BUILD=0 -DGROUND_TEST_MODE=1`) on ESP32 | Compile it before bench section 3 |

---

## 7. Open questions

1. Is the 500 ms pulse enough for your e-matches on the flight battery? (bench 3.7)
2. How quickly does the continuity input read open after a real ignition on
   the bench (inert loads cannot show this)? The 250 ms settle assumes it is fast.
3. Is the trade-off in S8 (deep brownout = power-on) acceptable?
4. GPS control nets: confirm what IO15 / IO16 / IO39 connect to on the RF sheet.
5. Continuity polarity and the VBAT divider on v5.
6. Should OTA require an SD card present (it does now, through the self-test)?
7. Packet extension (attempts + boot mode byte): approve or not.
8. APOGEE_TIMEOUT for the 2027 flight profile, and whether apogee detection
   latency (4 falling samples, ~200 ms) should be revisited.
