/*
  ================================================================
  IGNIS v2 Flight Computer — FlightData.h
  Single canonical data struct shared by Sensors, FlightState, Pyro,
  Telemetry, and DataLogger — avoids field drift between modules.
  ================================================================
*/

#pragma once
#include <Arduino.h>
#include "esp_attr.h"   // RTC_DATA_ATTR — see RtcFlightState below (v13)

enum FlightState_t : uint8_t {
  STATE_PAD_IDLE = 0,
  STATE_ARMED,
  STATE_BOOST,
  STATE_COAST,
  STATE_APOGEE,
  STATE_DROGUE_DESCENT,
  STATE_MAIN_DESCENT,
  STATE_LANDED
};

inline const char* flightStateName(FlightState_t s) {
  switch (s) {
    case STATE_PAD_IDLE:        return "PAD_IDLE";
    case STATE_ARMED:           return "ARMED";
    case STATE_BOOST:           return "BOOST";
    case STATE_COAST:           return "COAST";
    case STATE_APOGEE:          return "APOGEE";
    case STATE_DROGUE_DESCENT:  return "DROGUE_DESCENT";
    case STATE_MAIN_DESCENT:    return "MAIN_DESCENT";
    case STATE_LANDED:          return "LANDED";
    default:                    return "UNKNOWN";
  }
}

struct FlightData {
  unsigned long timestamp_ms = 0;
  FlightState_t state = STATE_PAD_IDLE;

  // BMP180
  float pressure_pa   = 0;
  float baro_alt_m    = 0;   // absolute (sea-level reference)
  float agl_m         = 0;   // computed directly from pressure ratio vs ground pressure — see FlightState.h
  float baro_temp_c   = 0;
  bool  baro_reading_valid = false;  // false = last read rejected (I2C fail or out-of-range) — fields above hold last-known-good, not garbage

  // SHT40 removed — not used on this build.

  // MPU6050
  float accel_x_g = 0, accel_y_g = 0, accel_z_g = 0;
  float accel_total_g = 0;              // raw magnitude — used by the FSM (fast reaction, no added lag)
  float accel_total_g_filtered = 0;     // lightly smoothed (EMA) — display/logging only, NOT used by the FSM
  float gyro_x_dps = 0, gyro_y_dps = 0, gyro_z_dps = 0;   // bias-corrected against boot-time calibration
  float mpu_temp_c = 0;                 // MPU6050's own temp sensor — free from the same burst read, useful cross-check
  bool  imu_reading_valid = false;      // false = last read rejected (I2C fail) — fields above hold last-known-good

  // GPS
  double gps_lat = 0, gps_lon = 0;
  float  gps_alt_m = 0;
  uint8_t gps_sats = 0;
  bool    gps_fix = false;

  // Power / RF
  float battery_v = 0;
  int   rssi_dbm = 0;
  float snr_db = 0;

  // Pyro
  bool pyro1_continuity = false, pyro2_continuity = false;
  bool pyro1_fired = false, pyro2_fired = false;

  // --- Forensics / diagnostics (added v11, after FLIGHT072) ---
  float    tilt_deg       = 0;   // integrated-gyro tilt estimate. DRIFTS. Diagnostic only, never gates pyro.
  uint16_t boot_count     = 0;   // NVS-persisted. An increment mid-flight == the FC rebooted mid-flight.
  uint8_t  reset_reason   = 0;   // esp_reset_reason(); 9 == ESP_RST_BROWNOUT on ESP32-S3, not 6 (see v12 note)
  uint8_t  deploy_trigger = 0;   // 0=none  1=barometric apogee  2=TIMEOUT FAILSAFE

  // --- Resume forensics (added v13, after FLIGHT051/052) ---
  bool resumed_this_boot  = false;  // true if setup() resumed mid-flight state instead of cold-starting into STATE_ARMED — see RtcFlightState below
};

// Timestamp of the most recent pyro fire event. Declared HERE rather than in
// Pyro.h on purpose: DataLogger.h needs it for the post-fire forced-flush
// window, and DataLogger.h is included BEFORE Pyro.h in the main sketch.
// Written by fireChannel(), read by logFlightData().
static unsigned long lastPyroEventMs = 0;

