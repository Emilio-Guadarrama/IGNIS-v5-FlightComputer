# Libraries and toolchain — IGNIS FC v11

Third-party libraries are **not bundled** in this archive. They carry their own
licences, and vendoring frozen copies into the flight repo is how version
drift becomes untraceable — you already have one instance of that (the
installed RadioHead build has no `setSyncWord()`, which is why `Telemetry.h`
writes register `0x39` directly).

Install from Library Manager or with the script below, then record the exact
versions you flew in the mission report.

---

## Board package

| Item | Value |
|---|---|
| Core | `esp32:esp32` (Espressif Systems) |
| Version | **3.3.10** — this is what the codebase targets |
| Board | ESP32S3 Dev Module |
| Flash Size | 8 MB (64 Mb) — module is `ESP32-S3-WROOM-1-N8R8` |
| PSRAM | **OPI PSRAM** (8 MB Octal) |
| Partition Scheme | 8M with spiffs (3MB APP / 1.5MB SPIFFS) |
| USB CDC On Boot | Enabled (leave Disabled if programming over the UART jig) |
| Upload Speed | 921600 (drop to 115200 on the UART jig if unstable) |

The core version matters. `StatusIndicator.h` branches on
`ESP_ARDUINO_VERSION_MAJOR >= 3` for the LEDC API — the `< 3` branch is a
defensive fallback that has **not** been validated on this hardware.

## Libraries

| Library | Author | Version used | Notes |
|---|---|---|---|
| RadioHead | Mike McCauley | **1.122** (as installed) | `RH_RF95` only. `setSyncWord()` absent — do not "fix" `Telemetry.h` by calling it |
| TinyGPSPlus | Mikal Hart | 1.1.0 | NMEA parsing, `Sensors.h` |
| Adafruit NeoPixel | Adafruit | 1.12.3 | WS2812B status LEDs |

Built into the ESP32 core, nothing to install: `SPI`, `SD`, `Wire`,
`Preferences`, `esp_system`, `driver/gpio`.

BMP180 and MPU6050 use raw-register drivers in `Sensors.h`. Do **not** install
`Adafruit_BMP085` or `Adafruit_MPU6050` — they are not used and will only
confuse whoever picks this up next.

---

## Install

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32@3.3.10

arduino-cli lib install "RadioHead"
arduino-cli lib install "TinyGPSPlus@1.1.0"
arduino-cli lib install "Adafruit NeoPixel@1.12.3"
```

`install_libraries.sh` in this folder does the same and then prints the
resolved versions so you can paste them into the mission report.

## Compile

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB,CDCOnBoot=cdc \
  --warnings all \
  IGNIS_FC_heavy_11
```

## Upload over the UART jig (bypasses the failing USB-C connectors)

Five of the flight computers have intermittent or dead USB-C. The connector is
only needed for programming — the boards log and fly correctly on battery
power alone. A CP2102 or CH340 adapter on the UART0 pins removes the
dependency entirely.

```
Adapter TX  ->  IO44  (ESP32 U0RXD)
Adapter RX  ->  IO43  (ESP32 U0TXD)
Adapter GND ->  board GND
Boot button ->  IO0 to GND
Reset       ->  EN to GND (momentary)
```

Entering download mode: hold IO0 low, pulse EN low then release, release IO0.

```bash
arduino-cli upload -p /dev/ttyUSB0 \
  --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=8M,CDCOnBoot=default \
  IGNIS_FC_heavy_11
```

Set **USB CDC On Boot: Disabled** when programming this way, or `Serial` output
goes to the USB peripheral instead of UART0 and the console will look dead.

---

## Compile verification status

This firmware has **not** been compiled against the real toolchain — the build
environment used to produce these patches has no ESP32 core installed. Brace
and parenthesis balance was verified programmatically on every file; that is
not the same as a clean compile.

**Compile it before you flash anything, and fix trivial errors rather than
assuming a patch is wrong.**
