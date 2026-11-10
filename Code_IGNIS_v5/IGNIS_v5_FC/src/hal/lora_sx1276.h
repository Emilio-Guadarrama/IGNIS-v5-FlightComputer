// =============================================================================
// IGNIS v5: lora_sx1276.h
// TX-only, polled SX1276 (RFM95W) driver + telemetry task.
//
// Why not RadioHead (reference Telemetry.h): it is not installed on this
// machine, and RH_RF95 services DIO0 by doing SPI transfers inside the ISR,
// which bypasses the SD/LoRa bus mutex required by requirement B. This driver
// never touches SPI from an ISR: the LoRa task polls RegIrqFlags.TxDone with
// the mutex held only for each short register access.
// RF plan unchanged: 915 MHz, SF9, BW125, CR4/5, sync 0x12, 17 dBm PA_BOOST,
// explicit header, CRC on, preamble 8, RadioHead 4-byte header emulated.
// =============================================================================
#pragma once
#include <stdint.h>

namespace ignis {

bool loraStart();          // reset + init radio, create task (task runs even if init failed: retries on pad)
bool loraOk();
bool loraLastTxOk();
uint32_t loraTxCount();
uint32_t loraSkipped();    // cycles skipped (bus busy or previous TX in progress)
uint32_t loraHeartbeat();

}  // namespace ignis
