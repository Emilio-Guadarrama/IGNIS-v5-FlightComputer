# IGNIS v5 flight computer firmware: CHANGES (0.1.0-prelim)

Preliminary firmware revision for IGNIS v5 (ESP32-S3-WROOM-1-N16R8). It is a
new code base in `Code_IGNIS_v5/IGNIS_v5_FC/`. The reference firmware
`Code_IGNIS_v5/IGNIS_FC_heavy_13/` was used read-only as a source of proven
building blocks and is unchanged. Every reused block cites its reference file
and line in the code.

**Nothing in this revision is approved for flight.** Every pyro, battery and
arming-related item below is **PENDING SIGN-OFF** by the Avionics Lead.

---

## 1. What the firmware does

- **Boot:** both pyro gates are driven LOW first. The firmware reads
  `esp_reset_reason()` and the persisted flags and decides NORMAL or RECOVERY.
  It logs the version, git hash and the SHA-256 of the running app partition.
- **Flight task** (core 1, 50 Hz, task-watchdog subscribed): LSM6DSV32X + DPS368
  → flight FSM → pyro → RAM snapshot → SD queue. Its states are
  `SAFE → ARMED → BOOST → COAST → (APOGEE) → DROGUE_DESCENT → MAIN_DESCENT → LANDED`,
  plus the terminal `RECOVERY`.
- **SAFE/ARMED** mirror the hardware arming signal (continuity through the pyro
  optocouplers). An invalid reading counts as ARMED. Liftoff detection runs in
  both pad states.
- **SD task** and **LoRa task** (core 1) share the SPI bus through one mutex.
  The flight task never touches SPI.
- **WiFi task** (core 0, priority 1): the read-only dashboard and the gated OTA
  endpoint. The AP is off unless the BOOT button is held.
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
| A | POST_FIRE + flight-in-progress flags in NVS, written on transitions only | `src/hal/nvs_state.cpp` (`nvsSetPostFire`, `nvsSetFlightInProgress`) |
| A | Gate asserted **before** the NVS write | `src/app/flight_task.cpp` (pyro block) |
| A | Reset reason + flags → NORMAL/RECOVERY, clear only on `ESP_RST_POWERON` | `src/core/boot_policy.cpp`, `IGNIS_v5_FC.ino` `setup()` |
| A | RECOVERY: SD + LoRa on, WiFi/OTA off, no arming, no firing | `flight_fsm.cpp` (terminal state), `pyro_logic.h` `lockout()`, `wifi_dashboard.cpp` latch, `ota_gate.cpp` |
| A | `REBOOT_FIRE_POLICY` constant (only NEVER_FIRE implemented) | `src/config/config_v5.h` |
| A | reset_reason + boot_count logged every boot, in every SD row and in telemetry | `IGNIS_v5_FC.ino`, `csv_row.cpp`, `telemetry_packet.cpp` |
| A | 98-byte packet unchanged, `static_assert` | `src/core/telemetry_packet.h` |
| A | Watchdogs untouched (sdkconfig TWDT 5 s/INT_WDT 300 ms); forced post-pyro flush kept | `config_v5.h` (`PYRO_FORCE_FLUSH_MS`, `PYRO_WINDOW_FLUSH_INTERVAL_MS`), `sd_logger.cpp` |
| B | SoftAP, WPA2 from gitignored `secrets.h`, 8-63 chars `static_assert` | `src/net/wifi_dashboard.cpp`, `src/net/secrets_check.h`, `secrets_example.h`, `.gitignore` |
| B | Read-only: GET endpoints only, no arm/fire/disarm/mode/disable | `wifi_dashboard.cpp` (`startAp`) |
| B | Shows state, sensors, VBAT, continuity/channel, GPS, LoRa, reset_reason, FW version/hash, SD | `/api/state` in `wifi_dashboard.cpp` |
| B | Off at BOOST and latched until power cycle; backstop from `loop()` | `wifi_dashboard.cpp` (`wifiTask`, `wifiBackstop`) |
| B | Snapshot at 2-5 Hz (`WIFI_SNAPSHOT_HZ`), SD download only in SAFE | `wifi_dashboard.cpp` |
| B | 1 client, HT20, `esp_wifi_set_max_tx_power` configurable | `config_v5.h` `WIFI_*`, `startAp()` |
| B | WiFi task on core 0 at priority 1; flight/SD/LoRa on core 1 above `arduino_events` (19) | `config_v5.h` `TASK_*` |
| B | SPI mutex, SD and LoRa tasks with queues, bounded lock times | `src/hal/spi_bus.*`, `sd_logger.cpp`, `lora_sx1276.cpp` |
| B | Compile-time flags `IGNIS_ENABLE_WIFI`, `IGNIS_ENABLE_OTA` | `config_v5.h` |
| C | Two OTA slots, rollback; valid only after self-test | `partitions.csv`, `verifyRollbackLater()` in `.ino`, `src/app/selftest.cpp` |
| C | OTA password (digest auth) + SHA-256 header + `esp_ota_end` image check | `src/net/ota_update.cpp` |
| C | Refused on continuity (either), invalid/stale continuity, not SAFE, flight, post-fire, recovery; abort mid-upload | `src/core/ota_gate.cpp`, `ota_update.cpp` (start / every chunk / idle poll / before boot switch) |
| C | Version + git hash + app SHA-256 at boot, SD `BOOTLOG.TXT`, dashboard | `IGNIS_v5_FC.ino`, `sd_logger.cpp`, `/api/state` |
| Pins | Single pin/HW header | `src/config/pins_v5.h` |

