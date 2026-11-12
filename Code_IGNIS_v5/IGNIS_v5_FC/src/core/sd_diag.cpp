#include "sd_diag.h"
#include <stdio.h>

namespace ignis {

const char* sdErrorName(SdError e) {
  switch (e) {
    case SdError::NONE: return "NONE";
    case SdError::SD_BEGIN_FAILED: return "SD_BEGIN_FAILED";
    case SdError::CARD_TYPE_NONE: return "CARD_TYPE_NONE";
    case SdError::NAME_SPACE_EXHAUSTED: return "NAME_SPACE_EXHAUSTED";
    case SdError::OPEN_FAILED: return "OPEN_FAILED";
    case SdError::LOCK_TIMEOUT: return "LOCK_TIMEOUT";
    case SdError::WRITE_ERRORS: return "WRITE_ERRORS";
  }
  return "UNKNOWN";
}

const char* sdCardTypeName(uint8_t t) {
  switch (t) {
    case kSdCardNone: return "NONE";
    case kSdCardMmc: return "MMC";
    case kSdCardSd: return "SD";
    case kSdCardSdhc: return "SDHC";
    case kSdCardUnknown: return "UNKNOWN";
    case kSdCardNotRead: return "NOT_READ";
  }
  return "INVALID";
}

SdError sdClassifyMount(const SdMountSteps& st) {
  if (!st.lock_ok) return SdError::LOCK_TIMEOUT;
  if (!st.begin_ok) return SdError::SD_BEGIN_FAILED;
  if (st.card_type == kSdCardNone) return SdError::CARD_TYPE_NONE;
  if (!st.name_free) return SdError::NAME_SPACE_EXHAUSTED;
  if (!st.open_ok) return SdError::OPEN_FAILED;
  return SdError::NONE;
}

size_t sdFormatDiag(const char* prefix, const SdMountDiag& d, char* buf, size_t len) {
  if (!buf || len == 0) return 0;
  int n = snprintf(buf, len,
                   "%s t=%lums mount#%lu err=%u(%s) cs=IO%u det_raw=%d card=%u(%s) hz=%lu held_ms=%lu scan_ms=%lu names=%u",
                   prefix ? prefix : "", (unsigned long)d.t_ms, (unsigned long)d.attempt,
                   (unsigned)d.err, sdErrorName(d.err), (unsigned)d.cs_pin, d.detect_raw,
                   (unsigned)d.card_type, sdCardTypeName(d.card_type), (unsigned long)d.spi_hz,
                   (unsigned long)d.lock_held_ms, (unsigned long)d.scan_ms, (unsigned)d.names_scanned);
  if (n < 0 || (size_t)n >= len) { buf[0] = '\0'; return 0; }
  return (size_t)n;
}

}  // namespace ignis
