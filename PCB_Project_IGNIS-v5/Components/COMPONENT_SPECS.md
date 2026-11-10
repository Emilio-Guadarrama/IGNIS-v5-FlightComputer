# IGNIS v5: component specifications (from the datasheets in this folder)

Quick reference so the datasheets don't have to be re-read. Every number below
was read from the PDF named in the section header. Items marked **[unread]**
are not transcribed (the value is only in a figure, or the datasheet is not in
the repo). Do not treat an **[unread]** item as a spec.

Datasheets in the repo (all under `PCB_Project_IGNIS-v5/Components/`):
ESP32 (`ESP32/`), SAM-M10Q (`GPS module - SAM-M10Q-00B (GNSS)/`),
LSM6DSV32X (`6-axis IMU - LSM6DSV32XTR/`), DPS368 and BMP280
(`Barometric pressure sensor - BMP280/`), RFM95W (`RF Module (LoRa) - RFM95W/`).

## Summary table

| Part | Role | Supply (V) | Operating temp (°C) | Size (mm) | Reflow peak | MSL |
|---|---|---|---|---|---|---|
| ESP32-S3-WROOM-1-N16R8 | MCU | 3.0 / 3.3 / 3.6 | **-40 to +65** (R8 variant) | 18.0 × 25.5 × 3.1 | **[unread]** Figure 13, p.47; single reflow only | 3 |
| SAM-M10Q-00B | GNSS | VCC 2.7–3.6 | -40 to +85 | ~15.5 × 15.5 (see p.16), 5.6 g | per IPC/JEDEC J-STD-020 (no numbers in datasheet) | see note |
| LSM6DSV32X | IMU | 1.71–3.6 (IO 1.08–3.6) | -40 to +85 | LGA-14L 2.5 × 3.0 × 0.83 | per J-STD-020; ST TN0018 **[unread]** | **[unread]** |
| DPS368 | Barometer | VDD 1.7–3.6, VDDIO 1.2–3.6 | -40 to +85 | PG-VLGA-8-2 2.0 × 2.5 × 1.1 | **260 +0/-5** (Pb-free) | 1 |
| RFM95W-915S2 | LoRa | 1.8–3.7 | **-20 to +70** | **[unread]** Figure 56, p.120 | **[unread]** | **[unread]** |

**Watch-outs for a rocket (from the table):**
- The **N16R8 module is only rated to +65 °C ambient**. A sun-heated avionics bay
  in the Brazil/New Mexico summer can exceed that. The 85 °C and 105 °C module
  variants have other flash/PSRAM options (see the ESP32 section).
- The **RFM95W is rated -20 to +70 °C**, narrower than the other parts.
- The DPS368 pressure range is **300–1200 hPa**. 300 hPa is about 9 km altitude,
  which is well above the 3 km target.

---

## ESP32-S3-WROOM-1-N16R8 (Espressif datasheet v1.4)

| Item | Value |
|---|---|
| Memory | 16 MB flash (Quad SPI) + 8 MB PSRAM (**Octal SPI**) |
| Dimensions | 18.0 × 25.5 × 3.1 mm (WROOM-1, PCB antenna) |
| Supply VDD33 | 3.0 / 3.3 / 3.6 V (min/typ/max) |
| Absolute max supply | -0.3 to 3.6 V |
| Current the supply must deliver | ≥ 0.5 A |
| Operating ambient | **-40 to +65 °C for R8 / R16V variants**; 85 °C variants -40 to +85; H4 variants -40 to +105 |
| Storage | -40 to +105 °C |
| ESD | HBM 2000 V, CDM 500 V |
| Storage (sealed bag) | < 40 °C, 90 % RH |
| MSL | **3**; solder within 168 h after opening (factory conditions 25 °C / 60 % RH; the PDF text reads "255 C", most likely an extraction typo) |
| Reflow | **Single reflow only.** Profile is Figure 13, p.47 (image, **[unread]**: read it before setting the oven profile) |
| Other handling | No ultrasonic cleaning/welding near the module (p.48) |
| Strapping defaults | GPIO0 weak pull-up (=1), GPIO3 floating, GPIO45, GPIO46 (see the datasheet table "Default Configuration of Strapping Pins") |

Project rules that follow from this: IO35, IO36, IO37 are used by the octal PSRAM
and must never be assigned (also in `pins_v5.h`); the EPAD (ground pad) soldering
is optional but improves thermal behavior.

## SAM-M10Q-00B (u-blox UBX-22013293 R05)

| Item | Value |
|---|---|
| Constellations | Up to 4 concurrent GNSS; integrated SAW filter and LNA |
| Supply VCC | 2.7 / 3.0 / 3.6 V |
| IO supply V_IO | 2.7–3.0, up to VCC (max 3.6 V) |
| Backup supply V_BCKP | 1.65–3.6 V (internal switch from V_IO to V_BCKP at about 1.45 V) |
| Operating temp | -40 to +85 °C |
| Absolute max, storage | Ambient -40 to +85 °C, storage -40 to +85 °C |
| Power | About 37 mW in continuous tracking with four constellations |
| Pin note | SAFEBOOT_N has an internal 1 kΩ series resistor |
| Size | A/A1 about 15.5 mm; see the mechanical drawing, Table 19, p.16 for every dimension (height not transcribed); weight 5.6 g |
| Soldering | Reflow per IPC/JEDEC J-STD-020; **the datasheet gives no temperatures** |
| MSL | The value did not extract cleanly from the text (probably level 4). Check Table 20 before opening the bag. |

