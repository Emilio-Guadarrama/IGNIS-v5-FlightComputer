#include "nvs_state.h"
#include <Arduino.h>
#include "esp_attr.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace ignis {

static const char* kNs = "ignis5";
static nvs_handle_t s_nvs = 0;
static bool s_open = false;
static bool s_flight_written = false;
static bool s_fire_written = false;
static uint8_t s_fire_mask = 0;   // bit0 = CH1 drogue, bit1 = CH2 main

// ---- RTC mirror ---------------------------------------------------------------
static constexpr uint32_t kRtcMagic = 0x49474E35UL;  // 'IGN5'
struct RtcFlags { uint32_t magic; uint8_t flight; uint8_t post_fire; uint8_t inv_flight; uint8_t inv_post_fire; };
RTC_NOINIT_ATTR static RtcFlags s_rtc;

void rtcMirrorRead(PersistFlags& out, bool& valid) {
  valid = (s_rtc.magic == kRtcMagic) &&
          (uint8_t)~s_rtc.flight == s_rtc.inv_flight &&
          (uint8_t)~s_rtc.post_fire == s_rtc.inv_post_fire;
  out.flight_in_progress = valid && s_rtc.flight;
  out.post_fire = valid && s_rtc.post_fire;
}

void rtcMirrorSet(const PersistFlags& f) {
  s_rtc.flight = f.flight_in_progress ? 1 : 0;
  s_rtc.post_fire = f.post_fire ? 1 : 0;
  s_rtc.inv_flight = (uint8_t)~s_rtc.flight;
  s_rtc.inv_post_fire = (uint8_t)~s_rtc.post_fire;
  s_rtc.magic = kRtcMagic;
}

void rtcMirrorClear() { rtcMirrorSet(PersistFlags{}); }

// ---- NVS ----------------------------------------------------------------------
NvsBootRead nvsBootRead(bool power_on_reset) {
  NvsBootRead r;
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // Erase ONLY on a power-on reset, where the flags would be cleared anyway.
    // On any other reset, erasing could destroy a post-fire flag: report
    // not-ok and let boot_policy fail closed (RECOVERY).
    if (!power_on_reset) return r;
    if (nvs_flash_erase() != ESP_OK) return r;
    e = nvs_flash_init();
  }
  if (e != ESP_OK) return r;
  if (nvs_open(kNs, NVS_READWRITE, &s_nvs) != ESP_OK) return r;
  s_open = true;

  uint8_t flight = 0, fire = 0;
  uint16_t boots = 0;
  esp_err_t e1 = nvs_get_u8(s_nvs, "flight", &flight);
  esp_err_t e2 = nvs_get_u8(s_nvs, "postfire", &fire);
  esp_err_t e3 = nvs_get_u16(s_nvs, "boots", &boots);
  bool readOk = (e1 == ESP_OK || e1 == ESP_ERR_NVS_NOT_FOUND) &&
                (e2 == ESP_OK || e2 == ESP_ERR_NVS_NOT_FOUND) &&
                (e3 == ESP_OK || e3 == ESP_ERR_NVS_NOT_FOUND);
  if (!readOk) return r;

  r.flags.flight_in_progress = flight != 0;
  r.flags.post_fire = fire != 0;
  r.fired_mask = fire & 0x03;
  s_flight_written = r.flags.flight_in_progress;
  s_fire_written = r.flags.post_fire;
  s_fire_mask = r.fired_mask;

  // Boot counter: one write per boot (reference IGNIS_FC_heavy_13.ino:110-116).
  r.boot_count = (uint16_t)(boots + 1);
  if (nvs_set_u16(s_nvs, "boots", r.boot_count) == ESP_OK) nvs_commit(s_nvs);
  r.ok = true;
  return r;
}

bool nvsClearFlags() {
  s_flight_written = false;
  s_fire_written = false;
  s_fire_mask = 0;
  rtcMirrorClear();
  if (!s_open) return false;
  bool ok = nvs_set_u8(s_nvs, "flight", 0) == ESP_OK &&
            nvs_set_u8(s_nvs, "postfire", 0) == ESP_OK;
  return ok && nvs_commit(s_nvs) == ESP_OK;
}

static bool writeFlag(const char* key) {
  if (!s_open) return false;
  return nvs_set_u8(s_nvs, key, 1) == ESP_OK && nvs_commit(s_nvs) == ESP_OK;
}

bool nvsSetFlightInProgress() {
  PersistFlags f; bool v; rtcMirrorRead(f, v);
  f.flight_in_progress = true;
  rtcMirrorSet(f);                    // instant, no flash
  if (s_flight_written) return true;  // transition-only write
  s_flight_written = true;
  return writeFlag("flight");
}

bool nvsSetPostFire(uint8_t channel_mask) {
  PersistFlags f; bool v; rtcMirrorRead(f, v);
  f.post_fire = true;
  f.flight_in_progress = true;
  rtcMirrorSet(f);
  uint8_t next = s_fire_mask | channel_mask;
  if (s_fire_written && next == s_fire_mask) return true;  // transition-only write
  s_fire_written = true;
  s_fire_mask = next;
  if (!s_open) return false;
  return nvs_set_u8(s_nvs, "postfire", next) == ESP_OK && nvs_commit(s_nvs) == ESP_OK;
}

}  // namespace ignis
