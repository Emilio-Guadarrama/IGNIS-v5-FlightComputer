// =============================================================================
// IGNIS v5: baro_dps368.h
// Raw-register DPS368 driver, continuous pressure+temperature mode, read the
// latest result each flight tick (non-blocking: no conversion waits in the
// 50 Hz loop, unlike the reference BMP180 delay() reads, Sensors.h:176-194).
// =============================================================================
#pragma once
#include <stdint.h>
#include "../core/flight_types.h"

namespace ignis {

bool baroBegin();
bool baroOk();
uint8_t baroAddress();
// Returns false (fields untouched) on I2C failure or out-of-range result.
bool baroRead(FlightSnapshot& s);

}  // namespace ignis
