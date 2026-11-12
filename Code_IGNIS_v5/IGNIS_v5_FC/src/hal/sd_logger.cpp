#include "sd_logger.h"
#include <SD.h>
#include <SPI.h>
#include <atomic>
#include "spi_bus.h"
#include "../config/config_v5.h"
#include "../config/pins_v5.h"
#include "../core/csv_row.h"

namespace ignis {

// core/sd_diag.h mirrors the SD library's card-type codes so it stays pure.
static_assert((uint8_t)CARD_NONE == kSdCardNone && (uint8_t)CARD_MMC == kSdCardMmc &&
              (uint8_t)CARD_SD == kSdCardSd && (uint8_t)CARD_SDHC == kSdCardSdhc &&
              (uint8_t)CARD_UNKNOWN == kSdCardUnknown,
              "sd_diag.h card-type codes no longer match the SD library (sd_defines.h)");

struct SdItem {
  FlightSnapshot snap;
  uint8_t flags;
};

static QueueHandle_t s_q = nullptr;
static File s_file;
static char s_path[24] = "";
static std::atomic<uint8_t> s_status{(uint8_t)SdStatus::NO_CARD};
static std::atomic<uint32_t> s_dropped{0};
static std::atomic<uint32_t> s_heartbeat{0};
static uint32_t s_last_flush_ms = 0;
static uint32_t s_last_mount_ms = 0;
static uint32_t s_write_errors = 0;
static char s_banner[200] = "";
static char s_row[kCsvRowMax];

// ---- Diagnostics (read-only reporting; never changes mount behavior) --------
// Atomics are read by the dashboard task. The records and the line buffer are
// touched only by setup() before the SD task exists, then only by the SD task.
static std::atomic<uint8_t> s_last_error{(uint8_t)SdError::NONE};    // most recent mount attempt / write event
static std::atomic<uint8_t> s_last_failure{(uint8_t)SdError::NONE};  // most recent non-NONE code this boot
static std::atomic<uint32_t> s_mount_attempts{0};
static std::atomic<uint32_t> s_fail_count{0};
static SdMountDiag s_first_fail;        // first failure this boot
static SdMountDiag s_last_fail;         // most recent failure this boot
static bool s_first_fail_in_bootlog = false;
static uint32_t s_fails_in_bootlog = 0; // s_fail_count when BOOTLOG.TXT was last appended
static char s_diag_line[240];

// Raw electrical level on SD_DETECT.
static int sdDetectRaw() { return digitalRead(PIN_SD_DETECT); }

// LOG-ONLY interpretation of the raw level under SD_DETECT_ACTIVE_LOW. Never
// gates mounting: the THD2528-11SD-GF card-detect switch polarity is
// UNVERIFIED (the repo datasheet is sheet 1 of 3 and has no switch circuit).
static bool isCardPresent() {
  int level = sdDetectRaw();
  return SD_DETECT_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
}

static void recordFailure(const SdMountDiag& d) {
  if (s_fail_count.load() == 0) s_first_fail = d;
  s_last_fail = d;
  s_fail_count++;
  s_last_failure = (uint8_t)d.err;
}

// Caller holds the SPI mutex and the card is mounted. Appends the first and
// the most recent failure seen this boot (RAM only: failures from before a
// reset are not known here). Each record is written once.
static void appendFailuresToBootlogLocked(File& b) {
  uint32_t fails = s_fail_count.load();
  if (fails == s_fails_in_bootlog) return;   // nothing new since the last append
  if (!s_first_fail_in_bootlog) {
    if (sdFormatDiag("sdfail first", s_first_fail, s_diag_line, sizeof(s_diag_line))) b.println(s_diag_line);
    s_first_fail_in_bootlog = true;
  }
  if (fails > 1) {
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "sdfail last n=%lu", (unsigned long)fails);
    if (sdFormatDiag(prefix, s_last_fail, s_diag_line, sizeof(s_diag_line))) b.println(s_diag_line);
  }
  s_fails_in_bootlog = fails;
}

