#include "csv_row.h"
#include <stdio.h>

namespace ignis {

const char kCsvHeader[] =
    "timestamp_ms,state,"
    "pressure_pa,baro_alt_m,agl_m,baro_temp_c,baro_valid,"
    "accel_x_g,accel_y_g,accel_z_g,accel_total_g,accel_total_g_filtered,gyro_x_dps,gyro_y_dps,gyro_z_dps,imu_temp_c,imu_valid,"
    "gps_lat,gps_lon,gps_alt_m,gps_sats,gps_fix,"
    "battery_v,rssi_dbm,snr_db,"
    "pyro1_cont,pyro2_cont,pyro1_fired,pyro2_fired,"
    "tilt_deg,boot_count,reset_reason,deploy_trigger,"
    // v5 columns
    "boot_mode,post_fire,pyro1_attempts,pyro2_attempts,cont_valid,max_agl_m,baro_vel_mps,"
    "lora_tx_count,lora_last_ok,lora_skipped,sd_rows_dropped,wifi";

size_t buildCsvRow(const FlightSnapshot& s, char* buf, size_t len) {
  int n = snprintf(buf, len,
      "%lu,%u,"
      "%.1f,%.2f,%.2f,%.2f,%d,"
      "%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%d,"
      "%.6f,%.6f,%.2f,%u,%d,"
      "%.2f,0,0.0,"                       // rssi/snr: TX-only radio (reference Telemetry.h:216-224)
      "%d,%d,%d,%d,"
      "%.1f,%u,%u,%u,"
      "%u,%d,%u,%u,%d,%.1f,%.2f,"
      "%lu,%d,%lu,%lu,%u",
      (unsigned long)s.timestamp_ms, (unsigned)s.state,
      s.pressure_pa, s.baro_alt_m, s.agl_m, s.baro_temp_c, s.baro_valid ? 1 : 0,
      s.accel_x_g, s.accel_y_g, s.accel_z_g, s.accel_total_g, s.accel_total_g_filtered,
      s.gyro_x_dps, s.gyro_y_dps, s.gyro_z_dps, s.imu_temp_c, s.imu_valid ? 1 : 0,
      s.gps_lat, s.gps_lon, s.gps_alt_m, (unsigned)s.gps_sats, s.gps_fix ? 1 : 0,
      s.battery_v,
      s.cont.ch1 ? 1 : 0, s.cont.ch2 ? 1 : 0, s.pyro1_fired ? 1 : 0, s.pyro2_fired ? 1 : 0,
      s.tilt_deg, (unsigned)s.boot_count, (unsigned)s.reset_reason, (unsigned)s.deploy_trigger,
      (unsigned)s.boot_mode, s.post_fire_latched ? 1 : 0, (unsigned)s.pyro_attempts[0],
      (unsigned)s.pyro_attempts[1], s.cont.valid ? 1 : 0,
      s.max_agl_m, s.baro_vel_mps,
      (unsigned long)s.lora_tx_count, s.lora_last_tx_ok ? 1 : 0,
      (unsigned long)s.lora_skipped, (unsigned long)s.sd_rows_dropped, (unsigned)s.wifi);
  if (n < 0 || (size_t)n >= len) return 0;
  return (size_t)n;
}

}  // namespace ignis
