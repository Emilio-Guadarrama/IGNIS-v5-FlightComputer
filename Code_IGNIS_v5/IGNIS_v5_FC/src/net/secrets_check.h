// Pulls in the gitignored secrets header and validates it at compile time.
#pragma once
#include "../config/config_v5.h"

#if IGNIS_ENABLE_WIFI
#if defined(__has_include)
#if __has_include("../../secrets.h")
#include "../../secrets.h"
#else
#error "secrets.h missing: copy secrets_example.h to secrets.h (gitignored) and set the passwords."
#endif
#endif

#ifndef IGNIS_WIFI_AP_PASSWORD
#error "secrets.h must define IGNIS_WIFI_AP_PASSWORD"
#endif
static_assert(sizeof(IGNIS_WIFI_AP_PASSWORD) - 1 >= 8 && sizeof(IGNIS_WIFI_AP_PASSWORD) - 1 <= 63,
              "IGNIS_WIFI_AP_PASSWORD must be 8-63 characters (WPA2)");

#if IGNIS_ENABLE_OTA
#ifndef IGNIS_OTA_PASSWORD
#error "secrets.h must define IGNIS_OTA_PASSWORD"
#endif
static_assert(sizeof(IGNIS_OTA_PASSWORD) - 1 >= 8 && sizeof(IGNIS_OTA_PASSWORD) - 1 <= 63,
              "IGNIS_OTA_PASSWORD must be 8-63 characters");
#endif
#endif