// Caller holds the SPI mutex. Fills d.card_type / scan_ms / names_scanned.
static SdError mountAndOpenLocked(SdMountDiag& d) {
  SdMountSteps st;
  st.lock_ok = true;
  st.begin_ok = SD.begin(PIN_SD_CS, SPI, SD_SPI_HZ);
  if (!st.begin_ok) return sdClassifyMount(st);
  st.card_type = (uint8_t)SD.cardType();
  d.card_type = st.card_type;
  if (st.card_type == kSdCardNone) { SD.end(); return sdClassifyMount(st); }
  uint32_t scan_t0 = millis();
  for (int i = 0; i < 1000; i++) {
    snprintf(s_path, sizeof(s_path), "/V5F%04d.CSV", i);
    d.names_scanned = (uint16_t)(i + 1);
    if (!SD.exists(s_path)) { st.name_free = true; break; }
  }
  d.scan_ms = millis() - scan_t0;
  if (!st.name_free) { SD.end(); return sdClassifyMount(st); }
  s_file = SD.open(s_path, FILE_WRITE);
  st.open_ok = (bool)s_file;
  if (!st.open_ok) { SD.end(); return sdClassifyMount(st); }
  s_file.println(kCsvHeader);
  s_file.flush();
  // Boot identity sidecar (keeps the CSV itself machine-parseable).
  File b = SD.open("/BOOTLOG.TXT", FILE_APPEND);
  if (b) {
    b.printf("%s file=%s\n", s_banner, s_path);
    appendFailuresToBootlogLocked(b);
    b.close();
  }
  return sdClassifyMount(st);
}

static SdError tryMount() {
  SdMountDiag d;
  d.attempt = ++s_mount_attempts;
  d.t_ms = millis();
  d.cs_pin = PIN_SD_CS;
  d.spi_hz = SD_SPI_HZ;
  d.detect_raw = sdDetectRaw();
  {
    SpiGuard g(3000);  // mount can take ~2 s with no card: pad states only
    if (!g.ok) {
      SdMountSteps st;  // lock_ok = false: the card was never touched
      d.err = sdClassifyMount(st);
    } else {
      uint32_t held_t0 = millis();
      d.err = mountAndOpenLocked(d);
      bool ok = d.err == SdError::NONE;
      s_status = (uint8_t)(ok ? SdStatus::LOGGING : SdStatus::NO_CARD);
      if (ok) { s_write_errors = 0; s_last_flush_ms = millis(); }
      d.lock_held_ms = millis() - held_t0;
    }
  }  // mutex released before any serial output
  s_last_error = (uint8_t)d.err;
  if (d.err != SdError::NONE) recordFailure(d);
  if (sdFormatDiag("[SD]", d, s_diag_line, sizeof(s_diag_line))) Serial.println(s_diag_line);
  return d.err;
}

static void sdTask(void*) {
  SdItem it;
  for (;;) {
    s_heartbeat++;
    if (xQueueReceive(s_q, &it, pdMS_TO_TICKS(250)) != pdTRUE) continue;
    SdStatus st = (SdStatus)s_status.load();
    bool pad = isPadState(it.snap.state);
    if (st != SdStatus::LOGGING) {
      if (pad && st != SdStatus::CLOSED && millis() - s_last_mount_ms >= SD_MOUNT_RETRY_INTERVAL_MS) {
        s_last_mount_ms = millis();
        tryMount();  // prints one diagnostic line (raw SD_DETECT level included)
      }
      continue;  // row not logged (no card)
    }
    // After landing: 1 Hz, flushed every row (reference closed the file at
    // LANDED; v5 keeps logging until power cycle, requirement A).
    static uint32_t s_last_landed_row = 0;
    bool landed = it.snap.state == FlightState::LANDED;
    if (landed && it.snap.timestamp_ms - s_last_landed_row < 1000 && !(it.flags & SD_FLAG_FORCE_FLUSH)) continue;
    if (landed) s_last_landed_row = it.snap.timestamp_ms;

    size_t n = buildCsvRow(it.snap, s_row, sizeof(s_row));
    if (n == 0) continue;
    SpiGuard g(SPI_MUTEX_TIMEOUT_SD_MS);
    if (!g.ok) { s_dropped++; continue; }
    size_t w = s_file.write((const uint8_t*)s_row, n);
    w += s_file.write((const uint8_t*)"\n", 1);
    if (w != n + 1) {
      if (++s_write_errors >= 3) {
        SdMountDiag d;
        d.err = SdError::WRITE_ERRORS;
        d.attempt = s_mount_attempts.load();   // the mount this file belonged to
        d.t_ms = millis();
        d.cs_pin = PIN_SD_CS;
        d.spi_hz = SD_SPI_HZ;
        d.detect_raw = sdDetectRaw();
        d.card_type = (uint8_t)SD.cardType();  // cached by the library, no bus access
        s_file.close();
        SD.end();
        s_status = (uint8_t)SdStatus::ERROR;
        s_last_mount_ms = millis();
        s_last_error = (uint8_t)d.err;
        recordFailure(d);
        Serial.println("[SD] repeated write errors: logging stopped");
        if (sdFormatDiag("[SD]", d, s_diag_line, sizeof(s_diag_line))) Serial.println(s_diag_line);
      }
      continue;
    }
    s_write_errors = 0;
    uint32_t now = millis();
    bool due = (it.flags & SD_FLAG_FORCE_FLUSH) || landed ||
               ((it.flags & SD_FLAG_PYRO_WINDOW) && now - s_last_flush_ms >= PYRO_WINDOW_FLUSH_INTERVAL_MS) ||
               (!(it.flags & SD_FLAG_PYRO_WINDOW) && now - s_last_flush_ms >= LOG_FLUSH_INTERVAL_MS);
    if (due) { s_file.flush(); s_last_flush_ms = now; }
  }
}