---

## 3. Safety-critical items for sign-off

None of these are approved. Each needs the Avionics Lead's review and a bench result.

| # | Item | Where | Change vs reference | Status |
|---|---|---|---|---|
| S1 | **Reboot never fires**: any non-POR reset with flight/post-fire flag → RECOVERY, pyro locked out (FSM terminal + `PyroLogic::lockout`) | `boot_policy.cpp`, `flight_fsm.cpp`, `pyro_logic.h`, `flight_task.cpp` | **Behavior change.** Reference v13 resumed into the persisted phase and could still fire the drogue after a mid-COAST reset (`FlightState.h:148-193`). Now a reset before apogee means no SRAD deployment; the RRC3 is the recovery path. | PENDING SIGN-OFF |
| S2 | Flags cleared only on `ESP_RST_POWERON` (enum value 1 verified in IDF 5.5 headers). A brownout deep enough to cause a POR is indistinguishable from an RBF cycle and also clears them. | `config_v5.h` `RESET_REASON_POWERON_VALUE` | New | PENDING SIGN-OFF + bench 1.4 |
| S3 | Unreadable NVS on a non-POR reset → RECOVERY (fail closed). NVS is erased only on POR. | `boot_policy.cpp`, `nvs_state.cpp` | New | PENDING SIGN-OFF |
| S4 | SAFE/ARMED from continuity; invalid reading = ARMED; return to SAFE after 1 s debounce (`SAFE_RETURN_DEBOUNCE_MS`, new constant) | `flight_fsm.cpp` | New (reference booted straight to ARMED) | PENDING SIGN-OFF |
| S5 | Liftoff detection allowed from SAFE as well as ARMED, so a bad continuity read cannot cancel a flight | `flight_fsm.cpp` | Consistent with reference philosophy (`Pyro.h:11-16`) | PENDING SIGN-OFF |
| S6 | Continuity is **not** a firing gate | `flight_fsm.cpp`, `pyro_hw.cpp` | Unchanged from reference | PENDING SIGN-OFF |
| S7 | Interlocks ported unchanged: MIN_APOGEE_ALT_M 30 m on both apogee paths, APOGEE_TIMEOUT_MS 20 s (**re-tune per flight**), BURNOUT_TIMEOUT 4 s, boost 1.8 g/250 ms + baro 15 m/s/150 ms, 1500 ms drogue→main stagger, SINGLE_EVENT_MODE=1, 1000 ms pulse | `config_v5.h`, `flight_fsm.cpp` | Values unchanged; FSM refactored to emit requests only | PENDING SIGN-OFF |
| S8 | `GROUND_TEST_MODE` is a compile-time `#error` in a flight build; no runtime toggle of it or SINGLE_EVENT_MODE | `config_v5.h` | Stricter than reference | PENDING SIGN-OFF |
| S9 | Gate asserted before the NVS post-fire write (flash write can take tens of ms; the fire is never delayed) | `flight_task.cpp` | New | PENDING SIGN-OFF |
| S10 | Pyro gates LOW before `pinMode(OUTPUT)`, first line of `setup()`; reduced drive (`PYRO_GATE_REDUCED_DRIVE`) kept | `pyro_hw.cpp` | Same mitigation as reference `Pyro.h:83-104` | PENDING SIGN-OFF + bench 5/5 switching |
| S11 | Task watchdog: flight task subscribed and fed only after a full iteration. SD/LoRa tasks deliberately **not** subscribed (a storage/radio hang must not reset the board in flight, given S1) | `flight_task.cpp` | New subscription; timeouts unchanged | PENDING SIGN-OFF |
| S12 | OTA gate (fail closed, abort mid-upload) | `ota_gate.cpp`, `ota_update.cpp` | New | PENDING SIGN-OFF |
| S13 | OTA self-test failure on a pending image → rollback reboot (never on RECOVERY or after flight latch) | `selftest.cpp` | New | PENDING SIGN-OFF |
| S14 | Battery: VBAT NO-GO 7.80 V / warn 8.00 V, advisory only (buzzer/LED/dashboard), never inhibits firing. `VBAT_DIVIDER_RATIO` is unconfirmed for v5. | `config_v5.h`, `pins_v5.h`, `status_io.cpp` | Thresholds unchanged | PENDING SIGN-OFF |
| S15 | Main-channel (CH2) path is reachable only with `SINGLE_EVENT_MODE=0` (compile-time) and only from DROGUE_DESCENT after a drogue request | `flight_fsm.cpp` | Unchanged logic | PENDING SIGN-OFF |

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
| `PYRO_CONT_ACTIVE_LEVEL` | 1 (HIGH = continuity) | Reference polarity (`Pyro.h:18-22`); v5 Pyros sheet not verified |
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
   98-byte payload so the existing RadioHead ground station keeps working.
   Verified offline: a v5-encoded packet decodes correctly with the unmodified
   `telemetry_parser_v12.py`. Over-the-air: still to be checked on the bench.
