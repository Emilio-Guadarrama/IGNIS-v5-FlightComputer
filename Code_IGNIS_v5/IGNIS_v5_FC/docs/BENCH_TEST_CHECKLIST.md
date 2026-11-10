# IGNIS v5 preliminary firmware: bench test checklist

**Inert loads only.** No e-matches, igniters or energetics in the room for any
test on this sheet. Use an inert e-match simulator on each pyro terminal (e.g.
a 1-2 Ω power resistor in series with an indicator LED and a fuse, sized for the
pyro supply). Treat every powered board as live pyro anyway.

Record for every session: date, operators, board serial, `IGNIS_FW_VERSION`,
`IGNIS_GIT_HASH`, and the **app sha256** printed at boot (also in
`/BOOTLOG.TXT` on the SD card and on the dashboard).

Firmware builds referenced below:
- **FLIGHT**: default flags (`IGNIS_FLIGHT_BUILD=1`, `GROUND_TEST_MODE=0`).
- **GROUND**: `-DIGNIS_FLIGHT_BUILD=0 -DGROUND_TEST_MODE=1` (lowers the altitude
  interlock to 1.5 m and the boost debounce to 40 ms). Never fly it.

---

## 0. Setup

| # | Check | Pass criterion | Done |
|---|---|---|---|
| 0.1 | No energetics in the room; inert loads on CH1 and CH2 | Confirmed by a second person | ☐ |
| 0.2 | `secrets.h` created from `secrets_example.h` with real passwords | `git status` does not show `secrets.h` | ☐ |
| 0.3 | Host tests: `powershell -File test\host\run_host_tests.ps1` | All tests PASS | ☐ |
| 0.4 | `python -I tools/gen_build_info.py`, then compile FLIGHT | Zero warnings in `src/`; app < 4 MB | ☐ |

## 1. Identity and boot

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 1.1 | Power on (RBF), read serial banner | Version, git hash, app sha256, `GROUND_TEST_MODE=0`, `SINGLE_EVENT_MODE=1` printed | ☐ |
| 1.2 | Compare the banner sha256 with `sha256sum` of the flashed `.bin` | Match | ☐ |
| 1.3 | Power-cycle 3× via RBF | `boot_count` +1 each time; `reset_reason 1 POWERON` | ☐ |
| 1.4 | Record the `reset_reason` produced by: EN button, USB re-enumeration, RBF cut, slow PSU dip to brownout | Table filled in. **This settles the POWERON assumption for v5** | ☐ |

## 2. Pins (confirms "Pins pending confirmation" in CHANGES.md)

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 2.1 | Inert load on CH1 only, then CH2 only, then none | `cont` follows the load on the right channel; polarity matches `PYRO_CONT_ACTIVE_LEVEL` | ☐ |
| 2.2 | SD card in / out | `SD_DETECT` polarity matches `SD_DETECT_ACTIVE_LOW` (boot message) | ☐ |
| 2.3 | VBAT reading vs DMM at 7.4 / 8.0 / 8.4 V | Within ±0.05 V; else fix `VBAT_DIVIDER_RATIO` | ☐ |
| 2.4 | IMU/baro detected | Boot log shows the I2C addresses found; dashboard shows sane values | ☐ |
| 2.5 | GPS outdoors | Fix within 2 min; `PIN_GPS_*` control pins confirmed from the schematic | ☐ |
| 2.6 | WS2812 / buzzer | LED0-2 and tones as described in `status_io.h` | ☐ |

## 3. State machine and gates (GROUND build, inert loads, vacuum chamber or pulley)

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 3.1 | No load connected | State `SAFE`; dashboard OTA gate "open" | ☐ |
| 3.2 | Connect an inert load | `ARMED` immediately; returns to `SAFE` ≥1 s after removal | ☐ |
| 3.3 | Chamber profile ground → apogee → descent | Gate LED on CH1 lights only after apogee; `deploy_trigger=1`; one pulse of 1000 ms | ☐ |
| 3.4 | Repeat 3.3, but disconnect the barometer after the chamber has passed 1.5 m simulated altitude | `deploy_trigger=2` at `APOGEE_TIMEOUT_MS` after burnout | ☐ |
| 3.5 | Short jerk (< 250 ms) in a FLIGHT build | No `BOOST` | ☐ |
| 3.6 | SD log around the fire | Rows continuous through the fire; flushed at ≤100 ms in the 4 s window | ☐ |

