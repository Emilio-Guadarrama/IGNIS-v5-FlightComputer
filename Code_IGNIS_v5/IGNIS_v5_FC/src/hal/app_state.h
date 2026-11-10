// =============================================================================
// IGNIS v5: app_state.h
// Cross-task shared state. The flight task is the only writer of the snapshot.
// Every other task reads a copy taken inside a short critical section
// (a ~200-byte memcpy), so no reader can ever stall the flight task.
// =============================================================================
#pragma once
#include <Arduino.h>
#include <atomic>
#include "../core/flight_types.h"

namespace ignis {

void snapshotPublish(const FlightSnapshot& s);   // flight task only
void snapshotRead(FlightSnapshot& out);          // any task

// One-way latches. Set by the flight task / boot code, read by WiFi/OTA/main.
extern std::atomic<bool> g_flight_latched;   // BOOST seen this boot (or recovery boot)
extern std::atomic<bool> g_post_fire;        // a pyro channel was commanded (this boot or persisted)
extern std::atomic<bool> g_recovery_boot;    // booted into RECOVERY
extern std::atomic<uint32_t> g_flight_loop_count;  // flight-task heartbeat

// Boot identity (filled once in setup, read-only afterwards)
struct BootInfo {
  uint8_t reset_reason = 0;
  uint16_t boot_count = 0;
  char app_sha256[65] = "unknown";
  const char* boot_why = "";
};
extern BootInfo g_boot;

}  // namespace ignis