3. **LSM6DSV32X / DPS368 / SX1276 register maps** were written from the
   published register maps. The datasheet PDFs in `PCB_Project_IGNIS-v5/Components`
   could not be text-extracted on this machine (no PDF tools), so they were
   **not cross-checked**. Each driver checks the chip ID and fails safe (sensor
   invalid; the other liftoff path stays active).
4. IMU: ±32 g, ±2000 dps, 480 Hz, gyro bias only (accel used untrimmed).
   Baro: 32 Hz × 8 oversampling, continuous. I2C at 400 kHz (reference: 100 kHz).
5. LoRa PA: `RegPaConfig = 0x80|0x70|(17-2)` for 17 dBm on PA_BOOST. Measure it.
6. SD: a new file `/V5Fnnnn.CSV` on every boot (the reference reopened the
   pre-reset file through RTC); boot identity goes in `/BOOTLOG.TXT`. After
   LANDED: 1 Hz rows, flushed every row, file kept open until power cycle
   (the reference closed it).
7. CSV columns keep the reference order up to `deploy_trigger`; the reference
   `resumed` column is replaced by v5 columns; `mpu_temp_c` becomes `imu_temp_c`.
8. Flight/LoRa/SD task priorities 22/21/20 sit above the core-1 `arduino_events`
   task (priority 19, verified in the 3.3.12 sdkconfig).
9. GPS control pins are never driven (unconfirmed nets).
10. The v2-board I2C "precondition" step (floating BMP180 CSB) was not ported;
    the 9-clock recovery and runtime recovery were.

### Telemetry format
Unchanged, 98 bytes, ground-station compatible. RECOVERY is sent as `state = 8`
(older parsers show `UNKNOWN(8)` and decode everything else correctly).
`groundstation/telemetry_parser_v5.py` adds the state names. **Not added**,
because there is no spare byte without bit-packing existing fields: a
continuity-valid bit, the WiFi state and a post-fire bit. Proposal for a
future packet v12: append `uint8_t v5_flags` (bit0 cont_valid, bit1
post_fire_latched, bit2 recovery_boot, bits3-4 wifi) → 99 bytes (+~4 ms ToA).
Needs your approval and a ground-station update.

