// =============================================================================
// IGNIS v5 Flight Computer: pins_v5.h
// Single source of truth for every GPIO and board-level hardware constant.
//
// SOURCE OF THE PIN MAP
//   PCB_Project_IGNIS-v5/Main.SchDoc (last saved 2026-09-28): the net labels
//   beside the ESP32-S3-WROOM-1-N16R8 symbol (MCU1), matched to the symbol pin
//   by row. Cross-checked against the module pad numbers in
//   "Project Logs for PCB_Project_IGNIS-v5/PCB_IGNIS-v5 PCB ECO 25-09-2026
//   05-40-45 p. m..LOG" (e.g. SD_CS -> MCU1-34 = IO41). Both sources agree.
//   This match is by label position, not by netlist export. Re-check against
//   an Altium netlist export before flight.
//
//   Tag legend:
//     // v5-confirmed: Main.SchDoc label + ECO pad number agree
//     // TODO(v5-unconfirmed): verify against v5 schematic
//
// MODULE RESTRICTIONS (ESP32-S3-WROOM-1-N16R8, octal PSRAM)
//   IO35, IO36, IO37 are used by the octal PSRAM. Never assign them.
//   Strapping pins: IO0 (BOOT), IO3 (JTAG source), IO45 (VDD_SPI), IO46.
//   IO19/IO20 = native USB D-/D+. IO43/IO44 = UART0 (debug JST-SH).
//   ADC2 is unusable while WiFi is on. Every analog input here is on ADC1.
// =============================================================================
#pragma once

// ---- I2C sensor bus (LSM6DSV32X + DPS368) ----------------------------------
#define PIN_I2C_SDA          21   // v5-confirmed: net SNS_I2C_SDA (MCU1-23)
#define PIN_I2C_SCL          38   // v5-confirmed: net SNS_I2C_SCL (MCU1-31)

// ---- Shared SPI bus (microSD + RFM95W) --------------------------------------
#define PIN_SPI_MOSI         11   // v5-confirmed: net SNS_SPI_MOSI (MCU1-19)
#define PIN_SPI_SCK          12   // v5-confirmed: net SNS_SPI_SCK  (MCU1-20)
#define PIN_SPI_MISO         13   // v5-confirmed: net SNS_SPI_MISO (MCU1-21)

// microSD. NOTE: CS and DETECT are SWAPPED relative to the reference firmware
// (IGNIS_FC_heavy_13/Config.h:23-24 had CS=42, DETECT=41).
#define PIN_SD_CS            41   // v5-confirmed: net SD_CS     (MCU1-34 = IO41)
#define PIN_SD_DETECT        42   // v5-confirmed: net SD_DETECT (MCU1-35 = IO42)

// RFM95W LoRa
#define PIN_LORA_CS          10   // v5-confirmed: net RFM95W_CS    (MCU1-18)
#define PIN_LORA_RST          8   // v5-confirmed: net RFM95W_RESET (MCU1-12)
#define PIN_LORA_DIO0         7   // v5-confirmed: net RFM95W_INT   (MCU1-7). Polled, no ISR (see lora_sx1276.cpp)

// ---- GNSS (u-blox SAM-M10Q) ------------------------------------------------
// Net names are device-centric (GPS_UART_TX = the module's TX), same
// convention as the reference (Config.h:29-38).
#define PIN_GPS_RX           17   // v5-confirmed: ESP32 RX <- net GPS_UART_TX (MCU1-10)
#define PIN_GPS_TX           18   // v5-confirmed: ESP32 TX -> net GPS_UART_RX (MCU1-11)
// Main.SchDoc labels these INT_GPS / RESET_GPS / PPS_GPS, but the RF_LoRa_&_GPS
// sheet uses GPS_PPS / GPS_SAFEBOOT_N. The label names differ between sheets.
#define PIN_GPS_EXTINT       15   // TODO(v5-unconfirmed): verify against v5 schematic (Main: INT_GPS, MCU1-8)
#define PIN_GPS_RESET_N      16   // TODO(v5-unconfirmed): verify against v5 schematic (Main: RESET_GPS, MCU1-9)
#define PIN_GPS_PPS          39   // TODO(v5-unconfirmed): verify against v5 schematic (Main: PPS_GPS, MCU1-32)
#define GPS_UART_BAUD      9600   // TODO(v5-unconfirmed): SAM-M10Q factory default 9600 8N1 (reference Sensors.h:533)

