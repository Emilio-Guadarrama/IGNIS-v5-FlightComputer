// =============================================================================
// IGNIS v5: sd_logger.h
// SD logging task. The flight task enqueues snapshots (never blocks: a full
// queue drops the row and counts it). This task formats CSV and writes under
// the SPI mutex, one row per lock.
// Flush policy (reference DataLogger.h:203-232):
//   - force flush on every state transition
//   - every PYRO_WINDOW_FLUSH_INTERVAL_MS inside the PYRO_FORCE_FLUSH_MS window
//     after any fire command
//   - otherwise every LOG_FLUSH_INTERVAL_MS
// Mount retries only in pad states (reference DataLogger.h:156-178): a missing
// card can block SD.begin() ~2 s, which must never happen in flight.
// =============================================================================
#pragma once
#include <Arduino.h>
#include "../core/flight_types.h"

namespace ignis {

enum SdItemFlags : uint8_t {
  SD_FLAG_FORCE_FLUSH = 1 << 0,   // state transition
  SD_FLAG_PYRO_WINDOW = 1 << 1,   // inside the post-fire forced-flush window
};

bool sdLoggerStart(const char* boot_banner);   // mount + create task
bool sdEnqueue(const FlightSnapshot& s, uint8_t flags);  // non-blocking
SdStatus sdStatus();
uint32_t sdRowsDropped();
uint32_t sdHeartbeat();
const char* sdCurrentFile();

// Dashboard support (callers must enforce state == SAFE).
typedef void (*SdListCb)(const char* name, uint32_t size, void* ctx);
bool sdListLogs(SdListCb cb, void* ctx);
// Reads up to len bytes at offset; returns bytes read or -1 on error.
int32_t sdReadChunk(const char* path, uint32_t offset, uint8_t* buf, size_t len, uint32_t* total_size);

}  // namespace ignis
