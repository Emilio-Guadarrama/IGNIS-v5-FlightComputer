// =============================================================================
// IGNIS v5: selftest.h  (requirement C: OTA rollback validation)
// The app is built with verifyRollbackLater() = true, so the Arduino core does
// NOT auto-confirm a freshly OTA'd image. ~3 s after boot, loop() calls
// selftestService(), which runs the self-test:
//   IMU and baro detected and producing valid samples, SD logging, continuity
//   inputs configured (valid reading), flight task subscribed to the TWDT and
//   its loop counter advancing (watchdog alive).
// If the running image is PENDING_VERIFY:
//   pass -> esp_ota_mark_app_valid_cancel_rollback()
//   fail -> esp_ota_mark_app_invalid_rollback_and_reboot() (previous image,
//           which boots with its own fail-safe defaults)
// Never reboots on a RECOVERY boot or after flight is latched: there the image
// is simply left unconfirmed (the bootloader rolls back on the next reset).
// =============================================================================
#pragma once
#include <stdint.h>

namespace ignis {

struct SelfTestResult {
  bool imu = false, baro = false, sd = false, cont = false, wdt = false, loop = false;
  bool pass() const { return imu && baro && sd && cont && wdt && loop; }
};

void selftestService(uint32_t now_ms);   // call from loop(); acts once
const char* otaImageStateName();

}  // namespace ignis
