#include "pyro_hw.h"
#include <Arduino.h>
#include "driver/gpio.h"
#include "../config/config_v5.h"
#include "../config/pins_v5.h"

namespace ignis {

static bool s_cont_pins_ok = false;

void pyroHwInitSafe() {
  // Gates LOW before the pin becomes an output, so it never glitches high.
  digitalWrite(PIN_PYRO1_GATE, LOW);
  digitalWrite(PIN_PYRO2_GATE, LOW);
  pinMode(PIN_PYRO1_GATE, OUTPUT);
  pinMode(PIN_PYRO2_GATE, OUTPUT);
  digitalWrite(PIN_PYRO1_GATE, LOW);
  digitalWrite(PIN_PYRO2_GATE, LOW);
#if PYRO_GATE_REDUCED_DRIVE
  // Kept from reference Pyro.h:83-104 (partial mitigation documented there).
  // Bench item: verify 5/5 reliable switching on the flight battery.
  gpio_set_drive_capability((gpio_num_t)PIN_PYRO1_GATE, GPIO_DRIVE_CAP_1);
  gpio_set_drive_capability((gpio_num_t)PIN_PYRO2_GATE, GPIO_DRIVE_CAP_1);
#endif
  // Continuity inputs: no internal pulls (external opto pull network).
  pinMode(PIN_PYRO1_CONT, INPUT);
  pinMode(PIN_PYRO2_CONT, INPUT);
  s_cont_pins_ok = true;
}

Continuity pyroReadContinuity() {
  Continuity c;
  // valid=false (fail closed: treated as armed) until the pins are configured.
  c.valid = s_cont_pins_ok;
  c.ch1 = digitalRead(PIN_PYRO1_CONT) == PYRO_CONT_ACTIVE_LEVEL;
  c.ch2 = digitalRead(PIN_PYRO2_CONT) == PYRO_CONT_ACTIVE_LEVEL;
  return c;
}

static inline uint8_t gatePin(PyroChannel ch) {
  return ch == PYRO_CH1_DROGUE ? PIN_PYRO1_GATE : PIN_PYRO2_GATE;
}

bool pyroFire(PyroLogic& logic, PyroChannel ch, uint32_t now_ms) {
  if (!logic.request(ch, now_ms)) return false;  // lockout / one-shot enforced here
  digitalWrite(gatePin(ch), HIGH);
  return true;
}

void pyroService(PyroLogic& logic, uint32_t now_ms) {
  logic.service(now_ms);
  // Mirror the logic state every tick: any channel not actively pulsing is
  // driven LOW again (covers lockout and pulse end).
  if (!logic.gateOn(PYRO_CH1_DROGUE)) digitalWrite(PIN_PYRO1_GATE, LOW);
  if (!logic.gateOn(PYRO_CH2_MAIN)) digitalWrite(PIN_PYRO2_GATE, LOW);
}

}  // namespace ignis
