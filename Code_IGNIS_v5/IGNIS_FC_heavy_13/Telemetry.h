/*
  ================================================================
  IGNIS v2 Flight Computer — Telemetry.h
  RFM95W LoRa downlink at 915.0 MHz, sync word 0x12, SF9, BW125kHz, CR4/5.

  TIME-ON-AIR MATH (Semtech AN1200.22) — read before changing packet
  size or send rate:
    Ts = 2^SF / BW = 2^9 / 125000 = 4.10 ms/symbol
    ToA(37B) ~267ms, ToA(41B) ~288ms, ToA(~98B full packet) ~530ms
  At SF9, transmit time is NOT "tens of ms" (an earlier comment here
  was wrong) — it's dominated by preamble + symbol count, and scales
  in ~4-5 byte steps due to the ceiling in the symbol-count formula.
  A full-sensor packet physically cannot go out every 200ms; sending
  is rate-limited below to respect this, and is NON-BLOCKING (skips a
  cycle rather than stalling the 50Hz flight loop) rather than forcing
  a cadence the radio can't physically sustain.
  ================================================================
*/

#pragma once
#include <SPI.h>
#include <RH_RF95.h>
#include "Config.h"
#include "FlightData.h"

// RH_RF95's default constructor already uses RadioHead's internal
// hardware_spi singleton, which wraps Arduino's global `SPI` object — the
// SAME global SPI that DataLogger.h calls SPI.begin() on. That's how the
// bus-sharing with SD actually happens on this hardware; no custom
// SPIClass/RHHardwareSPI wrapper is needed (the installed RadioHead build
// has no constructor that accepts one anyway — only Frequency/BitOrder/
// DataMode). DataLogger.h's SPI.begin() must run before initTelemetry()
// (already the case in IGNIS_FC.ino's setup() include/call order).
static RH_RF95 rf95(RFM95_CS_PIN, RFM95_DIO0_PIN);

#if !RFM95_SHARES_SD_SPI_BUS
  #warning "RFM95_SHARES_SD_SPI_BUS is false but Telemetry.h still relies on \
the global SPI object initialized in DataLogger.h. If your RFM95W is truly \
on a separate bus, this file needs its own SPI.begin() call with dedicated \
pins — ask before assuming, this hasn't been built for that case."
#endif

static bool loraOk = false;
static bool lastTxOk = false;          // did the most recent send() succeed
static unsigned long lastTxMs = 0;     // millis() of the most recent send attempt
static uint32_t txCount = 0;           // total send attempts since boot

// Full-sensor packet — same information the FC's own Dashboard.h shows,
// so the ground station can render an equivalent dashboard. ~98 bytes;
// see the ToA math above for why this can't go out at the old 200ms/5Hz
// cadence. IMPORTANT: this struct's layout must match the ground
// station's copy EXACTLY (same fields, same order, same
// __attribute__((packed))) or received bytes decode into garbage. If
// you change this struct, update IGNIS_GroundStation.ino's copy too.
struct __attribute__((packed)) TelemetryPacket {
  uint32_t seq;              // increments every send attempt — lets the ground station detect drops
  uint32_t timestamp_ms;
  uint8_t  state;

  float    pressure_pa;
  float    baro_alt_m;
  float    agl_m;
  float    baro_temp_c;
  uint8_t  baro_valid;

  float    accel_x_g;
  float    accel_y_g;
  float    accel_z_g;
  float    accel_total_g;
  float    gyro_x_dps;
  float    gyro_y_dps;
  float    gyro_z_dps;
  float    mpu_temp_c;
  uint8_t  imu_valid;

  double   gps_lat;
  double   gps_lon;
  float    gps_alt_m;
  uint8_t  gps_sats;
  uint8_t  gps_fix;

  float    battery_v;

  uint8_t  pyro1_continuity;
  uint8_t  pyro2_continuity;
  uint8_t  pyro1_fired;
  uint8_t  pyro2_fired;

  uint8_t  sd_logging;       // is the FC currently writing to SD? (see DataLogger.h's sdReady)

  // --- v11 forensic fields. APPENDED AT THE END on purpose: older ground
  // station builds that expect the v10 layout will still decode every field
  // up to sd_logging correctly, they just won't see these four.
  // You MUST still update IGNIS_GroundStation.ino and the Python parser -
  // see GroundStation_TelemetryPacket_v11.h shipped alongside this sketch.
  // Size impact: +8 bytes (~98 -> ~106). ToA at SF9/BW125/CR4-5 goes from
  // ~530 ms to ~550 ms, still inside TELEMETRY_INTERVAL_MS_FLT = 600 ms, but
  // the margin is thin. If you need it back, drop mpu_temp_c from this packet
  // (it stays in the SD log regardless).
  float    tilt_deg;
  uint16_t boot_count;
  uint8_t  reset_reason;
  uint8_t  deploy_trigger;
};

static uint32_t txSeq = 0;
static unsigned long lastTxAttemptMs = 0;

