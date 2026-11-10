// =============================================================================
// IGNIS v5: pyro_hw.h  SAFETY-CRITICAL
// GPIO layer for the two pyro channels. Decision logic lives in
// core/pyro_logic.h (host-tested). This file only:
//   - drives both gates LOW as the very first thing at boot (fail-safe default)
//   - mirrors PyroLogic::gateOn() onto the gate pins every tick
//   - reads continuity (digital, through the optocouplers)
// There is NO API to disable, kill or test-fire. The only way to make the
// board safe is the RBF power cut.
// =============================================================================
#pragma once
#include <stdint.h>
#include "../core/flight_types.h"
#include "../core/pyro_logic.h"

namespace ignis {

void pyroHwInitSafe();                       // call first in setup()
Continuity pyroReadContinuity();
// Request a channel. Returns true if the gate was asserted now.
bool pyroFire(PyroLogic& logic, PyroChannel ch, uint32_t now_ms);
// Call every flight tick: ends pulses, re-asserts LOW on idle channels.
void pyroService(PyroLogic& logic, uint32_t now_ms);

}  // namespace ignis