## 4. No restart after ignition (requirement A)

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 4.1 | After 3.3 (CH1 commanded), dip PSU to cause a brownout reset (not POR) | Boots into `RECOVERY`; banner says pyro locked out; WiFi does not start | ☐ |
| 4.2 | In RECOVERY, run the chamber profile again | **No gate activity on either channel**; SD + LoRa running; telemetry `state=8`, `reset_reason` shown, `pyro1_fired=1` | ☐ |
| 4.3 | Hold BOOT 3 s in RECOVERY | AP does not start | ☐ |
| 4.4 | RBF power cycle | `NORMAL`, flags cleared, `SAFE`/`ARMED` again | ☐ |
| 4.5 | BOOST only (shake/chamber, no apogee), then brownout | `RECOVERY` (flight_in_progress flag) | ☐ |
| 4.6 | Re-flash over USB after a fire (no power cycle) | Note the reset reason; expected `RECOVERY` until RBF power cycle | ☐ |

## 5. Shared SPI, SD and LoRa

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 5.1 | 30 min on the pad | `sd_rows_dropped` = 0; `lora_skipped` ≈ 0 | ☐ |
| 5.2 | Pull the SD card while logging | LoRa continues; SD status ERROR; no reset | ☐ |
| 5.3 | Flight-loop timing: Δ`timestamp_ms` between SD rows | 20 ms ± 2 ms for >99.9 % of rows, with WiFi off and on | ☐ |

### 5.4 LoRa link quality (ground station logs RSSI/SNR/seq)

Same position, antenna and distance for all three runs; ≥ 300 packets each.

| Condition | Packets expected | Received | PER % | RSSI mean / min | SNR mean / min |
|---|---|---|---|---|---|
| A. WiFi off | | | | | |
| B. WiFi AP on, dashboard streaming (`/api/state` at 4 Hz) | | | | | |
| C. OTA upload in progress (SAFE, no loads) | | | | | |

Pass: PER(B) and PER(C) within 2 percentage points of PER(A); mean RSSI/SNR
within 2 dB. Note: OTA flash writes briefly stall both cores (expected), so
check the SD Δt histogram during C separately.

## 6. WiFi dashboard (requirement B)

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 6.1 | Boot, no button | No AP visible | ☐ |
| 6.2 | Hold BOOT 2 s in SAFE | AP `IGNIS-v5-XXXX`, WPA2, page loads | ☐ |
| 6.3 | Second client tries to join | Refused (max 1) | ☐ |
| 6.4 | `curl -X POST` to `/`, `/api/state`, `/arm`, `/fire`, `/disarm`, `/mode` | 404 for all; no state change | ☐ |
| 6.5 | Inert load connected (ARMED) | Dashboard still works; `/api/logs` returns 403 | ☐ |
| 6.6 | Trigger BOOST (GROUND build shake/chamber) | AP disappears within 1 s and does not return until power cycle (BOOT hold ignored) | ☐ |
| 6.7 | No client for 10 min | AP turns off; BOOT hold restarts it | ☐ |
| 6.8 | TX power / HT20 | Spectrum or phone app shows 20 MHz channel; reduced power | ☐ |

## 7. OTA (requirement C)

Upload command (PC on the AP):
```
curl --digest -u ota:<OTA_PASSWORD> -H "X-Image-SHA256: <sha256 of .bin>" -F "image=@IGNIS_v5_FC.ino.bin" http://192.168.4.1/ota
```

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 7.1 | SAFE, no loads, correct hash | 200, reboot, new sha256 in banner, `[SELFTEST] PASS`, image VALID | ☐ |
| 7.2 | Wrong password | 401, nothing written | ☐ |
| 7.3 | Wrong `X-Image-SHA256` | 403 "SHA-256 mismatch"; old image still boots | ☐ |
| 7.4 | Inert load on CH1 (then CH2) before upload | 403 gate (deny bits shown) | ☐ |
| 7.5 | Connect an inert load mid-upload | Upload aborted; old image still boots | ☐ |
| 7.6 | Upload an image with the SD card removed | New image boots, `[SELFTEST] FAIL`, **rolls back** to previous image | ☐ |
| 7.7 | Truncated / random `.bin` with matching hash | 403 "image verification failed" | ☐ |

## 8. Standby (flagged for review in CHANGES.md)

| # | Test | Pass criterion | Done |
|---|---|---|---|
| 8.1 | 4 h on the flight pack, WiFi never enabled, VBAT logged | Ends above `VBAT_NOGO_V`; record mA average | ☐ |
| 8.2 | Same with AP on for the first 10 min | Record the delta | ☐ |