## LSM6DSV32X (ST DS13511 Rev 2)

| Item | Value |
|---|---|
| Package | LGA-14L, 2.5 × 3.0 × 0.83 mm typ. (outline title says 0.86) |
| Supply Vdd | 1.71–3.6 V; IO supply 1.08–3.6 V |
| Operating temp | -40 to +85 °C |
| Storage | -40 to +125 °C |
| Shock survival | 20,000 g for 0.2 ms |
| Full scales | Accel ±4/8/16/**32** g; gyro ±125 to ±4000 dps |
| Sensitivity | Accel 0.976 mg/LSB at ±32 g; gyro 70 mdps/LSB at ±2000 dps; temperature 256 LSB/°C, 0 LSB = 25 °C |
| Current | 0.65 mA in combo high-performance mode |
| I2C address | 0x6A (SA0 low) or 0x6B (SA0 high); WHO_AM_I (0x0F) = 0x70 |
| Soldering | ECOPACK/RoHS, qualified to J-STD-020. Land pattern and profile are in ST **TN0018 [unread, not in repo]** |
| Driver gotcha | CTRL8 bit 2 must be written as 1; the datasheet says so |

## DPS368 (Infineon v1.1, 2019-07-03)

| Item | Value |
|---|---|
| Package | PG-VLGA-8-2, 2.0 × 2.5 × 1.1 mm |
| Pressure range | 300–1200 hPa |
| Supply | VDD 1.7–3.6 V, VDDIO 1.2–3.6 V; ramp-up time 0.001–5 ms |
| Absolute max | VDD/VDDIO and any pin 4 V; ESD HBM ±2 kV; max pressure 10,000 hPa |
| Operating / storage temp | -40 to +85 °C / -40 to +125 °C |
| Stability | Solder drift 0.8 hPa (min. solder height 50 µm); long-term 1 hPa |
| Current | Peak 345 µA during a pressure measurement; standby 0.5 µA |
| I2C address | 0x77 (SDO high) / 0x76 (SDO low); ID register 0x0D = 0x10 |
| MSL | **1** |
| Reflow (Pb-free) | Preheat 150–200 °C, 60–120 s; **peak 260 °C (+0/-5)**, 20–40 s within 5 °C of peak; ramp-down ≤ 6 °C/s; time from 25 °C to peak ≤ 8 min. Sn-Pb: soak 100–150 °C, peak 235 °C (+0/-5). Forced-convection oven recommended. |

BMP280 (`BMP280_Datasheet.pdf`) is in the folder because the v5 footprint is
BMP280-compatible, but the v5 firmware uses the DPS368. **[unread]** here.

## RFM95W-915S2 (HopeRF, preliminary datasheet)

| Item | Value |
|---|---|
| Frequency | 915 MHz band module (SX1276 core) |
| Supply Vdd | 1.8–3.7 V (a separate range applies to +20 dBm operation) |
| Absolute max | Supply -0.5 to 3.9 V; temperature -55 to +115 °C; Tj 125 °C; RF input +10 dBm |
| Operating temp | **-20 to +70 °C** |
| Digital load | 25 pF max |
| Current | Sleep 0.2 µA typ (1 µA max); idle 1.5 µA; standby 1.6 mA; synthesizer 5.8 mA; **TX 120 mA at +20 dBm, 87 mA at +17 dBm (PA_BOOST)**, 29 mA at +13 dBm, 20 mA at +7 dBm |
| Sensitivity | Better than -148 dBm (LoRa) |
| Chip version register | 0x42 = 0x11 (this datasheet); real silicon often reads 0x12 |
| Size, reflow, MSL | Package outline is Figure 56, p.120 (image). **[unread]** |

---

## Components without a datasheet in the repo

Only the Altium library files exist for these, so no specs are recorded here.
Add the PDFs to the part's folder under `Components/` and extend this file:

AMS1117-3.3 and AMS1117-5.0 (LDOs), WS2812B (RGB LED), PC817 / TLP521 optocouplers,
NCE6005AR (pyro MOSFET), S8050 / SS8550 transistors, NTS4001NT1G (buzzer MOSFET),
SS24 and 1N4148W diodes, ESD9B5.0ST5G (USB ESD), BWSMA-KE-Z001 (SMA),
CMI-1275C-050 buzzer, TYPE-C-31-M-12 (USB-C), XT30PW-M, JST-SH 4-pin,
Phoenix 1727036 (screw terminal), SKRPABE010 (switch), SML-LXT0805IW-TR (LED).

The two parts that matter most for a first check are the **AMS1117-3.3**
(dropout and max input voltage with the battery, thermal limit with the 3.3 V
load of Wi-Fi + LoRa TX) and the **NCE6005AR** and optocouplers (pyro path).

## Next steps

1. Read ESP32 Figure 13 (reflow profile) and the RFM95W package page (Figure 56)
   when the PDF viewer can render them, and fill the **[unread]** cells.
2. Download the PDFs listed above and add them next to their Altium libraries.
