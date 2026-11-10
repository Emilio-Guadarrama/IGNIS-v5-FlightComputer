// =============================================================================
// IGNIS v5: secrets_example.h  (TEMPLATE, committed)
//
// Copy this file to "secrets.h" in the same folder and set real values.
// secrets.h is listed in .gitignore and must never be committed.
// The build fails if secrets.h is missing while WiFi/OTA are enabled.
// =============================================================================
#pragma once

// SoftAP WPA2 passphrase: 8 to 63 printable ASCII characters.
#define IGNIS_WIFI_AP_PASSWORD   "change-me-wifi"

// OTA upload password (separate from the WiFi password): 8 to 63 characters.
#define IGNIS_OTA_PASSWORD       "change-me-ota"
