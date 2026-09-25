/*
  ================================================================
  IGNIS v2 Flight Computer — Sensors.h
  Raw-register BMP180 + MPU6050 drivers (no Adafruit_BMP085/MPU6050
  abstraction layer), GPS. SHT40 removed — not used on this build.

  Why raw register drivers for BMP180/MPU6050:
    - Per-reading validity (I2C failure or out-of-range result) is
      checked and reported, instead of silently trusting whatever the
      library handed back. This matters specifically because this
      board has a known floating-CSB failure mode that produces
      corrupted/halved BMP180 pressure values that still LOOK like
      plausible numbers.
    - Exact datasheet LSB/unit scale factors are used directly,
      instead of going through a library's own internal constants.
    - Chip-ID / WHO_AM_I checks catch a dead/miswired sensor at boot
      instead of silently returning zeros forever.

  Design choice — "hold last-known-good on a bad read": when a single
  BMP180 or MPU6050 read is rejected, the corresponding FlightData
  fields are NOT overwritten with zeros/garbage — they keep their last
  valid value, and *_reading_valid is set false for that sample. This
  means every downstream consumer (FSM thresholds, telemetry, CSV log,
  dashboard) automatically falls back to the last trustworthy value
  without needing its own validity-gating logic scattered everywhere.
  The one place that DOES explicitly gate on validity is the apogee
  detector in FlightState.h, because that's the one place a single
  corrupted-but-plausible sample could directly cause an early pyro
  fire — see the comment there.

  MANDATORY: the I2C bus MUST be preconditioned (SDA/SCL driven HIGH
  then released to INPUT) before Wire.begin() on this hardware, due
  to the floating BMP180 CSB pin on the current PCB rev. We also run
  a 9-clock stuck-bus recovery right after, covering a second/
  different I2C fault mode (a slave holding SDA low mid-transaction).
  Doing both, in sequence, doesn't assume which one you're hitting on
  any given boot.
  ================================================================
*/

#pragma once
#include <Wire.h>
#include <TinyGPSPlus.h>
#include <math.h>
#include "Config.h"
#include "FlightData.h"

static TinyGPSPlus       gps;
static HardwareSerial    gpsSerial(1); // UART1

static bool mpuOk = false;
static bool bmpOk = false;

// ================================================================
// I2C bus preconditioning + recovery
// ================================================================
static void i2cPrecondition(int sdaPin, int sclPin) {
  pinMode(sdaPin, OUTPUT);
  pinMode(sclPin, OUTPUT);
  digitalWrite(sdaPin, HIGH);
  digitalWrite(sclPin, HIGH);
  delay(10);
  pinMode(sdaPin, INPUT);
  pinMode(sclPin, INPUT);
  delay(10);
}

// Classic 9-clock stuck-bus recovery: if a slave is holding SDA low
// mid-transaction, toggling SCL up to 9 times gives it enough clock
// edges to finish whatever it thought it was doing and release the
// bus. Distinct fault mode from the floating-CSB issue above — this
// one's about a slave stuck mid-byte, not a bad idle-state voltage.
static void recoverI2CBus(int sdaPin, int sclPin) {
  pinMode(sdaPin, INPUT_PULLUP);
  pinMode(sclPin, INPUT_PULLUP);
  delay(10);

  pinMode(sclPin, OUTPUT);
  for (int i = 0; i < 9; i++) {
    if (digitalRead(sdaPin) == HIGH) break;
    digitalWrite(sclPin, LOW);
    delayMicroseconds(10);
    digitalWrite(sclPin, HIGH);
    delayMicroseconds(10);
  }
  pinMode(sclPin, INPUT_PULLUP);
  pinMode(sdaPin, INPUT_PULLUP);
  delay(10);
}

// ================================================================
// BMP180 — raw register driver
// ================================================================
#define BMP180_REG_CHIP_ID  0xD0
#define BMP180_REG_CONTROL  0xF4
#define BMP180_REG_RESULT   0xF6
#define BMP180_CMD_TEMP     0x2E
#define BMP180_CMD_PRESSURE 0x34

static int16_t  bmpAC1, bmpAC2, bmpAC3;
static uint16_t bmpAC4, bmpAC5, bmpAC6;
static int16_t  bmpB1, bmpB2;
static int16_t  bmpMB, bmpMC, bmpMD;

