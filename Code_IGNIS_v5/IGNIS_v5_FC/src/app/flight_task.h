// =============================================================================
// IGNIS v5 SINGLE-DEPLOY: flight_task.h  SAFETY-CRITICAL
// 50 Hz flight loop on core 1: sensors -> FSM -> pyro sequencer -> GPIO ->
// persist -> snapshot -> SD queue. Subscribed to the task watchdog and fed
// only at the end of a full iteration, so a hang anywhere in the flight path
// trips the watchdog. Never touches SPI or WiFi; flash only for the
// transition-only NVS writes (always AFTER the gate GPIO is set).
// =============================================================================
#pragma once
#include <Arduino.h>
#include "../core/boot_policy.h"

namespace ignis {

void flightTaskStart(const BootDecision& boot);
TaskHandle_t flightTaskHandle();

}  // namespace ignis
