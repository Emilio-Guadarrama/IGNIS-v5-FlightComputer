/*
  ================================================================
  IGNIS v2 Flight Computer — Dashboard.h
  Periodic serial status block: sensors, GPS, telemetry, pyro, SD, power.

  This is purely a serial print — it does not affect the FSM, sensor
  loop, or logging. Include LAST in IGNIS_FC.ino: it reads static state
  owned by Sensors.h / DataLogger.h / Telemetry.h. Arduino builds the
  whole sketch as one translation unit, so these `static` globals from
  other headers are visible here as long as this file is included after
  them (already the case — see IGNIS_FC.ino include order).

  Call updateDashboard(fd, millis()) once per main loop() iteration; it
  self-throttles to DASHBOARD_INTERVAL_MS.
  ================================================================
*/

#pragma once
#include <Arduino.h>
#include "Config.h"
#include "FlightData.h"

static unsigned long lastDashboardMs = 0;

static void printDashboard(const FlightData& d) {
  unsigned long now = millis();

  Serial.println();
  Serial.println(F("============================================================"));
  Serial.printf( " IGNIS v2  |  STATE: %-16s  |  t+%lu ms\n",
                 flightStateName(d.state), d.timestamp_ms);
  Serial.println(F("------------------------------------------------------------"));
  Serial.println(F(" LEDS   L0=Sensors  L1=Comms/SD  L2=Arm/Phase   (green=OK/safe  yellow=degraded  red=DANGER/armed)"));
  Serial.printf( "        L0:%-22s L1:%-22s L2:%-26s\n",
                ledStatusText(0, d), ledStatusText(1, d), ledStatusText(2, d));
  Serial.println(F("------------------------------------------------------------"));

  Serial.printf(" BARO   AGL: %8.2f m   ABS: %8.2f m   T: %6.2f C   [%s]  ground-cal:%s\n",
                d.agl_m, d.baro_alt_m, d.baro_temp_c,
                d.baro_reading_valid ? "OK" : "STALE",
                groundPressureCaptured ? "done" : "waiting");

  Serial.printf(" IMU    accel X:%6.2f Y:%6.2f Z:%6.2f |%6.2f|g (filt:%6.2f)  gyro X:%7.1f Y:%7.1f Z:%7.1f dps  T:%5.1fC  [%s]\n",
                d.accel_x_g, d.accel_y_g, d.accel_z_g, d.accel_total_g, d.accel_total_g_filtered,
                d.gyro_x_dps, d.gyro_y_dps, d.gyro_z_dps, d.mpu_temp_c,
                d.imu_reading_valid ? "OK" : "STALE");

  if (d.gps_fix) {
    Serial.printf(" GPS    FIX  sats:%2u  lat:%10.6f  lon:%11.6f  alt:%8.2f m\n",
                  d.gps_sats, d.gps_lat, d.gps_lon, d.gps_alt_m);
  } else {
    Serial.printf(" GPS    NO FIX  sats:%2u\n", d.gps_sats);
  }

  Serial.printf(" PWR    battery: %5.2f V\n", d.battery_v);

  unsigned long sinceTxMs = loraOk ? (now - lastTxMs) : 0;
  Serial.printf(" LORA   %-8s  last TX: %-4s  %4lums ago  (#%lu)  RSSI/SNR: see ground station (TX-only here)\n",
                loraOk ? "OK" : "DISABLED",
                (loraOk && txCount > 0) ? (lastTxOk ? "OK" : "FAIL") : "-",
                sinceTxMs, (unsigned long)txCount);

  Serial.printf(" SD     %-8s  file: %s\n",
                sdReady ? "LOGGING" : "OFF", sdReady ? currentLogPath : "-");

  Serial.printf(" PYRO   CH1(drogue) cont:%-4s fired:%-3s   |   CH2(main) cont:%-4s fired:%-3s\n",
                d.pyro1_continuity ? "OK" : "OPEN", d.pyro1_fired ? "YES" : "no",
                d.pyro2_continuity ? "OK" : "OPEN", d.pyro2_fired ? "YES" : "no");

  if (!d.pyro1_continuity || !d.pyro2_continuity) {
    Serial.println(F("        *** NO-GO: a pyro channel reads OPEN. Verify the e-match wiring."));
  }
  if (d.battery_v < VBAT_NOGO_V) {
    Serial.printf ("        *** NO-GO: VBAT %.2f V is below the %.2f V threshold.\n",
                   d.battery_v, VBAT_NOGO_V);
  }

  Serial.printf(" MODE   single_event:%s   ground_test:%s   min_apogee_alt: %.1f m   peak_agl: %.1f m\n",
                SINGLE_EVENT_MODE ? "ON " : "off",
                GROUND_TEST_MODE  ? "*** ON *** NOT FLIGHT CONFIG" : "off",
                minApogeeAltM(), maxAglSinceBoost);

  Serial.printf(" FSM    baro_v: %6.2f m/s   tilt: %6.1f deg %s  deploy_trigger: %s\n",
                baroVelocityMps, d.tilt_deg,
                (d.tilt_deg > TILT_WARN_DEG && d.state >= STATE_BOOST) ? "(>warn)" : "       ",
                d.deploy_trigger == 0 ? "none" : (d.deploy_trigger == 1 ? "baro-apogee" : "TIMEOUT-FAILSAFE"));

  Serial.printf(" BOOT   count: %u   reset_reason: %u %s\n",
                (unsigned)d.boot_count, (unsigned)d.reset_reason,
                (d.reset_reason == 9) ? "*** BROWNOUT ***" :   // ESP_RST_BROWNOUT == 9, not 6
                (d.reset_reason == 4) ? "*** PANIC ***"    :
                (d.reset_reason == 5) ? "*** INT_WDT ***"  :
                (d.reset_reason == 6) ? "*** TASK_WDT ***" :
                (d.reset_reason == 7) ? "*** WDT ***"      : "");

  Serial.println(F("============================================================"));
}

// Call once per loop() — self-throttled to DASHBOARD_INTERVAL_MS.
static void updateDashboard(const FlightData& d, unsigned long now) {
  if (now - lastDashboardMs >= DASHBOARD_INTERVAL_MS) {
    lastDashboardMs = now;
    printDashboard(d);
  }
}
