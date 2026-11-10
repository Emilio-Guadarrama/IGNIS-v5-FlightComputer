// =============================================================================
// IGNIS v5: ota_update.h  (requirement C)  SAFETY-CRITICAL GATE
// POST /ota (multipart file field), HTTP digest auth user "ota" with
// IGNIS_OTA_PASSWORD, header "X-Image-SHA256: <64 hex>" = SHA-256 of the .bin.
// Integrity: SHA-256 computed over the received bytes must match the header
// BEFORE the boot partition is switched; esp_ota_end() also verifies the
// image's own checksum/appended hash.
// Gate (core/ota_gate.h) is evaluated at start, on every chunk, and before the
// boot partition switch; any denial aborts with esp_ota_abort(). A snapshot
// older than 200 ms counts as an invalid continuity reading (fail closed).
// =============================================================================
#pragma once
#include "../config/config_v5.h"

#if IGNIS_ENABLE_OTA
class WebServer;
namespace ignis {
void otaRegister(WebServer& server);
void otaPoll();                 // WiFi task loop: abort a stalled upload if the gate closes
uint32_t otaCurrentDenyMask();
}  // namespace ignis
#endif
