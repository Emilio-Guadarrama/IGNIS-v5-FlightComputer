#include "telemetry_packet.h"
#include <string.h>

namespace ignis {

// Field mapping follows reference Telemetry.h:153-192.
void encodeTelemetry(const FlightSnapshot& s, uint32_t seq, TelemetryPacket& p) {
  memset(&p, 0, sizeof(p));
  p.seq = seq;
  p.timestamp_ms = s.timestamp_ms;
  p.state = (uint8_t)s.state;
  p.pressure_pa = s.pressure_pa;
  p.baro_alt_m = s.baro_alt_m;
  p.agl_m = s.agl_m;
  p.baro_temp_c = s.baro_temp_c;
  p.baro_valid = s.baro_valid ? 1 : 0;
  p.accel_x_g = s.accel_x_g;
  p.accel_y_g = s.accel_y_g;
  p.accel_z_g = s.accel_z_g;
  p.accel_total_g = s.accel_total_g;
  p.gyro_x_dps = s.gyro_x_dps;
  p.gyro_y_dps = s.gyro_y_dps;
  p.gyro_z_dps = s.gyro_z_dps;
  p.mpu_temp_c = s.imu_temp_c;
  p.imu_valid = s.imu_valid ? 1 : 0;
  p.gps_lat = s.gps_lat;
  p.gps_lon = s.gps_lon;
  p.gps_alt_m = s.gps_alt_m;
  p.gps_sats = s.gps_sats;
  p.gps_fix = s.gps_fix ? 1 : 0;
  p.battery_v = s.battery_v;
  // Raw channel bits; an invalid reading is reported as 0 here and flagged on
  // the dashboard/SD (the packet has no spare field for "invalid").
  p.pyro1_continuity = (s.cont.valid && s.cont.ch1) ? 1 : 0;
  p.pyro2_continuity = (s.cont.valid && s.cont.ch2) ? 1 : 0;
  p.pyro1_fired = s.pyro1_fired ? 1 : 0;
  p.pyro2_fired = s.pyro2_fired ? 1 : 0;
  p.sd_logging = (s.sd == SdStatus::LOGGING) ? 1 : 0;
  p.tilt_deg = s.tilt_deg;
  p.boot_count = s.boot_count;
  p.reset_reason = s.reset_reason;
  p.deploy_trigger = s.deploy_trigger;
}

size_t buildRadioHeadFrame(const TelemetryPacket& p, uint8_t* buf, size_t buflen) {
  if (buflen < kTelemetryFrameLen) return 0;
  buf[0] = 0xFF;  // TO: RH_BROADCAST_ADDRESS
  buf[1] = 0xFF;  // FROM: RH default this-address
  buf[2] = 0x00;  // ID
  buf[3] = 0x00;  // FLAGS
  memcpy(buf + kRhHeaderLen, &p, sizeof(p));
  return kTelemetryFrameLen;
}

}  // namespace ignis
