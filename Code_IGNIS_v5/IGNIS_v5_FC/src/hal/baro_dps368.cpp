#include "baro_dps368.h"
#include <Arduino.h>
#include "i2c_bus.h"
#include "../config/config_v5.h"
#include "../config/pins_v5.h"
#include "../core/dps368_math.h"
#include "../core/flight_fsm.h"

// TODO(v5-verify): DPS3xx register map (PRS_B2 0x00, TMP_B2 0x03, PRS_CFG 0x06,
// TMP_CFG 0x07, MEAS_CFG 0x08, CFG_REG 0x09, RESET 0x0C, ID 0x0D, COEF 0x10,
// COEF_SRCE 0x28). Verify against infineon-dps368-datasheet-en.pdf.
namespace {
constexpr uint8_t REG_PSR_B2 = 0x00;
constexpr uint8_t REG_PRS_CFG = 0x06;
constexpr uint8_t REG_TMP_CFG = 0x07;
constexpr uint8_t REG_MEAS_CFG = 0x08;
constexpr uint8_t REG_CFG = 0x09;
constexpr uint8_t REG_RESET = 0x0C;
constexpr uint8_t REG_ID = 0x0D;
constexpr uint8_t REG_COEF = 0x10;
constexpr uint8_t REG_COEF_SRCE = 0x28;
constexpr uint8_t PROD_ID = 0x0;          // low nibble of 0x0D

// ASSUMPTION(v5): pressure 32 Hz x 8 oversampling (~16.4 ms each), temperature
// 4 Hz x 1. Sum of measurement time per second stays < 1 s as required.
constexpr uint8_t PM_RATE_32 = 0x50, PM_PRC_8 = 0x03;
constexpr uint8_t TMP_RATE_4 = 0x20, TMP_PRC_1 = 0x00;
constexpr uint8_t OSR_P = 8, OSR_T = 1;
constexpr uint8_t MEAS_CONT_PT = 0x07;

uint8_t s_addr = 0;
bool s_ok = false;
ignis::Dps368Coef s_coef;
float s_kP = 0, s_kT = 0;

int32_t be24(const uint8_t* b) {
  return ignis::signExtend(((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2], 24);
}
}  // namespace

namespace ignis {

bool baroOk() { return s_ok; }
uint8_t baroAddress() { return s_addr; }

bool baroBegin() {
  s_ok = false;
  s_addr = 0;
  const uint8_t addrs[2] = {DPS368_ADDR_PRIMARY, DPS368_ADDR_ALT};
  for (uint8_t a : addrs) {
    uint8_t id = 0xFF;
    if (i2cRead(a, REG_ID, &id, 1) && (id & 0x0F) == PROD_ID && id != 0x00 && id != 0xFF) { s_addr = a; break; }
    // id 0x10 expected (rev 1, prod 0). 0x00/0xFF rejected as bus garbage.
  }
  if (!s_addr) { Serial.println("[BARO] DPS368 not found at 0x77/0x76"); return false; }

  i2cWrite8(s_addr, REG_RESET, 0x09);  // soft reset
  delay(40);
  // Wait for coefficients + sensor ready (bounded).
  uint8_t meas = 0;
  uint32_t t0 = millis();
  do {
    if (!i2cRead(s_addr, REG_MEAS_CFG, &meas, 1)) meas = 0;
    if ((meas & 0xC0) == 0xC0) break;
    delay(5);
  } while (millis() - t0 < 200);
  if ((meas & 0xC0) != 0xC0) { Serial.println("[BARO] not ready (COEF_RDY/SENSOR_RDY)"); return false; }

  uint8_t raw[18];
  if (!i2cRead(s_addr, REG_COEF, raw, 18)) return false;
  s_coef = dps368ParseCoef(raw);
  uint8_t src = 0;
  if (!i2cRead(s_addr, REG_COEF_SRCE, &src, 1)) return false;
  uint8_t tmp_ext = src & 0x80;  // use the same temperature sensor the coefficients came from

  bool ok = i2cWrite8(s_addr, REG_PRS_CFG, PM_RATE_32 | PM_PRC_8) &&
            i2cWrite8(s_addr, REG_TMP_CFG, tmp_ext | TMP_RATE_4 | TMP_PRC_1) &&
            i2cWrite8(s_addr, REG_CFG, 0x00) &&          // no P/T shift needed at <= 8x
            i2cWrite8(s_addr, REG_MEAS_CFG, MEAS_CONT_PT);
  if (!ok) return false;
  s_kP = dps368ScaleFactor(OSR_P);
  s_kT = dps368ScaleFactor(OSR_T);
  delay(100);  // first conversions
  s_ok = true;
  Serial.printf("[BARO] DPS368 @0x%02X: 32 Hz x8 pressure, continuous\n", s_addr);
  return true;
}

bool baroRead(FlightSnapshot& s) {
  if (!s_ok) return false;
  uint8_t b[6];
  if (!i2cRead(s_addr, REG_PSR_B2, b, 6)) return false;
  int32_t p_raw = be24(b), t_raw = be24(b + 3);
  float t = dps368Temperature(s_coef, t_raw, s_kT);
  float p = dps368Pressure(s_coef, p_raw, s_kP, t_raw, s_kT);
  // Same generous sanity window as the reference (Sensors.h:236-241).
  if (!(p >= 30000.0f && p <= 110000.0f)) return false;
  if (!(t > -45.0f && t < 90.0f)) return false;
  s.pressure_pa = p;
  s.baro_temp_c = t;
  s.baro_alt_m = altitudeFromPressure(p, SEA_LEVEL_PRESSURE_PA);
  return true;
}

}  // namespace ignis
