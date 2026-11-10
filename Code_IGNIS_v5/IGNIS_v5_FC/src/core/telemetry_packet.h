// =============================================================================
// IGNIS v5: telemetry_packet.h  (pure C++, host-testable)
//
// Byte-identical to the reference packet (IGNIS_FC_heavy_13/Telemetry.h:55-104)
// and the ground station (GroundStation_TelemetryPacket_v12.h:30-74,
// telemetry_parser_v12.py _FMT_V11). DO NOT reorder or resize fields: the
// ground station decodes by position. Size is pinned by static_assert.
//
// v5 changes with NO layout change:
//   - "mpu_temp_c" now carries the LSM6DSV32X die temperature (same float slot)
//   - "state" may be 8 (RECOVERY). Old parsers show UNKNOWN(8) and still decode.
//   - "reset_reason" was already present (requirement A satisfied in-format).
// =============================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "flight_types.h"

namespace ignis {

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
  float    mpu_temp_c;       // v5: IMU (LSM6DSV32X) temperature
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

  float    tilt_deg;
  uint16_t boot_count;
  uint8_t  reset_reason;
  uint8_t  deploy_trigger;
};

static_assert(sizeof(TelemetryPacket) == 98,
              "TelemetryPacket must stay 98 bytes (ground-station parser v11/v12)");
static_assert(offsetof(TelemetryPacket, state) == 8, "layout drift");
static_assert(offsetof(TelemetryPacket, gps_lat) == 59, "layout drift");
static_assert(offsetof(TelemetryPacket, battery_v) == 81, "layout drift");
static_assert(offsetof(TelemetryPacket, sd_logging) == 89, "layout drift");
static_assert(offsetof(TelemetryPacket, reset_reason) == 96, "layout drift");

void encodeTelemetry(const FlightSnapshot& s, uint32_t seq, TelemetryPacket& out);

// RadioHead RH_RF95 framing: 4 header bytes (TO, FROM, ID, FLAGS) precede the
// payload in the LoRa frame. The ground station receives with RadioHead, so
// the in-tree SX1276 driver must emit the same header or the receiver strips
// 4 bytes of our packet. Defaults match RH_RF95 (broadcast, from 0xFF, 0, 0).
// ASSUMPTION(v5): verify on the bench that the existing GS decodes 98 bytes.
constexpr size_t kRhHeaderLen = 4;
constexpr size_t kTelemetryFrameLen = kRhHeaderLen + sizeof(TelemetryPacket);
size_t buildRadioHeadFrame(const TelemetryPacket& p, uint8_t* buf, size_t buflen);

}  // namespace ignis
