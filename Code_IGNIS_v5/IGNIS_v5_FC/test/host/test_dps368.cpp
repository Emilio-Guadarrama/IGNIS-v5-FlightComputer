// Host tests: DPS368 coefficient decoding (bit packing + sign extension) and
// compensation sanity. Register layout per DPS368 datasheet (to be verified,
// see dps368_math.h).
#include "mini_test.h"
#include "../../src/core/dps368_math.h"

using namespace ignis;

TEST(dps368_sign_extend) {
  CHECK_EQ(signExtend(0x7FF, 12), 2047);
  CHECK_EQ(signExtend(0x800, 12), -2048);
  CHECK_EQ(signExtend(0xFFF, 12), -1);
  CHECK_EQ(signExtend(0x80000, 20), -524288);
  CHECK_EQ(signExtend(0x7FFFFF, 24), 8388607);
  CHECK_EQ(signExtend(0xFFFFFF, 24), -1);
}

TEST(dps368_coef_bit_packing) {
  // c0 = 0x123, c1 = 0xFED (-19), c00 = 0x12345, c10 = 0xFEDCB (-4661),
  // c01 = 0x8000 (-32768), c11 = 0x0001, c20 = 0x7FFF, c21 = 0xFFFF, c30 = 0x0102
  const uint8_t r[18] = {0x12, 0x3F, 0xED, 0x12, 0x34, 0x5F, 0xED, 0xCB,
                         0x80, 0x00, 0x00, 0x01, 0x7F, 0xFF, 0xFF, 0xFF, 0x01, 0x02};
  Dps368Coef c = dps368ParseCoef(r);
  CHECK_EQ(c.c0, 0x123);
  CHECK_EQ(c.c1, -19);
  CHECK_EQ(c.c00, 0x12345);
  CHECK_EQ(c.c10, -4661);
  CHECK_EQ(c.c01, -32768);
  CHECK_EQ(c.c11, 1);
  CHECK_EQ(c.c20, 32767);
  CHECK_EQ(c.c21, -1);
  CHECK_EQ(c.c30, 0x0102);
}

TEST(dps368_compensation_reduces_to_c00_and_c0) {
  Dps368Coef c{};
  c.c0 = 50;      // T = c0/2 = 25 C at t_raw = 0
  c.c00 = 86000;  // P = c00 at p_raw = t_raw = 0
  CHECK_NEAR(dps368Temperature(c, 0, dps368ScaleFactor(1)), 25.0, 1e-6);
  CHECK_NEAR(dps368Pressure(c, 0, dps368ScaleFactor(8), 0, dps368ScaleFactor(1)), 86000.0, 1e-3);
  CHECK_NEAR(dps368ScaleFactor(8), 7864320.0, 0.5);
  CHECK_EQ((int)dps368ScaleFactor(3), 0);
}
