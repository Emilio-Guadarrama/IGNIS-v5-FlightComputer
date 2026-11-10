// =============================================================================
// IGNIS v5: spi_bus.h
// The microSD and the RFM95W share one SPI bus (v5 Main.SchDoc:
// SNS_SPI_MOSI/MISO/SCK, same as reference DataLogger.h:34-42).
// Every transaction on the bus, from any task, holds this mutex.
// Holders keep it for ONE bounded operation (one SD write/flush, one radio
// register burst) and release it between operations, so the LoRa task waits
// at most one SD operation, and the SD task (including the forced post-pyro
// flush) waits at most one radio register burst (< 1 ms).
// The flight task never touches this bus.
// =============================================================================
#pragma once
#include <Arduino.h>

namespace ignis {

void spiBusBegin();                     // SPI.begin + mutex + CS lines idle high
bool spiLock(uint32_t timeout_ms);
void spiUnlock();

struct SpiGuard {
  explicit SpiGuard(uint32_t timeout_ms) : ok(spiLock(timeout_ms)) {}
  ~SpiGuard() { if (ok) spiUnlock(); }
  SpiGuard(const SpiGuard&) = delete;
  SpiGuard& operator=(const SpiGuard&) = delete;
  bool ok;
};

}  // namespace ignis
