#include "gps_battery.h"
#include <Arduino.h>
#include "../config/pins_v5.h"
#include "../core/nmea.h"

namespace ignis {

static NmeaParser s_nmea;

void gpsBegin() {
  pinMode(PIN_GPS_EXTINT, INPUT);
  pinMode(PIN_GPS_RESET_N, INPUT);   // not driven: polarity/net unconfirmed (see pins_v5.h)
  pinMode(PIN_GPS_PPS, INPUT);
  Serial1.setRxBufferSize(1024);
  Serial1.begin(GPS_UART_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
}

void gpsPump(FlightSnapshot& s) {
  // Bounded: at 9600 baud at most ~20 bytes arrive per 20 ms tick; cap the
  // drain so a babbling UART can never stretch the flight loop.
  int budget = 256;
  while (budget-- > 0 && Serial1.available() > 0) s_nmea.feed((char)Serial1.read());
  const GpsFix& f = s_nmea.fix();
  s.gps_fix = f.fix;
  if (f.fix) { s.gps_lat = f.lat; s.gps_lon = f.lon; s.gps_alt_m = f.alt_m; }
  s.gps_sats = f.sats;
}

void batteryBegin() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT_MON, ADC_11db);  // ADC1: usable with WiFi on
}

float batteryReadV() {
  // Calibrated millivolts (eFuse curve) instead of raw counts * 3.3 / 4095.
  uint32_t mv = analogReadMilliVolts(PIN_VBAT_MON);
  return (mv / 1000.0f) / VBAT_DIVIDER_RATIO;
}

}  // namespace ignis
