#include "sd_logger.h"
#include <SD.h>
#include <SPI.h>
#include <atomic>
#include "spi_bus.h"
#include "../config/config_v5.h"
#include "../config/pins_v5.h"
#include "../core/csv_row.h"

namespace ignis {

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

static bool isCardPresent() {
  int level = digitalRead(PIN_SD_DETECT);
  return SD_DETECT_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
}

// Caller holds the SPI mutex.
static bool mountAndOpenLocked() {
  if (!SD.begin(PIN_SD_CS, SPI, SD_SPI_HZ)) return false;
  if (SD.cardType() == CARD_NONE) { SD.end(); return false; }
  for (int i = 0; i < 1000; i++) {
    snprintf(s_path, sizeof(s_path), "/V5F%04d.CSV", i);
    if (!SD.exists(s_path)) break;
    if (i == 999) { SD.end(); return false; }
  }
  s_file = SD.open(s_path, FILE_WRITE);
  if (!s_file) { SD.end(); return false; }
  s_file.println(kCsvHeader);
  s_file.flush();
  // Boot identity sidecar (keeps the CSV itself machine-parseable).
  File b = SD.open("/BOOTLOG.TXT", FILE_APPEND);
  if (b) { b.printf("%s file=%s\n", s_banner, s_path); b.close(); }
  return true;
}

static bool tryMount() {
  SpiGuard g(3000);  // mount can take ~2 s with no card: pad states only
  if (!g.ok) return false;
  bool ok = mountAndOpenLocked();
  s_status = (uint8_t)(ok ? SdStatus::LOGGING : SdStatus::NO_CARD);
  if (ok) { s_write_errors = 0; s_last_flush_ms = millis(); }
  return ok;
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
        Serial.printf("[SD] mount retry (detect=%s)\n", isCardPresent() ? "present" : "absent");
        tryMount();
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
        s_file.close();
        SD.end();
        s_status = (uint8_t)SdStatus::ERROR;
        s_last_mount_ms = millis();
        Serial.println("[SD] repeated write errors: logging stopped");
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
  s_last_mount_ms = millis();
  bool ok = tryMount();
  Serial.printf("[SD] %s %s\n", ok ? "logging to" : "not mounted", ok ? s_path : "(continuing without SD)");
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
