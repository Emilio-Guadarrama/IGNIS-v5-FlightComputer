// =============================================================================
// IGNIS v5: i2c_bus.h  (sensor bus helpers, flight task only)
// Bounded-time register access. Wire timeout from config (reference
// Config.h:286-294) so a stuck transaction returns instead of hanging.
// =============================================================================
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace ignis {

void i2cBusBegin();       // 9-clock recovery + Wire.begin + timeout
void i2cBusRecover();     // runtime recovery (reference Sensors.h:587-599)
bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t val);
bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len);
bool i2cProbe(uint8_t addr);

}  // namespace ignis
