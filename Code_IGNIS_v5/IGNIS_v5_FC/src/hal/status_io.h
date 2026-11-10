// =============================================================================
// IGNIS v5: status_io.h
// 3x WS2812B (RMT, built into Arduino-ESP32 3.x: no NeoPixel library) and the
// piezo buzzer (LEDC). Non-blocking; called from the low-priority loop().
//   LED0 sensors   green ok / yellow one degraded / red both failed
//   LED1 comms/SD  green both / yellow one / red none
//   LED2 state     green SAFE / red ARMED / blue flight / white LANDED / magenta RECOVERY
// Buzzer (pad only, plus RECOVERY/LANDED locator):
//   SAFE: 1 short beep / 3 s   ARMED: 2 beeps / 1 s   NO-GO: low 3-tone / 2 s
//   RECOVERY or LANDED: 1 long beep / 4 s (recovery locator)
// =============================================================================
#pragma once
#include "../core/flight_types.h"

namespace ignis {

void statusBegin();
void statusUpdate(const FlightSnapshot& s, uint32_t now_ms);

}  // namespace ignis