static bool initTelemetry() {
  pinMode(RFM95_RST_PIN, OUTPUT);
  digitalWrite(RFM95_RST_PIN, HIGH);
  delay(10);
  digitalWrite(RFM95_RST_PIN, LOW);
  delay(10);
  digitalWrite(RFM95_RST_PIN, HIGH);
  delay(10);

  loraOk = rf95.init();
  if (!loraOk) {
    Serial.println("[LORA] rf95.init() FAILED.");
    return false;
  }

  rf95.setFrequency(LORA_FREQ_MHZ);
  rf95.setTxPower(LORA_TX_POWER_DBM, false); // false = PA_BOOST pin
  rf95.setSpreadingFactor(LORA_SF);
  rf95.setSignalBandwidth(LORA_BW_KHZ * 1000);
  rf95.setCodingRate4(LORA_CR_DENOM);
  // setSyncWord() isn't present on the installed RadioHead build's RH_RF95
  // class — write RegSyncWord (0x39) directly instead, same pattern as the
  // RSSI/SNR reads below. This is a standard SX1276 register, stable across
  // RadioHead versions even when the convenience wrapper isn't.
  rf95.spiWrite(0x39, LORA_SYNC_WORD);

  return true;
}

static void sendTelemetry(const FlightData& d) {
  if (!loraOk) return;

  // NON-BLOCKING: if the previous transmission is still on air (ToA up
  // to ~530ms for this packet size, see header math), skip this cycle
  // instead of blocking the 50Hz flight loop waiting for it to finish.
  // RH_RF95::send() would otherwise block internally on its own if
  // called again mid-transmission — checking mode() here lets us skip
  // gracefully instead. Effective delivered rate ends up governed by
  // ToA, not by TELEMETRY_INTERVAL_MS_FLT/PAD — those intervals are an
  // upper bound on attempt rate, not a guarantee every attempt sends.
  if (rf95.mode() == RHGenericDriver::RHModeTx) {
    return;
  }

  TelemetryPacket pkt;
  pkt.seq            = txSeq++;
  pkt.timestamp_ms   = d.timestamp_ms;
  pkt.state          = (uint8_t)d.state;

  pkt.pressure_pa    = d.pressure_pa;
  pkt.baro_alt_m     = d.baro_alt_m;
  pkt.agl_m          = d.agl_m;
  pkt.baro_temp_c    = d.baro_temp_c;
  pkt.baro_valid     = d.baro_reading_valid ? 1 : 0;

  pkt.accel_x_g      = d.accel_x_g;
  pkt.accel_y_g      = d.accel_y_g;
  pkt.accel_z_g      = d.accel_z_g;
  pkt.accel_total_g  = d.accel_total_g;
  pkt.gyro_x_dps     = d.gyro_x_dps;
  pkt.gyro_y_dps     = d.gyro_y_dps;
  pkt.gyro_z_dps     = d.gyro_z_dps;
  pkt.mpu_temp_c     = d.mpu_temp_c;
  pkt.imu_valid      = d.imu_reading_valid ? 1 : 0;

  pkt.gps_lat        = d.gps_lat;
  pkt.gps_lon        = d.gps_lon;
  pkt.gps_alt_m      = d.gps_alt_m;
  pkt.gps_sats       = d.gps_sats;
  pkt.gps_fix        = d.gps_fix ? 1 : 0;

  pkt.battery_v      = d.battery_v;

  pkt.pyro1_continuity = d.pyro1_continuity ? 1 : 0;
  pkt.pyro2_continuity = d.pyro2_continuity ? 1 : 0;
  pkt.pyro1_fired       = d.pyro1_fired ? 1 : 0;
  pkt.pyro2_fired       = d.pyro2_fired ? 1 : 0;

  pkt.sd_logging     = sdReady ? 1 : 0;

  pkt.tilt_deg       = d.tilt_deg;
  pkt.boot_count     = d.boot_count;
  pkt.reset_reason   = d.reset_reason;
  pkt.deploy_trigger = d.deploy_trigger;

  // v13: bracket with the RTC breadcrumb (see FlightData.h) so a reset
  // during rf95.send() itself is visible at the next boot. NOTE: this
  // does NOT add a bounded timeout inside send() — that would require
  // auditing RadioHead's internal SPI/FIFO wait loops, which weren't
  // available to check this session. This call is a real candidate for a
  // watchdog reset (shares the SD's SPI bus — see DataLogger.h) if a
  // marginal/unresponsive radio module ever makes it spin; flag this as
  // a followup rather than a verified fix.
  rtcSetLastOp(OP_RADIO_SEND);
  bool sent = rf95.send((uint8_t*)&pkt, sizeof(pkt));
  rtcSetLastOp(OP_IDLE);
  // Deliberately NOT calling waitPacketSent() here — RadioHead completes
  // the transmission in the background via the DIO0 interrupt regardless
  // of whether we wait for it. "sent" only confirms the packet was
  // successfully handed to the radio's FIFO, not that it was received —
  // this project has no ACK/retry scheme, same as before.
  lastTxOk = sent;
  lastTxMs = millis();
  lastTxAttemptMs = lastTxMs;
  txCount++;
}

// RSSI/SNR reads were REMOVED from here (used to be readRssiDbm()/
// readSnrDb(), called from IGNIS_FC.ino's main loop). CONFIRMED via
// bench test: they always returned the register reset value (-157dBm /
// 0.0dB, completely unvarying across 50+ samples) because this radio
// only ever transmits — RegRssiValue/RegPktSnrValue only update in RX
// mode. Real RSSI/SNR now comes from IGNIS_GroundStation.ino, which is
// the actual receiver and where these registers mean something.
// FlightData.h's rssi_dbm/snr_db fields are kept (still in the CSV/
// telemetry struct for now) but simply stay at 0 on the FC side.
