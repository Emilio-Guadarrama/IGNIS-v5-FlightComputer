// =============================================================================
// IGNIS v5: imu_lsm6dsv32x.h
// Raw-register LSM6DSV32X driver (same approach as the reference raw MPU6050
// driver, Sensors.h:255-477): WHO_AM_I check at boot, per-read validity,
// hold-last-known-good on a failed read.
// =============================================================================
#pragma once
#include <stdint.h>
#include "../core/flight_types.h"

namespace ignis {

bool imuBegin();                       // probe both addresses, configure
bool imuOk();
uint8_t imuAddress();
void imuCalibrateGyroBias();           // board stationary; skipped on RECOVERY boot
// Reads into the snapshot. Returns false (fields untouched) on any I2C failure.
bool imuRead(FlightSnapshot& s);

}  // namespace ignis
