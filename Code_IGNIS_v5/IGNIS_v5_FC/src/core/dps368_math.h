// =============================================================================
// IGNIS v5: dps368_math.h  (pure C++, host-testable)
// DPS368 calibration-coefficient decoding and compensation formula.
// Register layout and formula: Infineon DPS368 datasheet, sections
// "Calibration Coefficients" (COEF 0x10-0x21) and "How to Calculate
// Compensated Pressure and Temperature Values".
// TODO(v5-verify): register addresses/bit fields were written from the
// published DPS3xx register map; the repo PDF could not be text-extracted on
// this machine. Verify against infineon-dps368-datasheet-en.pdf.
// =============================================================================
#pragma once
#include <stdint.h>

namespace ignis {

struct Dps368Coef {
  int32_t c0, c1, c00, c10, c01, c11, c20, c21, c30;
};

// Two's-complement sign extension of an n-bit value.
inline int32_t signExtend(uint32_t v, int bits) {
  uint32_t m = 1u << (bits - 1);
  v &= (1u << bits) - 1u;
  return (int32_t)((v ^ m) - m);
}

// raw: 18 bytes read from 0x10..0x21
inline Dps368Coef dps368ParseCoef(const uint8_t r[18]) {
  Dps368Coef c;
  c.c0  = signExtend(((uint32_t)r[0] << 4) | (r[1] >> 4), 12);
  c.c1  = signExtend((((uint32_t)r[1] & 0x0F) << 8) | r[2], 12);
  c.c00 = signExtend(((uint32_t)r[3] << 12) | ((uint32_t)r[4] << 4) | (r[5] >> 4), 20);
  c.c10 = signExtend((((uint32_t)r[5] & 0x0F) << 16) | ((uint32_t)r[6] << 8) | r[7], 20);
  c.c01 = signExtend(((uint32_t)r[8] << 8) | r[9], 16);
  c.c11 = signExtend(((uint32_t)r[10] << 8) | r[11], 16);
  c.c20 = signExtend(((uint32_t)r[12] << 8) | r[13], 16);
  c.c21 = signExtend(((uint32_t)r[14] << 8) | r[15], 16);
  c.c30 = signExtend(((uint32_t)r[16] << 8) | r[17], 16);
  return c;
}

// Compensation scale factor kP / kT by oversampling rate (1..128).
inline float dps368ScaleFactor(uint8_t oversampling) {
  switch (oversampling) {
    case 1: return 524288.0f;   case 2: return 1572864.0f;
    case 4: return 3670016.0f;  case 8: return 7864320.0f;
    case 16: return 253952.0f;  case 32: return 516096.0f;
    case 64: return 1040384.0f; case 128: return 2088960.0f;
    default: return 0.0f;
  }
}

inline float dps368Temperature(const Dps368Coef& c, int32_t t_raw, float kT) {
  float t_sc = t_raw / kT;
  return c.c0 * 0.5f + c.c1 * t_sc;
}

inline float dps368Pressure(const Dps368Coef& c, int32_t p_raw, float kP, int32_t t_raw, float kT) {
  float p_sc = p_raw / kP;
  float t_sc = t_raw / kT;
  return c.c00 + p_sc * (c.c10 + p_sc * (c.c20 + p_sc * c.c30)) +
         t_sc * c.c01 + t_sc * p_sc * (c.c11 + p_sc * c.c21);
}

}  // namespace ignis
