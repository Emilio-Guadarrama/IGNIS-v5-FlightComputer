#include "ota_update.h"
#if IGNIS_ENABLE_OTA
#include <Arduino.h>
#include <WebServer.h>
#include "esp_ota_ops.h"
#include "mbedtls/sha256.h"
#include "secrets_check.h"
#include "../core/ota_gate.h"
#include "../hal/app_state.h"

namespace ignis {

static uint32_t gateNow() {
  FlightSnapshot s;
  snapshotRead(s);
  OtaGateInput g;
  g.state = s.state;
  g.cont = s.cont;
  if (millis() - s.timestamp_ms > 200) g.cont.valid = false;  // stale = unknown = armed
  g.flight_latched = g_flight_latched.load();
  g.post_fire = g_post_fire.load() || s.post_fire_latched;
  g.recovery_boot = g_recovery_boot.load();
  g.ota_built = true;
  return otaDenyReasons(g);
}

uint32_t otaCurrentDenyMask() { return gateNow(); }

struct OtaCtx {
  bool active = false;
  bool ok = false;
  bool need_auth = false;
  const char* err = nullptr;
  uint32_t deny = 0;
  esp_ota_handle_t h = 0;
  const esp_partition_t* part = nullptr;
  mbedtls_sha256_context sha;
  uint8_t expected[32];
  size_t bytes = 0;
};
static OtaCtx s_ota;

static bool parseHex32(const String& hex, uint8_t out[32]) {
  if (hex.length() != 64) return false;
  for (int i = 0; i < 32; i++) {
    char b[3] = {hex[2 * i], hex[2 * i + 1], 0};
    char* end;
    long v = strtol(b, &end, 16);
    if (*end) return false;
    out[i] = (uint8_t)v;
  }
  return true;
}

static void abortOta(const char* why) {
  if (s_ota.active) {
    esp_ota_abort(s_ota.h);
    mbedtls_sha256_free(&s_ota.sha);
  }
  s_ota.active = false;
  s_ota.ok = false;
  if (!s_ota.err) s_ota.err = why;
  Serial.printf("[OTA] aborted: %s (deny=0x%02lx, %u bytes)\n", why, (unsigned long)s_ota.deny,
                (unsigned)s_ota.bytes);
}

// Called from the WiFi task loop: aborts an upload that has stalled between
// chunks if the gate closes (continuity appears, state leaves SAFE, flight).
void otaPoll() {
  if (!s_ota.active) return;
  uint32_t d = gateNow();
  if (d) { s_ota.deny = d; abortOta("gate closed while upload idle"); }
}

void otaRegister(WebServer& server) {
  static const char* hdrs[] = {"X-Image-SHA256"};
  server.collectHeaders(hdrs, 1);

  server.on("/ota", HTTP_POST,
    // Final handler (after upload completes)
    [&server]() {
      if (s_ota.need_auth) { server.requestAuthentication(DIGEST_AUTH, "IGNIS-OTA"); return; }
      if (!s_ota.ok) {
        char msg[96];
        snprintf(msg, sizeof(msg), "OTA refused: %s (deny=0x%02lx)\n", s_ota.err ? s_ota.err : "unknown",
                 (unsigned long)s_ota.deny);
        server.send(403, "text/plain", msg);
        return;
      }
      server.send(200, "text/plain", "OTA OK: rebooting into new image (self-test decides validity)\n");
      delay(300);
      esp_restart();
    },
    // Upload handler (streams chunks)
    [&server]() {
      HTTPUpload& up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        s_ota = OtaCtx();
        if (!server.authenticate("ota", IGNIS_OTA_PASSWORD)) { s_ota.need_auth = true; s_ota.err = "auth"; return; }
        s_ota.deny = gateNow();
        if (s_ota.deny) { s_ota.err = "gate"; Serial.printf("[OTA] refused at start, deny=0x%02lx\n", (unsigned long)s_ota.deny); return; }
        if (!parseHex32(server.header("X-Image-SHA256"), s_ota.expected)) { s_ota.err = "missing/invalid X-Image-SHA256"; return; }
        s_ota.part = esp_ota_get_next_update_partition(nullptr);
        if (!s_ota.part || esp_ota_begin(s_ota.part, OTA_WITH_SEQUENTIAL_WRITES, &s_ota.h) != ESP_OK) {
          s_ota.err = "esp_ota_begin"; return;
        }
        mbedtls_sha256_init(&s_ota.sha);
        mbedtls_sha256_starts(&s_ota.sha, 0);
        s_ota.active = true;
        Serial.printf("[OTA] started -> %s\n", s_ota.part->label);
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (!s_ota.active) return;
        s_ota.deny = gateNow();
        if (s_ota.deny) { abortOta("gate closed mid-upload (continuity/state/flight)"); return; }
        if (esp_ota_write(s_ota.h, up.buf, up.currentSize) != ESP_OK) { abortOta("esp_ota_write"); return; }
        mbedtls_sha256_update(&s_ota.sha, up.buf, up.currentSize);
        s_ota.bytes += up.currentSize;
      } else if (up.status == UPLOAD_FILE_END) {
        if (!s_ota.active) return;
        uint8_t got[32];
        mbedtls_sha256_finish(&s_ota.sha, got);
        mbedtls_sha256_free(&s_ota.sha);
        if (memcmp(got, s_ota.expected, 32) != 0) {
          esp_ota_abort(s_ota.h); s_ota.active = false; s_ota.err = "SHA-256 mismatch"; return;
        }
        s_ota.active = false;
        if (esp_ota_end(s_ota.h) != ESP_OK) { s_ota.err = "image verification failed"; return; }
        s_ota.deny = gateNow();
        if (s_ota.deny) { s_ota.err = "gate closed before boot switch"; return; }
        if (esp_ota_set_boot_partition(s_ota.part) != ESP_OK) { s_ota.err = "set_boot_partition"; return; }
        s_ota.ok = true;
        Serial.printf("[OTA] image OK (%u bytes), boot partition -> %s\n", (unsigned)s_ota.bytes, s_ota.part->label);
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        abortOta("client aborted");
      }
    });
}

}  // namespace ignis
#endif  // IGNIS_ENABLE_OTA
