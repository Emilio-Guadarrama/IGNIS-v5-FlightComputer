// =============================================================================
// IGNIS v5: flight_task.h  SAFETY-CRITICAL
// 50 Hz flight loop on core 1: sensors -> FSM -> pyro -> snapshot -> SD queue.
// Subscribed to the task watchdog; fed only at the end of a full iteration
// so a hang anywhere in the flight path trips the watchdog.
// It never touches SPI, WiFi or flash except the two one-shot NVS flag writes.
// =============================================================================
#pragma once
#include <Arduino.h>

namespace ignis {

// persisted_fired_mask: channels commanded before this reset (bit0 CH1, bit1 CH2).
void flightTaskStart(bool recovery_boot, uint8_t persisted_fired_mask);
TaskHandle_t flightTaskHandle();

}  // namespace ignis
