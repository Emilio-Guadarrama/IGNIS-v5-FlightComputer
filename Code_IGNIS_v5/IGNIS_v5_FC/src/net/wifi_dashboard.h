// =============================================================================
// IGNIS v5: wifi_dashboard.h  (requirement B)
// SoftAP + HTTP dashboard. READ-ONLY with respect to flight logic:
//   GET  /            HTML page
//   GET  /api/state   JSON snapshot (cached at WIFI_SNAPSHOT_HZ)
//   GET  /api/logs    SD file list        (SAFE only)
//   GET  /api/log?f=  SD file download    (SAFE only)
//   POST /ota         firmware upload     (IGNIS_ENABLE_OTA, gated, see ota_update.h)
// No arm / disarm / fire / test-fire / mode-toggle / disable endpoint exists.
//
// Power: the AP is OFF at boot. It starts only when the BOOT button (IO0) is
// held WIFI_BOOT_HOLD_MS in SAFE/ARMED, turns off after WIFI_IDLE_OFF_MS with
// no client, and shuts down permanently (until power cycle) at BOOST entry.
// Never started on a RECOVERY boot.
// =============================================================================
#pragma once
#include "../config/config_v5.h"
#include "../core/flight_types.h"

namespace ignis {

#if IGNIS_ENABLE_WIFI
WifiStatus wifiStatus();
void wifiDashboardStart();          // creates the core-0 task (AP stays off)
void wifiBackstop(uint32_t now_ms); // loop(): force WiFi off if the task missed the latch
#else
inline WifiStatus wifiStatus() { return WifiStatus::DISABLED_BUILD; }
inline void wifiDashboardStart() {}
inline void wifiBackstop(uint32_t) {}
#endif

}  // namespace ignis