// ---- Pyro (2 channels, optically isolated) ---------------------------------
#define PIN_PYRO1_GATE        4   // v5-confirmed: net PYRO_CH1_GATE (MCU1-4)  drogue
#define PIN_PYRO2_GATE        5   // v5-confirmed: net PYRO_CH2_GATE (MCU1-5)  main
#define PIN_PYRO1_CONT        1   // v5-confirmed: net PYRO_CH1_CONT (MCU1-39)
#define PIN_PYRO2_CONT        2   // v5-confirmed: net PYRO_CH2_CONT (MCU1-38)
// Logic level that means "continuity present". Reference Pyro.h:18-22: the opto
// pulls CONT HIGH when the loop is intact.
#define PYRO_CONT_ACTIVE_LEVEL 1  // TODO(v5-unconfirmed): verify polarity on the v5 Pyros sheet / bench (inert load)

// ---- Power monitor ---------------------------------------------------------
#define PIN_VBAT_MON          3   // v5-confirmed: net VBAT_MON (MCU1-15), ADC1_CH2. Strapping pin, input only here
#define VBAT_DIVIDER_RATIO   0.3007f // TODO(v5-unconfirmed): reference value (Config.h:137). Read R values off v5 Power sheet
#define ADC_REF_V             3.3f   // used only if calibrated mV read is unavailable
#define ADC_MAX_COUNTS     4095.0f

// ---- Indicators ------------------------------------------------------------
#define PIN_WS2812_DATA       6   // v5-confirmed: net WS2812B_DATA (MCU1-6)
#define NUM_STATUS_LEDS       3   // README: 3x WS2812B
#define PIN_BUZZER           14   // v5-confirmed: net BUZZER (MCU1-22)

// ---- Misc ------------------------------------------------------------------
#define PIN_BOOT_BUTTON       0   // v5-confirmed: net BOOT (MCU1-27). Read after boot only (WiFi enable hold)
#define BOOT_BUTTON_ACTIVE_LEVEL 0  // TODO(v5-unconfirmed): button to GND with pull-up assumed
#define PIN_IMU_INT1          9   // TODO(v5-unconfirmed): Main label MPU6050_INT (MCU1-17); Sensors sheet uses IMU_INT1/IMU_INT2. Not used (IMU is polled)

// SD card-detect polarity. Reference DataLogger.h:21 assumed active-LOW.
#define SD_DETECT_ACTIVE_LOW  1   // TODO(v5-unconfirmed): verify on v5 Storage sheet

// ---- I2C addresses ---------------------------------------------------------
// Both possible straps are probed at boot; the one that answers is used.
#define LSM6DSV32X_ADDR_PRIMARY   0x6A  // TODO(v5-unconfirmed): SA0 strap (0x6A = SA0 low, 0x6B = SA0 high)
#define LSM6DSV32X_ADDR_ALT       0x6B
#define DPS368_ADDR_PRIMARY       0x77  // TODO(v5-unconfirmed): SDO strap (0x77 = SDO high/float, 0x76 = SDO low)
#define DPS368_ADDR_ALT           0x76

// ---- Compile-time pin sanity -----------------------------------------------
#define IGNIS_PIN_IS_PSRAM(p) ((p) == 35 || (p) == 36 || (p) == 37)
#if IGNIS_PIN_IS_PSRAM(PIN_I2C_SDA) || IGNIS_PIN_IS_PSRAM(PIN_I2C_SCL) ||      \
    IGNIS_PIN_IS_PSRAM(PIN_SPI_MOSI) || IGNIS_PIN_IS_PSRAM(PIN_SPI_SCK) ||     \
    IGNIS_PIN_IS_PSRAM(PIN_SPI_MISO) || IGNIS_PIN_IS_PSRAM(PIN_SD_CS) ||       \
    IGNIS_PIN_IS_PSRAM(PIN_LORA_CS) || IGNIS_PIN_IS_PSRAM(PIN_PYRO1_GATE) ||   \
    IGNIS_PIN_IS_PSRAM(PIN_PYRO2_GATE) || IGNIS_PIN_IS_PSRAM(PIN_PYRO1_CONT) || \
    IGNIS_PIN_IS_PSRAM(PIN_PYRO2_CONT) || IGNIS_PIN_IS_PSRAM(PIN_GPS_PPS)
#error "pins_v5.h: a pin is assigned to IO35-37, which the octal PSRAM uses on N16R8."
#endif
#if PIN_PYRO1_GATE == PIN_PYRO2_GATE || PIN_PYRO1_CONT == PIN_PYRO2_CONT
#error "pins_v5.h: pyro channels share a pin."
#endif