### Standby power proposal (flagged for review, not implemented)
- WiFi is **off by default**. It starts only on a 2 s BOOT-button hold on the
  pad, turns off after 10 min with no client, and latches off at BOOST.
- Light sleep and GPS backup mode are **not implemented**
  (`STANDBY_LOW_POWER_ENABLED 0`). The prebuilt Arduino libs have
  `CONFIG_PM_ENABLE` off, so automatic light sleep is not available, and
  manual light sleep conflicts with 50 Hz liftoff detection.
- Proposal: a pad-only low-rate mode (CPU 80 MHz, IMU at 15 Hz in its low-power
  mode with the wake-up interrupt on IMU_INT1 to return to full rate, GPS put
  into backup via UBX-RXM-PMREQ until ARMED).
  This needs IMU_INT1 and GPS EXTINT confirmed, and bench 8.1 numbers first.

---

## 6. Tested vs assumed

**Tested (on this PC, no hardware):**
- Host tests: **36/36 PASS** (`run_host_tests.ps1`, clang 22 via zig): 13 FSM
  scenarios (nominal, apogee-only firing, pulley-jerk debounce, ground
  false-boost interlock, timeout failsafe, dual-event altitude and stagger,
  low-apogee stagger, RECOVERY never fires, continuity fail-closed, liftoff
  from SAFE, baro-backup liftoff, invalid baro ignored, millis rollover),
  boot policy across all reset reasons, OTA gate per condition, pyro latch and
  lockout, packet layout/offsets, RadioHead header, NMEA (checksum, garbage,
  overlong lines), CSV column count, DPS368 coefficient packing.
- Mutation check: removing the altitude interlock, or removing the drogue→main
  stagger, each makes a test fail.
- Ground-station compatibility: a v5-encoded packet decodes correctly with the
  unmodified reference `telemetry_parser_v12.py`; `telemetry_parser_v5.py`
  self-check passes.
- ESP32-S3 builds with `--warnings all` against Arduino-ESP32 3.3.12, details
  in the build log section below.

**Assumed / not run (needs the bench checklist):**
- Anything on hardware: nothing was flashed. Driver register maps (item 5.3),
  pin map, polarities, LoRa over-the-air framing, PA power, reset_reason
  produced by RBF/EN/brownout, OTA end-to-end, rollback, WiFi coexistence,
  SD timing, standby current.

### Build log
| Build | Flags | Result |
|---|---|---|
| FLIGHT | defaults | OK, 0 warnings in `src/`, app 1,066,406 B, RAM 53,148 B |
| No WiFi/OTA | `-DIGNIS_ENABLE_WIFI=0 -DIGNIS_ENABLE_OTA=0` | OK, 0 warnings in `src/`, app 446,543 B, RAM 26,228 B |
| Guard | `-DGROUND_TEST_MODE=1` (flight build) | Fails with `#error "GROUND_TEST_MODE must be 0 when IGNIS_FLIGHT_BUILD is 1."` (checked by preprocessing `config_v5.h` with host clang; the full ESP32 rebuild was stopped to save time) |
| Guard | `-DIGNIS_ENABLE_WIFI=0` alone | Fails with `#error "IGNIS_ENABLE_OTA requires IGNIS_ENABLE_WIFI."` (host clang) |
| Not built | GROUND build (`-DIGNIS_FLIGHT_BUILD=0 -DGROUND_TEST_MODE=1`) on ESP32 | Compile it before bench section 3 |

---

## 7. Open questions

1. **S1 policy:** is "no SRAD deployment after any in-flight reset" acceptable?
   Or should a reset in COAST (no fire yet) be allowed to re-arm only the
   timeout failsafe? Today: never fire.
2. Is the POR-clears-flags trade-off (S2) acceptable, given the brownout case?
3. GPS control nets: confirm what IO15 / IO16 / IO39 connect to on the RF sheet.
4. Continuity polarity and the VBAT divider on v5.
5. Should OTA require an SD card present (it does now, through the self-test)?
6. Packet v12 extension (`v5_flags` byte): approve or not.
7. Main deployment altitude (450 m) and the APOGEE_TIMEOUT for the 2027 profiles.