// ================================================================
// RTC-persisted flight session state (v13)
// ----------------------------------------------------------------
// Added after FLIGHT051/052: setup() had no concept of "we already
// launched," so any in-flight reset (including the confirmed
// ESP_RST_INT_WDT reset between those two files) restarted the whole
// state machine from STATE_ARMED, requiring boost/apogee to be
// re-detected from scratch and losing the descent as one continuous
// record. This struct is what setup() checks to resume instead.
//
// RTC_DATA_ATTR places it in the ESP32's "RTC slow memory," which is
// NOT cleared by a software/panic/watchdog reset (only by a genuine
// power-on / full power cycle of the RTC domain) — that's the entire
// mechanism this depends on. It is NOT guaranteed to survive every
// brownout severity: if VBAT sags hard enough to fully power-cycle the
// RTC domain, this is lost too, and the FC falls back to the old
// cold-start behavior automatically (see the magic-number check in
// IGNIS_FC_heavy_13.ino's setup()). Bench-test which case this
// project's brownout protection actually produces before flying on the
// assumption that resume always works.
//
// Deliberately NOT attempting to reconstruct absolute mission-elapsed
// time across the reset: millis() restarts at 0 on every reboot, so
// per-phase timers (coastEnteredMs, landedStableSinceMs, etc., all in
// FlightState.h) are simply re-armed fresh on resume rather than
// carried across the reset boundary. This is a conservative
// simplification, not a correctness bug — it can only make a failsafe
// timer fire a little later than it would have with zero downtime,
// never early, and it doesn't affect the primary sample-based
// apogee/landed detectors at all, which don't depend on elapsed time.
// ================================================================

#define RTC_FLIGHT_MAGIC  0x49474E31UL   // 'IGN1' — bump this if RtcFlightState's layout changes, so a firmware update never misreads a stale record left by an older build

enum RtcLastOp_t : uint8_t {
  OP_IDLE = 0,
  OP_I2C_READ,
  OP_SD_WRITE,
  OP_SD_MOUNT,
  OP_RADIO_SEND,
  OP_PYRO_FIRE,
  OP_MPU_CAL,
};

inline const char* rtcLastOpName(uint8_t op) {
  switch (op) {
    case OP_IDLE:       return "idle";
    case OP_I2C_READ:   return "i2c_read";
    case OP_SD_WRITE:   return "sd_write";
    case OP_SD_MOUNT:   return "sd_mount";
    case OP_RADIO_SEND: return "radio_send";
    case OP_PYRO_FIRE:  return "pyro_fire";
    case OP_MPU_CAL:    return "mpu_cal";
    default:            return "unknown";
  }
}

struct RtcFlightState {
  uint32_t magic;                 // RTC_FLIGHT_MAGIC when this record is usable; anything else means "no valid resume data"
  uint8_t  phase;                 // mirrors FlightState_t, as of the last rtcSyncFlightState() call (FlightState.h)
  bool     pyro1_fired;
  bool     pyro2_fired;
  float    maxAglSinceBoost;
  float    groundPressurePa;
  bool     groundPressureCaptured;
  bool     mpuCalibValid;
  float    accelBiasX_g, accelBiasY_g, accelBiasZ_g;
  float    gyroBiasX_dps, gyroBiasY_dps, gyroBiasZ_dps;
  char     logFileName[20];       // e.g. "/FLIGHT051.CSV" — reopened (appended, not overwritten) on resume so pre- and post-reset data land in one file
  uint8_t  lastOp;                // RtcLastOp_t — breadcrumb: what the FC was doing right before the most recent reset. Printed at boot next to reset_reason.
};

RTC_DATA_ATTR static RtcFlightState rtcState;

// Call before any operation that's a realistic candidate for stalling the
// loop long enough to trip the interrupt watchdog (I2C transaction, SD
// write/mount, radio send, pyro fire). Cheap — just an RTC SRAM write —
// so callers bracket the risky call with OP_x then OP_IDLE rather than
// trying to be clever about when it matters. Turns any future reset from
// "reset_reason tells you THAT it happened" into "...and WHAT it was
// touching when it did," without needing telemetry reconstruction like
// FLIGHT051/052 required.
static inline void rtcSetLastOp(uint8_t op) {
  rtcState.lastOp = op;
}
