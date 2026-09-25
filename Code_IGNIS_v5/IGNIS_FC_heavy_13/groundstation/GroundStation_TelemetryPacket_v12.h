/*
  ================================================================
  IGNIS Ground Station — TelemetryPacket v11
  Ignitia Rocket Lab — LASC 2026

  Drop-in replacement for the TelemetryPacket struct inside
  IGNIS_GroundStation.ino. This MUST match Telemetry.h on the flight
  computer byte for byte.

  The struct is __attribute__((packed)). A mismatch does NOT throw an
  error — it silently decodes every field after the divergence point
  into garbage. If the ground station starts showing plausible-looking
  but wrong altitudes, check this file first.

  v11 appends four fields at the END of the v10 layout:
      tilt_deg, boot_count, reset_reason, deploy_trigger

  Payload sizes (verified):
      v10 = 90 bytes   ToA @ SF9/BW125/CR4-5 = 545.8 ms
      v11 = 98 bytes   ToA                    = 566.3 ms
  FC transmit interval in flight is 600 ms, so v11 fits with 34 ms margin.
  ================================================================
*/

#pragma once
#include <Arduino.h>

#define IGNIS_PACKET_VERSION 11

struct __attribute__((packed)) TelemetryPacket {
  uint32_t seq;
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

  uint8_t  sd_logging;

  // ---- v11 ----
  float    tilt_deg;        // integrated-gyro estimate. Drifts. Display only.
  uint16_t boot_count;      // if this INCREMENTS mid-flight, the FC rebooted
  uint8_t  reset_reason;    // esp_reset_reason(); 6 == ESP_RST_BROWNOUT
  uint8_t  deploy_trigger;  // 0=none 1=baro apogee 2=TIMEOUT FAILSAFE
};

static_assert(sizeof(TelemetryPacket) == 98,
              "TelemetryPacket is not 98 bytes — layout does not match FC v11");

// ---------------------------------------------------------------
// Decoding helpers — use these in the ground station display.
// ---------------------------------------------------------------

// CORRECTED in v12. The earlier table in this file was wrong: it mapped 6 to
// BROWNOUT. In esp_reset_reason_t, 6 is TASK_WDT and BROWNOUT is 9.
inline const char* resetReasonName(uint8_t r) {
  switch (r) {
    case  0: return "UNKNOWN";
    case  1: return "POWERON";
    case  2: return "EXT";
    case  3: return "SW";
    case  4: return "PANIC";
    case  5: return "INT_WDT";
    case  6: return "TASK_WDT";
    case  7: return "WDT";
    case  8: return "DEEPSLEEP";
    case  9: return "BROWNOUT";      // <-- the one that matters
    case 10: return "SDIO";
    case 11: return "USB";
    case 12: return "JTAG";
    case 15: return "CPU_LOCKUP";
    default: return "OTHER";
  }
}

inline const char* deployTriggerName(uint8_t t) {
  switch (t) {
    case 1:  return "baro-apogee";
    case 2:  return "TIMEOUT-FAILSAFE";
    default: return "none";
  }
}

/*
  RECOMMENDED GROUND STATION ALARMS (v11)

  0. battery_v below 7.80 V while state <= ARMED
       -> pad NO-GO. FLIGHT072 flew at 7.73 V (~45% SoC on a 2S pack) and
          the FC reset mid-flight during the pyro pulse. The pulley tests
          that did NOT reset ran at 8.10-8.12 V, so that comparison does not
          settle the question. Do not fly at half charge either way.

  1. boot_count increases while state >= STATE_BOOST
       -> the flight computer rebooted in flight. This is what happened on
          FLIGHT072 and the only reason we caught it was the seq counter
          resetting to 0. Make it an explicit, loud alarm now.

  2. pyro1_continuity == 0 || pyro2_continuity == 0  while state <= ARMED
       -> NO-GO. FLIGHT072 sat on the pad for 382 s with CH1 open and flew
          anyway. The ground station should refuse to show a green ready
          state under this condition.

  3. deploy_trigger == 2
       -> the barometric apogee chain never closed and the timeout failsafe
          fired instead. Recovery may still have worked; the barometer still
          needs investigating.

  4. seq gap > 5
       -> FLIGHT072/GSLOG003 saw 40.2 % packet loss (144 of 241 expected)
          with one 61-packet blackout, at a pad RSSI of -123.6 dBm and SNR
          of -12.9 dB. SF9 demodulation floor is around -15 dB SNR, so the
          link was roughly 2 dB from total loss during the entire count.
          Surface link margin live, not after the flight.
*/
