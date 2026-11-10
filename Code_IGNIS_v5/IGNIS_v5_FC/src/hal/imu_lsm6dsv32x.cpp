#include "imu_lsm6dsv32x.h"
#include <Arduino.h>
#include <math.h>
#include "i2c_bus.h"
#include "../config/pins_v5.h"

// TODO(v5-verify): register addresses and field encodings below follow the
// ST LSM6DSV32X register map (WHO_AM_I 0x0F = 0x70, CTRL1..CTRL8, OUT_TEMP_L
// 0x20, OUTX_L_G 0x22, OUTX_L_A 0x28). The repo datasheet PDF could not be
// text-extracted on this machine: verify against "Datasheet - LSM6DSV32XTR.pdf"
// before flight. A wrong WHO_AM_I makes imuBegin() fail safe (IMU invalid,
// barometric liftoff backup still active).
namespace {
constexpr uint8_t REG_WHO_AM_I = 0x0F;
constexpr uint8_t WHO_AM_I_VAL = 0x70;
constexpr uint8_t REG_CTRL1 = 0x10;   // OP_MODE_XL[6:4] ODR_XL[3:0]
constexpr uint8_t REG_CTRL2 = 0x11;   // OP_MODE_G[6:4]  ODR_G[3:0]
constexpr uint8_t REG_CTRL3 = 0x12;   // BOOT(7) BDU(6) IF_INC(2) SW_RESET(0)
constexpr uint8_t REG_CTRL6 = 0x15;   // LPF1_G_BW[6:4] FS_G[3:0]
constexpr uint8_t REG_CTRL8 = 0x17;   // HP_LPF2_XL_BW[7:5] FS_XL[1:0]
constexpr uint8_t REG_OUT_TEMP_L = 0x20;  // burst: temp(2) gyro(6) accel(6) = 14 bytes

constexpr uint8_t ODR_480HZ_HP = 0x08;    // high-performance mode, 480 Hz
// ASSUMPTION(v5): +/-32 g (part maximum, rocket boost) and +/-2000 dps
// (better resolution than 4000 dps; tilt is diagnostic only).
constexpr uint8_t FS_XL_32G = 0x03;
constexpr uint8_t FS_G_2000DPS = 0x04;
constexpr float ACCEL_G_PER_LSB = 0.000976f;   // 0.976 mg/LSB @ +/-32 g
constexpr float GYRO_DPS_PER_LSB = 0.070f;     // 70 mdps/LSB @ +/-2000 dps
constexpr float TEMP_LSB_PER_C = 256.0f;       // 0 LSB = 25 C

uint8_t s_addr = 0;
bool s_ok = false;
float s_gbx = 0, s_gby = 0, s_gbz = 0;

int16_t le16(const uint8_t* b) { return (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8)); }
}  // namespace

namespace ignis {

bool imuOk() { return s_ok; }
uint8_t imuAddress() { return s_addr; }

bool imuBegin() {
  const uint8_t addrs[2] = {LSM6DSV32X_ADDR_PRIMARY, LSM6DSV32X_ADDR_ALT};
  s_ok = false;
  for (uint8_t a : addrs) {
    uint8_t who = 0;
    if (i2cRead(a, REG_WHO_AM_I, &who, 1) && who == WHO_AM_I_VAL) { s_addr = a; break; }
  }
  if (!s_addr) {
    Serial.println("[IMU] LSM6DSV32X not found at 0x6A/0x6B (WHO_AM_I != 0x70)");
    return false;
  }
  i2cWrite8(s_addr, REG_CTRL3, 0x01);  // SW_RESET
  delay(5);
  bool ok = i2cWrite8(s_addr, REG_CTRL3, 0x44) &&          // BDU + IF_INC
            i2cWrite8(s_addr, REG_CTRL8, FS_XL_32G) &&
            i2cWrite8(s_addr, REG_CTRL6, FS_G_2000DPS) &&
            i2cWrite8(s_addr, REG_CTRL1, ODR_480HZ_HP) &&
            i2cWrite8(s_addr, REG_CTRL2, ODR_480HZ_HP);
  if (!ok) { Serial.println("[IMU] configuration write failed"); return false; }
  uint8_t rb = 0;
  if (!i2cRead(s_addr, REG_CTRL8, &rb, 1) || (rb & 0x03) != FS_XL_32G) {
    Serial.println("[IMU] CTRL8 read-back mismatch");
    return false;
  }
  delay(20);
  s_ok = true;
  Serial.printf("[IMU] LSM6DSV32X @0x%02X: +/-32 g, +/-2000 dps, 480 Hz\n", s_addr);
  return true;
}

// Gyro bias only (simplified from reference Sensors.h:359-437, which also
// trimmed accel). ASSUMPTION(v5): accel is used untrimmed; liftoff threshold
// 1.8 g is insensitive to factory-level accel offsets.
void imuCalibrateGyroBias() {
  if (!s_ok) return;
  double sx = 0, sy = 0, sz = 0;
  int n = 0;
  for (int i = 0; i < 200; i++) {
    uint8_t b[14];
    if (i2cRead(s_addr, REG_OUT_TEMP_L, b, 14)) {
      sx += le16(b + 2) * GYRO_DPS_PER_LSB;
      sy += le16(b + 4) * GYRO_DPS_PER_LSB;
      sz += le16(b + 6) * GYRO_DPS_PER_LSB;
      n++;
    }
    delay(3);
  }
  if (n > 50) { s_gbx = sx / n; s_gby = sy / n; s_gbz = sz / n; }
  Serial.printf("[IMU] gyro bias %.2f %.2f %.2f dps (%d samples)\n", s_gbx, s_gby, s_gbz, n);
}

bool imuRead(FlightSnapshot& s) {
  if (!s_ok) return false;
  uint8_t b[14];
  if (!i2cRead(s_addr, REG_OUT_TEMP_L, b, 14)) return false;
  s.imu_temp_c = 25.0f + le16(b) / TEMP_LSB_PER_C;
  s.gyro_x_dps = le16(b + 2) * GYRO_DPS_PER_LSB - s_gbx;
  s.gyro_y_dps = le16(b + 4) * GYRO_DPS_PER_LSB - s_gby;
  s.gyro_z_dps = le16(b + 6) * GYRO_DPS_PER_LSB - s_gbz;
  s.accel_x_g = le16(b + 8) * ACCEL_G_PER_LSB;
  s.accel_y_g = le16(b + 10) * ACCEL_G_PER_LSB;
  s.accel_z_g = le16(b + 12) * ACCEL_G_PER_LSB;
  s.accel_total_g = sqrtf(s.accel_x_g * s.accel_x_g + s.accel_y_g * s.accel_y_g +
                          s.accel_z_g * s.accel_z_g);
  // Display/log-only EMA; the FSM uses the raw magnitude (reference Sensors.h:461-467).
  const float a = 0.2f;
  s.accel_total_g_filtered = (s.accel_total_g_filtered == 0)
      ? s.accel_total_g : a * s.accel_total_g + (1 - a) * s.accel_total_g_filtered;
  return true;
}

}  // namespace ignis
