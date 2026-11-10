// =============================================================================
// IGNIS v5: gps_battery.h
// GNSS UART pump (non-blocking, flight task) and battery ADC.
// GPS control pins (EXTINT, RESET_N, PPS) are unconfirmed for v5, so they are
// left as high-impedance inputs: firmware never drives an unconfirmed pin.
// =============================================================================
#pragma once
#include "../core/flight_types.h"

namespace ignis {

void gpsBegin();
void gpsPump(FlightSnapshot& s);      // drains UART RX, updates fix fields
void batteryBegin();
float batteryReadV();

}  // namespace ignis