bool sdLoggerStart(const char* boot_banner) {
  strncpy(s_banner, boot_banner, sizeof(s_banner) - 1);
  pinMode(PIN_SD_DETECT, INPUT);  // TODO(v5-unconfirmed): external pull assumed (reference R28)
  s_q = xQueueCreate(SD_QUEUE_DEPTH, sizeof(SdItem));
  if (!s_q) return false;
  // Raw level only. The interpretation is labeled as an assumption, not a fact.
  Serial.printf("[SD] SD_DETECT IO%d raw=%d (switch polarity UNVERIFIED; SD_DETECT_ACTIVE_LOW=%d would read this as card %s; not used to gate mounting)\n",
                PIN_SD_DETECT, sdDetectRaw(), SD_DETECT_ACTIVE_LOW, isCardPresent() ? "present" : "absent");
  s_last_mount_ms = millis();
  SdError err = tryMount();
  bool ok = err == SdError::NONE;
  if (ok) Serial.printf("[SD] logging to %s\n", s_path);
  else Serial.printf("[SD] not mounted (continuing without SD) err=%u(%s)\n", (unsigned)err, sdErrorName(err));
  xTaskCreatePinnedToCore(sdTask, "sd", 6144, nullptr, TASK_PRIO_SD, nullptr, TASK_CORE_SD);
  return ok;
}

bool sdEnqueue(const FlightSnapshot& s, uint8_t flags) {
  if (!s_q) return false;
  SdItem it;
  it.snap = s;
  it.flags = flags;
  if (xQueueSend(s_q, &it, 0) != pdTRUE) { s_dropped++; return false; }
  return true;
}

SdStatus sdStatus() { return (SdStatus)s_status.load(); }
uint32_t sdRowsDropped() { return s_dropped.load(); }
uint32_t sdHeartbeat() { return s_heartbeat.load(); }
const char* sdCurrentFile() { return s_path; }
SdError sdLastError() { return (SdError)s_last_error.load(); }
SdError sdLastFailure() { return (SdError)s_last_failure.load(); }
uint32_t sdMountAttempts() { return s_mount_attempts.load(); }
uint32_t sdFailureCount() { return s_fail_count.load(); }

bool sdListLogs(SdListCb cb, void* ctx) {
  if (sdStatus() != SdStatus::LOGGING) return false;
  SpiGuard g(SPI_MUTEX_TIMEOUT_SD_MS);
  if (!g.ok) return false;
  File root = SD.open("/");
  if (!root) return false;
  int count = 0;
  for (File f = root.openNextFile(); f && count < 64; f = root.openNextFile(), count++) {
    if (!f.isDirectory()) cb(f.name(), (uint32_t)f.size(), ctx);
    f.close();
  }
  root.close();
  return true;
}

int32_t sdReadChunk(const char* path, uint32_t offset, uint8_t* buf, size_t len, uint32_t* total) {
  if (sdStatus() != SdStatus::LOGGING) return -1;
  if (!path || path[0] != '/' || strstr(path, "..")) return -1;
  SpiGuard g(SPI_MUTEX_TIMEOUT_SD_MS);
  if (!g.ok) return -1;
  File f = SD.open(path, FILE_READ);
  if (!f) return -1;
  if (total) *total = (uint32_t)f.size();
  int32_t n = -1;
  if (f.seek(offset)) n = (int32_t)f.read(buf, len);
  f.close();
  return n;
}

}  // namespace ignis