static bool bmpWrite8(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BMP180_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool bmpReadBytes(uint8_t reg, uint8_t* buffer, uint8_t len) {
  Wire.beginTransmission(BMP180_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  uint8_t received = Wire.requestFrom((uint8_t)BMP180_ADDR, len);
  if (received != len) return false;

  for (uint8_t i = 0; i < len; i++) buffer[i] = Wire.read();
  return true;
}

static uint8_t bmpRead8(uint8_t reg) {
  uint8_t value = 0;
  bmpReadBytes(reg, &value, 1);
  return value;
}

static uint16_t bmpReadU16BE(uint8_t reg) {
  uint8_t b[2];
  if (!bmpReadBytes(reg, b, 2)) return 0;
  return ((uint16_t)b[0] << 8) | b[1];
}

static int16_t bmpReadS16BE(uint8_t reg) {
  return (int16_t)bmpReadU16BE(reg);
}

static bool bmpReadCalibration() {
  bmpAC1 = bmpReadS16BE(0xAA);
  bmpAC2 = bmpReadS16BE(0xAC);
  bmpAC3 = bmpReadS16BE(0xAE);
  bmpAC4 = bmpReadU16BE(0xB0);
  bmpAC5 = bmpReadU16BE(0xB2);
  bmpAC6 = bmpReadU16BE(0xB4);
  bmpB1  = bmpReadS16BE(0xB6);
  bmpB2  = bmpReadS16BE(0xB8);
  bmpMB  = bmpReadS16BE(0xBA);
  bmpMC  = bmpReadS16BE(0xBC);
  bmpMD  = bmpReadS16BE(0xBE);

  if (bmpAC1 == 0 || bmpAC1 == -1) return false;
  if (bmpAC4 == 0 || bmpAC4 == 65535) return false;
  if (bmpAC5 == 0 || bmpAC5 == 65535) return false;
  if (bmpAC6 == 0 || bmpAC6 == 65535) return false;
  return true;
}

static bool bmp180Begin() {
  uint8_t chipID = bmpRead8(BMP180_REG_CHIP_ID);
  Serial.printf("[BMP180] Chip ID: 0x%02X\n", chipID);

  if (chipID != 0x55) {
    Serial.println("[BMP180] ERROR: not detected correctly (check CSB tied to VCC_3V3, VDDIO, SDO).");
    return false;
  }
  if (!bmpReadCalibration()) {
    Serial.println("[BMP180] ERROR: failed to read calibration data.");
    return false;
  }
  Serial.println("[BMP180] Calibration loaded.");
  return true;
}

static bool bmpReadRawTemperature(int32_t& UT) {
  if (!bmpWrite8(BMP180_REG_CONTROL, BMP180_CMD_TEMP)) return false;
  delay(5);
  UT = bmpReadU16BE(BMP180_REG_RESULT);
  return true;
}

static bool bmpReadRawPressure(int32_t& UP, uint8_t oss) {
  uint8_t b[3];
  uint8_t command = BMP180_CMD_PRESSURE + (oss << 6);
  if (!bmpWrite8(BMP180_REG_CONTROL, command)) return false;
  // Datasheet max conversion time by OSS: 4.5/7.5/13.5/25.5 ms for 0/1/2/3.
  // +a few ms margin rather than shaving it exactly to the datasheet min.
  static const uint8_t ossDelayMs[4] = {5, 8, 14, 27};
  delay(ossDelayMs[oss & 0x03]);
  if (!bmpReadBytes(BMP180_REG_RESULT, b, 3)) return false;
  UP = (((int32_t)b[0] << 16) | ((int32_t)b[1] << 8) | b[2]) >> (8 - oss);
  return true;
}

// Returns false on any I2C failure OR an out-of-range result — both
// treated as "don't trust this reading." Caller (readSensors) leaves
// FlightData's baro fields at their last-known-good value when this
// returns false.
static bool readBMP180(float& temperatureC, int32_t& pressurePa, uint8_t oss) {
  int32_t UT, UP;
  if (!bmpReadRawTemperature(UT)) return false;
  if (!bmpReadRawPressure(UP, oss)) return false;

  int32_t X1 = ((UT - (int32_t)bmpAC6) * (int32_t)bmpAC5) >> 15;
  int32_t X2 = ((int32_t)bmpMC << 11) / (X1 + bmpMD);
  int32_t B5 = X1 + X2;

  int32_t temp_x10 = (B5 + 8) >> 4;
  temperatureC = temp_x10 / 10.0f;

  int32_t B6 = B5 - 4000;
  X1 = ((int32_t)bmpB2 * ((B6 * B6) >> 12)) >> 11;
  X2 = ((int32_t)bmpAC2 * B6) >> 11;
  int32_t X3 = X1 + X2;
  int32_t B3 = ((((int32_t)bmpAC1 * 4 + X3) << oss) + 2) >> 2;

  X1 = ((int32_t)bmpAC3 * B6) >> 13;
  X2 = ((int32_t)bmpB1 * ((B6 * B6) >> 12)) >> 16;
  X3 = ((X1 + X2) + 2) >> 2;

  uint32_t B4 = ((uint32_t)bmpAC4 * (uint32_t)(X3 + 32768)) >> 15;
  uint32_t B7 = ((uint32_t)UP - B3) * (50000 >> oss);

  int32_t p;
  if (B7 < 0x80000000) p = (B7 * 2) / B4;
  else                 p = (B7 / B4) * 2;

  X1 = (p >> 8) * (p >> 8);
  X1 = (X1 * 3038) >> 16;
  X2 = (-7357 * p) >> 16;
  p = p + ((X1 + X2 + 3791) >> 4);

  pressurePa = p;

  // Reject corrupt reads — known failure mode: floating CSB produces
  // corrupted/halved pressure values that still fall inside a naively
  // "plausible" range, so this bound is deliberately generous (covers
  // anywhere from ~3000m below to ~9000m above sea level) rather than
  // tight. It catches the gross corruption case, not subtle drift.
  if (pressurePa < 30000 || pressurePa > 110000) return false;

  return true;
}

// Direct pressure-ratio altitude — used for BOTH absolute (vs standard/
// launch-day sea level) and relative/AGL (vs measured ground pressure)
// by passing a different reference pressure. This avoids computing two
// separate absolute altitudes and subtracting them, which introduces a
// small extra nonlinearity error versus the true barometric formula.
static float altitudeFromPressure(float pressurePa, float referencePressurePa) {
  return 44330.0f * (1.0f - powf(pressurePa / referencePressurePa, 0.19029495f));
}

// ================================================================
// MPU6050 — raw register driver
// ================================================================
#define MPU6050_REG_PWR_MGMT_1    0x6B
#define MPU6050_REG_CONFIG        0x1A   // DLPF
#define MPU6050_REG_GYRO_CONFIG   0x1B
#define MPU6050_REG_ACCEL_CONFIG  0x1C
#define MPU6050_REG_ACCEL_XOUT_H  0x3B   // burst: accel(6) + temp(2) + gyro(6) = 14 bytes
#define MPU6050_REG_WHO_AM_I      0x75

// Range selection: AFS_SEL=3 -> +/-16g, FS_SEL=1 -> +/-500 dps.
// Matches the high-g boost expectation (T/W 15.9:1) and prior gyro
// range choice. DLPF_CFG=4 -> ~21Hz accel BW / ~20Hz gyro BW.
#define MPU_ACCEL_FS_SEL   3
#define MPU_GYRO_FS_SEL    1
#define MPU_DLPF_CFG       4

// Exact datasheet LSB-per-unit scale factors for the ranges above —
// used directly instead of a library's internal constants.
#define MPU_ACCEL_LSB_PER_G     2048.0f   // AFS_SEL=3 (+/-16g)
#define MPU_GYRO_LSB_PER_DPS      65.5f   // FS_SEL=1  (+/-500dps)

static float gyroBiasX_dps = 0, gyroBiasY_dps = 0, gyroBiasZ_dps = 0;

static bool mpuWrite8(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool mpuReadBytes(uint8_t reg, uint8_t* buffer, uint8_t len) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  uint8_t received = Wire.requestFrom((uint8_t)MPU6050_ADDR, len);
  if (received != len) return false;

  for (uint8_t i = 0; i < len; i++) buffer[i] = Wire.read();
  return true;
}

static bool mpu6050Begin() {
  uint8_t whoAmI = 0;
  if (!mpuReadBytes(MPU6050_REG_WHO_AM_I, &whoAmI, 1)) {
    Serial.println("[MPU6050] ERROR: I2C read failed (not responding).");
    return false;
  }
  Serial.printf("[MPU6050] WHO_AM_I: 0x%02X\n", whoAmI);
  if (whoAmI != 0x68) {
    Serial.println("[MPU6050] ERROR: unexpected WHO_AM_I (expected 0x68).");
    return false;
  }

  // Wake from sleep, PLL with X-gyro reference (more accurate clock
  // source than the internal 8MHz oscillator).
  if (!mpuWrite8(MPU6050_REG_PWR_MGMT_1, 0x01)) return false;
  delay(50); // datasheet-recommended settle time after waking

  if (!mpuWrite8(MPU6050_REG_CONFIG, MPU_DLPF_CFG)) return false;
  if (!mpuWrite8(MPU6050_REG_GYRO_CONFIG, MPU_GYRO_FS_SEL << 3)) return false;
  if (!mpuWrite8(MPU6050_REG_ACCEL_CONFIG, MPU_ACCEL_FS_SEL << 3)) return false;

  Serial.println("[MPU6050] Configured: +/-16g, +/-500dps, DLPF~21Hz.");
  return true;
}

static float accelBiasX_g = 0, accelBiasY_g = 0, accelBiasZ_g = 0;

// Iterative accel+gyro bias calibration, adapted from the well-known Luis
// Rodenas MPU6050 calibration technique (I2Cdev/MPU6050 library), with
// three deliberate departures from that original:
//
//   1. SOFTWARE bias subtraction, not chip offset registers. The classic
//      script writes computed offsets into the MPU6050's internal
//      XA_OFFS/YA_OFFS/ZA_OFFS/XG_OFFS/etc. registers. Those registers'
//      bit-widths and reserved bits are known to be inconsistent across
//      MPU6050 silicon revisions and clone chips — risky to depend on
//      given this board's already-documented I2C fragility. Subtracting
//      a computed bias in software, in the same place the gyro
//      subtraction already happened, is portable and avoids that risk
//      entirely, at the cost of a few extra float subtractions per read
//      (irrelevant next to the I2C transaction time).
//
//   2. Auto-detected gravity axis, not assumed Z-up. The original script
//      assumes the board sits flat on a table (Z axis reads +1g, X/Y
//      read 0). This board sits however it's mounted in the airframe on
//      a VERTICAL rail — could be any axis. We take a quick pre-sample
//      and calibrate against whichever axis actually reads close to 1g,
//      with the correct sign.
//
//   3. Deadzones/targets in physical units (g, dps), scaled to whatever
//      accel/gyro range this board is configured for — not hardcoded
//      raw LSB counts (the original's "16384" target is the LSB/g count
//      for +-2g; we run +-16g, where 1g is 2048 LSB, an 8x difference).
//      Config.h's ACCEL_CAL_DEADZONE_G / GYRO_CAL_DEADZONE_DPS express
//      the actual physical precision wanted, independent of range.
//
// Also added: a hard iteration cap. The original script's convergence
// loop is an unconditional while(1) with no exit if it never converges —
// acceptable for a bench utility you can just unplug, not acceptable for
// something that has to finish booting a flight computer. We log a
// warning and use whatever was reached if it doesn't fully converge.
static void mpuCalibrateBias() {
  Serial.printf("[MPU6050] Calibrating accel+gyro bias (keep board still, orientation as mounted)...\n");

  // ---- Step 1: quick pre-sample to find which axis gravity is on ----
  const int PRESAMPLE_N = 30;
  double preX = 0, preY = 0, preZ = 0;
  for (int i = 0; i < PRESAMPLE_N; i++) {
    uint8_t b[14];
    if (mpuReadBytes(MPU6050_REG_ACCEL_XOUT_H, b, 14)) {
      preX += (int16_t)((b[0] << 8) | b[1]) / MPU_ACCEL_LSB_PER_G;
      preY += (int16_t)((b[2] << 8) | b[3]) / MPU_ACCEL_LSB_PER_G;
      preZ += (int16_t)((b[4] << 8) | b[5]) / MPU_ACCEL_LSB_PER_G;
    }
    delay(2);
  }
  preX /= PRESAMPLE_N; preY /= PRESAMPLE_N; preZ /= PRESAMPLE_N;

  float targetX = 0, targetY = 0, targetZ = 0;
  float absX = fabsf(preX), absY = fabsf(preY), absZ = fabsf(preZ);
  const char* gravityAxisName;
  if (absX >= absY && absX >= absZ) { targetX = (preX >= 0) ? 1.0f : -1.0f; gravityAxisName = "X"; }
  else if (absY >= absX && absY >= absZ) { targetY = (preY >= 0) ? 1.0f : -1.0f; gravityAxisName = "Y"; }
  else { targetZ = (preZ >= 0) ? 1.0f : -1.0f; gravityAxisName = "Z"; }
  Serial.printf("[MPU6050] Detected gravity on %s axis (pre-sample: X=%.2f Y=%.2f Z=%.2f g)\n",
                gravityAxisName, preX, preY, preZ);

  // ---- Step 2: iterative convergence, same shape as the classic script ----
  accelBiasX_g = accelBiasY_g = accelBiasZ_g = 0;
  gyroBiasX_dps = gyroBiasY_dps = gyroBiasZ_dps = 0;

  bool converged = false;
  for (int iter = 0; iter < ACCEL_GYRO_CAL_MAX_ITERATIONS; iter++) {
    double sumAx = 0, sumAy = 0, sumAz = 0, sumGx = 0, sumGy = 0, sumGz = 0;
    int valid = 0;

    for (int i = 0; i < ACCEL_GYRO_CAL_SAMPLES_PER_ITER; i++) {
      uint8_t b[14];
      if (mpuReadBytes(MPU6050_REG_ACCEL_XOUT_H, b, 14)) {
        sumAx += ((int16_t)((b[0] << 8) | b[1])) / MPU_ACCEL_LSB_PER_G - accelBiasX_g;
        sumAy += ((int16_t)((b[2] << 8) | b[3])) / MPU_ACCEL_LSB_PER_G - accelBiasY_g;
        sumAz += ((int16_t)((b[4] << 8) | b[5])) / MPU_ACCEL_LSB_PER_G - accelBiasZ_g;
        sumGx += ((int16_t)((b[8]  << 8) | b[9]))  / MPU_GYRO_LSB_PER_DPS - gyroBiasX_dps;
        sumGy += ((int16_t)((b[10] << 8) | b[11])) / MPU_GYRO_LSB_PER_DPS - gyroBiasY_dps;
        sumGz += ((int16_t)((b[12] << 8) | b[13])) / MPU_GYRO_LSB_PER_DPS - gyroBiasZ_dps;
        valid++;
      }
      delay(2); // matches the classic script's spacing to avoid repeated/stale measures
    }
    if (valid == 0) continue;

    float meanAx = sumAx / valid, meanAy = sumAy / valid, meanAz = sumAz / valid;
    float meanGx = sumGx / valid, meanGy = sumGy / valid, meanGz = sumGz / valid;

    float errAx = meanAx - targetX, errAy = meanAy - targetY, errAz = meanAz - targetZ;

    accelBiasX_g += errAx;
    accelBiasY_g += errAy;
    accelBiasZ_g += errAz;
    gyroBiasX_dps += meanGx;
    gyroBiasY_dps += meanGy;
    gyroBiasZ_dps += meanGz;

    bool ok = fabsf(errAx) <= ACCEL_CAL_DEADZONE_G && fabsf(errAy) <= ACCEL_CAL_DEADZONE_G &&
              fabsf(errAz) <= ACCEL_CAL_DEADZONE_G && fabsf(meanGx) <= GYRO_CAL_DEADZONE_DPS &&
              fabsf(meanGy) <= GYRO_CAL_DEADZONE_DPS && fabsf(meanGz) <= GYRO_CAL_DEADZONE_DPS;

    Serial.printf("[MPU6050] Cal iter %d: accel err X=%.3f Y=%.3f Z=%.3f g | gyro mean X=%.2f Y=%.2f Z=%.2f dps%s\n",
                  iter, errAx, errAy, errAz, meanGx, meanGy, meanGz, ok ? "  -> converged" : "");

    if (ok) { converged = true; break; }
  }

  if (!converged) {
    Serial.println("[MPU6050] WARNING: bias calibration did not fully converge within the iteration cap — using best estimate reached. Consider rerunning on a more stable surface.");
  }

  Serial.printf("[MPU6050] Final bias: accel X=%.3f Y=%.3f Z=%.3f g | gyro X=%.2f Y=%.2f Z=%.2f dps\n",
                accelBiasX_g, accelBiasY_g, accelBiasZ_g, gyroBiasX_dps, gyroBiasY_dps, gyroBiasZ_dps);
}

// Single burst read of accel+temp+gyro (14 contiguous registers), one
// I2C transaction instead of three separate ones. Returns false (and
// leaves outputs untouched) on any I2C failure.
static bool readMPU6050(FlightData& d) {
  uint8_t b[14];
  if (!mpuReadBytes(MPU6050_REG_ACCEL_XOUT_H, b, 14)) return false;

  int16_t ax = (int16_t)((b[0]  << 8) | b[1]);
  int16_t ay = (int16_t)((b[2]  << 8) | b[3]);
  int16_t az = (int16_t)((b[4]  << 8) | b[5]);
  int16_t rawTemp = (int16_t)((b[6] << 8) | b[7]);
  int16_t gx = (int16_t)((b[8]  << 8) | b[9]);
  int16_t gy = (int16_t)((b[10] << 8) | b[11]);
  int16_t gz = (int16_t)((b[12] << 8) | b[13]);

  d.accel_x_g = (ax / MPU_ACCEL_LSB_PER_G) - accelBiasX_g;
  d.accel_y_g = (ay / MPU_ACCEL_LSB_PER_G) - accelBiasY_g;
  d.accel_z_g = (az / MPU_ACCEL_LSB_PER_G) - accelBiasZ_g;
  d.accel_total_g = sqrtf(d.accel_x_g * d.accel_x_g +
                           d.accel_y_g * d.accel_y_g +
                           d.accel_z_g * d.accel_z_g);

  // Light EMA smoothing for display/logging only — NOT fed back into
  // the FSM, which reacts to raw accel_total_g so burnout/liftoff
  // detection latency isn't increased by filtering.
  const float EMA_ALPHA = 0.2f;
  d.accel_total_g_filtered = (d.accel_total_g_filtered == 0)
    ? d.accel_total_g
    : (EMA_ALPHA * d.accel_total_g + (1.0f - EMA_ALPHA) * d.accel_total_g_filtered);

  d.gyro_x_dps = (gx / MPU_GYRO_LSB_PER_DPS) - gyroBiasX_dps;
  d.gyro_y_dps = (gy / MPU_GYRO_LSB_PER_DPS) - gyroBiasY_dps;
  d.gyro_z_dps = (gz / MPU_GYRO_LSB_PER_DPS) - gyroBiasZ_dps;

  // MPU6050 datasheet formula for its internal temp sensor.
  d.mpu_temp_c = (rawTemp / 340.0f) + 36.53f;

  return true;
}

// ================================================================
// Init
// ================================================================

// v13: loads the RTC-persisted accel/gyro bias instead of recomputing it.
// Used on a resume boot only — see initSensors() below.
static void applyPersistedBias() {
  accelBiasX_g = rtcState.accelBiasX_g;
  accelBiasY_g = rtcState.accelBiasY_g;
  accelBiasZ_g = rtcState.accelBiasZ_g;
  gyroBiasX_dps = rtcState.gyroBiasX_dps;
  gyroBiasY_dps = rtcState.gyroBiasY_dps;
  gyroBiasZ_dps = rtcState.gyroBiasZ_dps;
  Serial.printf("[MPU6050] Resume boot: reusing persisted bias (accel X=%.3f Y=%.3f Z=%.3f g | gyro X=%.2f Y=%.2f Z=%.2f dps).\n",
                accelBiasX_g, accelBiasY_g, accelBiasZ_g, gyroBiasX_dps, gyroBiasY_dps, gyroBiasZ_dps);
}

// isResume (v13): true only when setup() determined this is a resumed
// mid-flight boot (see IGNIS_FC_heavy_13.ino). mpu6050Begin() still runs
// every boot regardless — cheap, and needed in case the sensor itself
// lost power — but the SLOW calibration loop (mpuCalibrateBias(), up to
// ~1.6s worst case and which assumes the board is stationary) is skipped
// on resume: recalibrating on a body that's actually flying/falling is
// not just slow, it's wrong.
static bool initSensors(bool isResume = false) {
  i2cPrecondition(I2C_SDA_PIN, I2C_SCL_PIN);
  recoverI2CBus(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(I2C_TIMEOUT_MS);  // v13 — see Config.h

  mpuOk = mpu6050Begin();
  if (mpuOk) {
    if (isResume && rtcState.mpuCalibValid) {
      applyPersistedBias();
    } else {
      rtcSetLastOp(OP_MPU_CAL);
      mpuCalibrateBias();
      rtcState.mpuCalibValid  = true;
      rtcState.accelBiasX_g   = accelBiasX_g;
      rtcState.accelBiasY_g   = accelBiasY_g;
      rtcState.accelBiasZ_g   = accelBiasZ_g;
      rtcState.gyroBiasX_dps  = gyroBiasX_dps;
      rtcState.gyroBiasY_dps  = gyroBiasY_dps;
      rtcState.gyroBiasZ_dps  = gyroBiasZ_dps;
      rtcSetLastOp(OP_IDLE);
    }
  } else {
    Serial.println("[SENS] MPU6050 init FAILED.");
  }

  bmpOk = bmp180Begin();
  if (!bmpOk) Serial.println("[SENS] BMP180 init FAILED.");

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  return mpuOk && bmpOk; // GPS is non-critical to flight logic
}

// ---------------- Feed GPS parser (call every loop) ----------------
static void pumpGPS() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
}

// ---------------- Read all sensors into FlightData ----------------
// Extracted from readSensors() in v12 so setup() can evaluate the battery
// NO-GO before the first sensor loop runs. Same maths, same divider ratio.
static void readBattery(FlightData& d) {
  int raw = analogRead(VBAT_MON_PIN);
  d.battery_v = (raw / ADC_MAX_COUNTS) * ADC_REF_V / VBAT_DIVIDER_RATIO;
}

// v13: consecutive-failure counter driving runtime I2C bus recovery, below.
static uint16_t i2cConsecutiveFailures = 0;

static void readSensors(FlightData& d) {
  d.timestamp_ms = millis();

  if (mpuOk) {
    rtcSetLastOp(OP_I2C_READ);
    d.imu_reading_valid = readMPU6050(d);
    // On failure, readMPU6050() touched nothing — d.accel_*/gyro_* still
    // hold their last-known-good values, only the validity flag changed.
  }

  if (bmpOk) {
    uint8_t oss = (d.state <= STATE_ARMED) ? BMP180_OSS_IDLE : BMP180_OSS_FLIGHT;
    float temperatureC;
    int32_t pressurePa;
    rtcSetLastOp(OP_I2C_READ);
    d.baro_reading_valid = readBMP180(temperatureC, pressurePa, oss);
    if (d.baro_reading_valid) {
      d.pressure_pa = (float)pressurePa;
      d.baro_temp_c = temperatureC;
      d.baro_alt_m  = altitudeFromPressure((float)pressurePa, SEA_LEVEL_PRESSURE_PA);
    }
    // On failure, d.pressure_pa/baro_temp_c/baro_alt_m keep last-known-good.
  }
  rtcSetLastOp(OP_IDLE);

  // v13: if BOTH sensors keep failing back-to-back, the shared I2C bus
  // itself is the more likely explanation than two coincidentally-bad
  // samples — re-run the same boot-time recovery instead of retrying a
  // wedged bus every 20 ms indefinitely (deliberately AND, not OR: a
  // single permanently-dead sensor on an otherwise-healthy bus would
  // false-trigger this every 10 ticks forever under OR, for no benefit).
  if (mpuOk && bmpOk && !d.imu_reading_valid && !d.baro_reading_valid) {
    i2cConsecutiveFailures++;
    if (i2cConsecutiveFailures >= I2C_FAIL_RECOVERY_THRESHOLD) {
      Serial.println("[SENS] I2C: repeated failures on both sensors — re-running bus recovery.");
      recoverI2CBus(I2C_SDA_PIN, I2C_SCL_PIN);
      Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
      Wire.setClock(100000);
      Wire.setTimeOut(I2C_TIMEOUT_MS);
      i2cConsecutiveFailures = 0;
    }
  } else {
    i2cConsecutiveFailures = 0;
  }

  pumpGPS();
  d.gps_fix = gps.location.isValid();
  if (d.gps_fix) {
    d.gps_lat = gps.location.lat();
    d.gps_lon = gps.location.lng();
  }
  if (gps.altitude.isValid())   d.gps_alt_m = gps.altitude.meters();
  if (gps.satellites.isValid()) d.gps_sats  = gps.satellites.value();

  readBattery(d);
}
