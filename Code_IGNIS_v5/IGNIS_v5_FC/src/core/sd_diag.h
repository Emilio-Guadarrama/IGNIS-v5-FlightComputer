// =============================================================================
// IGNIS v5: sd_diag.h  (pure C++, host-testable)
// SD mount diagnostics: the error code, the mapping from mount steps to that
// code, and the one-line text used on serial and in /BOOTLOG.TXT.
// No Arduino or ESP-IDF headers here: hal/sd_logger.cpp feeds it plain values.
// Diagnostics only. Nothing here changes when or how the card is mounted.
// =============================================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ignis {

// Numeric values are printed on serial, in BOOTLOG.TXT and on the dashboard.
// Append new codes at the end; never renumber.
enum class SdError : uint8_t {
  NONE                 = 0,  // mounted, log file open
  SD_BEGIN_FAILED      = 1,  // SD.begin() false: no card response, or FAT mount failed
  CARD_TYPE_NONE       = 2,  // SD.begin() true but cardType() == CARD_NONE
  NAME_SPACE_EXHAUSTED = 3,  // /V5F0000.CSV .. /V5F0999.CSV all exist
  OPEN_FAILED          = 4,  // SD.open(new file, FILE_WRITE) failed
  LOCK_TIMEOUT         = 5,  // SPI mutex not acquired: the card was never touched
  WRITE_ERRORS         = 6,  // repeated short writes while logging: file closed
};
constexpr uint8_t kSdErrorCount = 7;

const char* sdErrorName(SdError e);

// SD.cardType() codes. Mirrors sdcard_type_t in the Arduino-ESP32 3.3.12 SD
// library (sd_defines.h); hal/sd_logger.cpp static_asserts that they match.
constexpr uint8_t kSdCardNone = 0, kSdCardMmc = 1, kSdCardSd = 2, kSdCardSdhc = 3, kSdCardUnknown = 4;
constexpr uint8_t kSdCardNotRead = 0xFF;   // cardType() was not reached on this attempt
const char* sdCardTypeName(uint8_t t);

// Outcome of each mount step, in the order the steps run. A step that was not
// reached keeps its default.
struct SdMountSteps {
  bool lock_ok = false;                // SPI mutex acquired
  bool begin_ok = false;               // SD.begin() returned true
  uint8_t card_type = kSdCardNotRead;  // SD.cardType()
  bool name_free = false;              // a free /V5Fnnnn.CSV name was found
  bool open_ok = false;                // the new log file opened
};

// First failed step decides the code (later fields are ignored).
SdError sdClassifyMount(const SdMountSteps& st);

// Everything one diagnostic line carries.
struct SdMountDiag {
  SdError err = SdError::NONE;
  uint32_t attempt = 0;        // mount-attempt counter (1 = the boot attempt)
  uint32_t t_ms = 0;           // millis() when the event happened
  uint8_t cs_pin = 0;          // PIN_SD_CS
  int detect_raw = -1;         // raw digitalRead(PIN_SD_DETECT): 0 / 1, -1 = not read
  uint8_t card_type = kSdCardNotRead;
  uint32_t spi_hz = 0;         // SD_SPI_HZ
  uint32_t lock_held_ms = 0;   // how long this attempt held the SPI mutex
  uint32_t scan_ms = 0;        // of that, the SD.exists() free-name scan
  uint16_t names_scanned = 0;  // SD.exists() calls in that scan
};

// One line, no trailing newline:
//   <prefix> t=<ms>ms mount#<n> err=<code>(<NAME>) cs=IO<pin> det_raw=<0|1|-1>
//   card=<code>(<NAME>) hz=<hz> held_ms=<ms> scan_ms=<ms> names=<n>
// det_raw is the raw pin level. It deliberately does not say "present" or
// "absent": the THD2528-11SD-GF card-detect switch polarity is unverified.
// Returns chars written (excluding NUL); 0 and an empty string on truncation.
size_t sdFormatDiag(const char* prefix, const SdMountDiag& d, char* buf, size_t len);

}  // namespace ignis
