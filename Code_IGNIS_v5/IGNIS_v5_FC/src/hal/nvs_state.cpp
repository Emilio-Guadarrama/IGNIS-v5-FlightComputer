#include "nvs_state.h"
#include <Arduino.h>
#include <string.h>
#include "esp_attr.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace ignis {

static const char* kNs = "ignis5";
static nvs_handle_t s_nvs = 0;
static bool s_open = false;
static PersistedFlight s_cur;     // what is currently persisted

// ---- RTC mirror (survives SW/WDT/panic resets, not power-on) ----------------
static constexpr uint32_t kRtcMagic = 0x49474E36UL;  // 'IGN6' (layout v2)
struct RtcRecord {
  uint32_t magic;
  uint8_t phase, att1, att2, pad;
  float ground_pa;
  uint32_t check;
};
RTC_NOINIT_ATTR static RtcRecord s_rtc;

static uint32_t rtcCheck(const RtcRecord& r) {
  uint32_t g;
  memcpy(&g, &r.ground_pa, 4);
  return (r.magic ^ 0xA5A5A5A5u) + ((uint32_t)r.phase << 16) + ((uint32_t)r.att1 << 8) + r.att2 + g * 31u;
}

void rtcMirrorRead(PersistedFlight& out, bool& valid) {
  valid = s_rtc.magic == kRtcMagic && s_rtc.check == rtcCheck(s_rtc);
  out = PersistedFlight();
  if (!valid) return;
  out.phase = s_rtc.phase;
  out.attempts[0] = s_rtc.att1;
  out.attempts[1] = s_rtc.att2;
  out.ground_pa = s_rtc.ground_pa;
}

static void rtcWrite(const PersistedFlight& p) {
  RtcRecord r{};
  r.magic = kRtcMagic;
  r.phase = p.phase;
  r.att1 = p.attempts[0];
  r.att2 = p.attempts[1];
  r.ground_pa = p.ground_pa;
  r.check = rtcCheck(r);
  s_rtc = r;
}

// ---- NVS ----------------------------------------------------------------------
NvsBootRead nvsBootRead(bool power_on_reset) {
  NvsBootRead r;
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    if (!power_on_reset) return r;      // never erase a possible flight record
    if (nvs_flash_erase() != ESP_OK) return r;
    e = nvs_flash_init();
  }
  if (e != ESP_OK) return r;
  if (nvs_open(kNs, NVS_READWRITE, &s_nvs) != ESP_OK) return r;
  s_open = true;

  uint8_t phase = 0, a1 = 0, a2 = 0;
  uint32_t gbits = 0;
  uint16_t boots = 0;
  auto okOrMissing = [](esp_err_t x) { return x == ESP_OK || x == ESP_ERR_NVS_NOT_FOUND; };
  bool readOk = okOrMissing(nvs_get_u8(s_nvs, "phase", &phase)) &&
                okOrMissing(nvs_get_u8(s_nvs, "att1", &a1)) &&
                okOrMissing(nvs_get_u8(s_nvs, "att2", &a2)) &&
                okOrMissing(nvs_get_u32(s_nvs, "gndpa", &gbits)) &&
                okOrMissing(nvs_get_u16(s_nvs, "boots", &boots));
  if (!readOk) return r;
  r.pf.phase = phase;
  r.pf.attempts[0] = a1;
  r.pf.attempts[1] = a2;
  memcpy(&r.pf.ground_pa, &gbits, 4);

  // Boot counter: one write per boot (reference IGNIS_FC_heavy_13.ino:110-116).
  r.boot_count = (uint16_t)(boots + 1);
  if (nvs_set_u16(s_nvs, "boots", r.boot_count) == ESP_OK) nvs_commit(s_nvs);
  r.ok = true;
  return r;
}

void persistSeed(const PersistedFlight& pf) {
  s_cur = pf;
  rtcWrite(s_cur);
}

static bool nvsWriteAll() {
  if (!s_open) return false;
  uint32_t gbits;
  memcpy(&gbits, &s_cur.ground_pa, 4);
  bool ok = nvs_set_u8(s_nvs, "phase", s_cur.phase) == ESP_OK &&
            nvs_set_u8(s_nvs, "att1", s_cur.attempts[0]) == ESP_OK &&
            nvs_set_u8(s_nvs, "att2", s_cur.attempts[1]) == ESP_OK &&
            nvs_set_u32(s_nvs, "gndpa", gbits) == ESP_OK;
  return ok && nvs_commit(s_nvs) == ESP_OK;
}

bool persistClear() {
  s_cur = PersistedFlight();
  rtcWrite(s_cur);
  return nvsWriteAll();
}

bool persistPhase(uint8_t phase, float ground_pa) {
  bool changed = false;
  if (phase > s_cur.phase) { s_cur.phase = phase; changed = true; }   // phases only move forward
  if (ground_pa > 0 && ground_pa != s_cur.ground_pa) { s_cur.ground_pa = ground_pa; changed = true; }
  if (!changed) return true;           // transition-only write
  rtcWrite(s_cur);                     // instant
  return nvsWriteAll();                // flash (after the fact)
}

bool persistAttempts(uint8_t a1, uint8_t a2) {
  if (a1 == s_cur.attempts[0] && a2 == s_cur.attempts[1]) return true;
  s_cur.attempts[0] = a1;
  s_cur.attempts[1] = a2;
  if (s_cur.phase < 3) s_cur.phase = 3;  // PHASE_DESCENT
  rtcWrite(s_cur);
  return nvsWriteAll();
}

}  // namespace ignis
