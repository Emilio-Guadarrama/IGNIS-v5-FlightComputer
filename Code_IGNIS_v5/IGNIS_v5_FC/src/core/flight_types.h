// =============================================================================
// IGNIS v5: flight_types.h  (pure C++, host-testable)
// Shared enums and the RAM snapshot that every consumer reads (SD logger,
// LoRa, dashboard). Same idea as the reference FlightData struct
// (IGNIS_FC_heavy_13/FlightData.h:38-82): one canonical record, no field drift.
// =============================================================================
#pragma once
#include <stdint.h>

namespace ignis {

// Numeric values 0..7 match the reference enum (FlightData.h:13-22) and the
// ground-station parser (telemetry_parser_v12.py:39-42), so the telemetry
// "state" byte keeps its meaning. SAFE uses slot 0 (formerly PAD_IDLE).
// RECOVERY (8) is new in v5.
enum class FlightState : uint8_t {
  SAFE            = 0,  // pad, no continuity detected on any channel
  ARMED           = 1,  // pad, hardware armed (continuity on >=1 channel, or reading invalid)
  BOOST           = 2,
  COAST           = 3,
  APOGEE          = 4,  // same-tick transient (reference FlightState.h:130-139)
  DROGUE_DESCENT  = 5,
  MAIN_DESCENT    = 6,
  LANDED          = 7,
  RECOVERY        = 8,  // booted after a reset mid-flight / post-fire: log + telemetry only
};

const char* flightStateName(FlightState s);

inline bool isPadState(FlightState s) {
  return s == FlightState::SAFE || s == FlightState::ARMED;
}
// "Flight detected" for gating WiFi/OTA: anything that is not a pad state.
inline bool isFlightOrAfter(FlightState s) { return !isPadState(s); }

enum DeployTrigger : uint8_t {
  DEPLOY_NONE = 0,
  DEPLOY_BARO_APOGEE = 1,
  DEPLOY_TIMEOUT_FAILSAFE = 2,
};

// Continuity as read from the opto outputs. valid=false means the read itself
// could not be trusted (e.g. pin not configured). Fail closed: treated as armed.
struct Continuity {
  bool ch1 = false;
  bool ch2 = false;
  bool valid = false;
};

inline bool continuityIndicatesArmed(const Continuity& c) {
  return !c.valid || c.ch1 || c.ch2;
}

enum class WifiStatus : uint8_t { DISABLED_BUILD = 0, OFF = 1, AP_ON = 2, LATCHED_OFF = 3 };
enum class SdStatus : uint8_t { NO_CARD = 0, LOGGING = 1, ERROR = 2, CLOSED = 3 };

// The one RAM snapshot, written by the flight task, read everywhere else.
struct FlightSnapshot {
  uint32_t timestamp_ms = 0;
  FlightState state = FlightState::SAFE;

  // Barometer (DPS368)
  float pressure_pa = 0, baro_alt_m = 0, agl_m = 0, baro_temp_c = 0;
  bool baro_valid = false;

  // IMU (LSM6DSV32X)
  float accel_x_g = 0, accel_y_g = 0, accel_z_g = 0, accel_total_g = 0;
  float accel_total_g_filtered = 0;
  float gyro_x_dps = 0, gyro_y_dps = 0, gyro_z_dps = 0;
  float imu_temp_c = 0;
  bool imu_valid = false;

  // GNSS
  double gps_lat = 0, gps_lon = 0;
  float gps_alt_m = 0;
  uint8_t gps_sats = 0;
  bool gps_fix = false;

  // Power
  float battery_v = 0;

  // Pyro
  Continuity cont;
  bool pyro1_fired = false, pyro2_fired = false;

  // Diagnostics
  float tilt_deg = 0;          // integrated gyro, drifts, NEVER gates pyro (reference Config.h:247-258)
  float max_agl_m = 0;
  float baro_vel_mps = 0;
  uint16_t boot_count = 0;
  uint8_t reset_reason = 0;
  uint8_t deploy_trigger = DEPLOY_NONE;
  bool recovery_boot = false;
  bool post_fire_latched = false;

  // Subsystem status (filled by main from task heartbeats)
  SdStatus sd = SdStatus::NO_CARD;
  bool lora_ok = false;
  bool lora_last_tx_ok = false;
  uint32_t lora_tx_count = 0;
  uint32_t lora_skipped = 0;
  uint32_t sd_rows_dropped = 0;
  WifiStatus wifi = WifiStatus::OFF;
};

}  // namespace ignis
