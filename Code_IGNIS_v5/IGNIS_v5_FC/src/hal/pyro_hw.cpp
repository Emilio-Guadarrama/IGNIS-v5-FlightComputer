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

void pyroDrive(const PyroSequencer& seq) {
  // Mirror the sequencer every tick: HIGH only while that channel's attempt
  // pulse is active, LOW otherwise (pulse end, lockout, idle).
  digitalWrite(PIN_PYRO1_GATE, seq.gateOn(PYRO_CH1_PRIMARY) ? HIGH : LOW);
  digitalWrite(PIN_PYRO2_GATE, seq.gateOn(PYRO_CH2_BACKUP) ? HIGH : LOW);
}

}  // namespace ignis
