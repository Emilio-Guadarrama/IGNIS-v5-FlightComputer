#include "i2c_bus.h"
#include <Arduino.h>
#include <Wire.h>
#include "../config/config_v5.h"
#include "../config/pins_v5.h"

namespace ignis {

// Classic 9-clock stuck-bus recovery (reference Sensors.h:68-89). The
// reference's separate "precondition" step targeted the v2 board's floating
// BMP180 CSB and is not carried over (not applicable to the v5 sensor set).
static void nineClock() {
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  delayMicroseconds(50);
  pinMode(PIN_I2C_SCL, OUTPUT);
  for (int i = 0; i < 9; i++) {
    if (digitalRead(PIN_I2C_SDA) == HIGH) break;
    digitalWrite(PIN_I2C_SCL, LOW);
    delayMicroseconds(10);
    digitalWrite(PIN_I2C_SCL, HIGH);
    delayMicroseconds(10);
  }
  pinMode(PIN_I2C_SCL, INPUT_PULLUP);
  pinMode(PIN_I2C_SDA, INPUT_PULLUP);
  delayMicroseconds(50);
}

void i2cBusBegin() {
  nineClock();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  Wire.setTimeOut(I2C_TIMEOUT_MS);
}

void i2cBusRecover() {
  Wire.end();
  nineClock();
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_CLOCK_HZ);
  Wire.setTimeOut(I2C_TIMEOUT_MS);
}

bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom(addr, (size_t)len);
  if (got != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
  return true;
}

bool i2cProbe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

}  // namespace ignis
