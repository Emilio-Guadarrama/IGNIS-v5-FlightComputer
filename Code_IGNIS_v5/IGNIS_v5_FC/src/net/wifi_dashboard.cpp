#include "wifi_dashboard.h"
#if IGNIS_ENABLE_WIFI
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <atomic>
#include "esp_wifi.h"
#include "ota_update.h"
#include "secrets_check.h"
#include "../config/pins_v5.h"
#include "../config/version.h"
#include "../core/boot_policy.h"
#include "../core/ota_gate.h"
#include "../hal/app_state.h"
#include "../hal/sd_logger.h"

namespace ignis {

static std::atomic<uint8_t> s_status{(uint8_t)WifiStatus::OFF};
static std::atomic<uint32_t> s_latched_at{0};
static WebServer* s_server = nullptr;
static FlightSnapshot s_cache;
static uint32_t s_cache_ms = 0;
static char s_json[1800];

WifiStatus wifiStatus() { return (WifiStatus)s_status.load(); }

static const char kPage[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>IGNIS v5</title>
<style>body{font:14px monospace;margin:12px;background:#111;color:#ddd}h1{font-size:18px}
td{padding:2px 8px}.bad{color:#f55}.ok{color:#5f5}.warn{color:#fc3}</style></head><body>
<h1>IGNIS v5 &middot; read-only dashboard</h1><p>No commands exist on this page. Power cut (RBF) is the only way to disable the board.</p>
<table id="t"></table><p id="logs"></p>
<script>
const P=%PERIOD%;
function row(k,v,c){return '<tr><td>'+k+'</td><td class="'+(c||'')+'">'+v+'</td></tr>';}
async function tick(){try{const r=await fetch('/api/state');const s=await r.json();let h='';
h+=row('state',s.state,s.state=='RECOVERY'?'bad':'');h+=row('t_ms',s.t);
h+=row('reset_reason',s.reset_reason+' '+s.reset_name);h+=row('boot_count',s.boot_count);
h+=row('fw',s.fw.version+' git '+s.fw.git);h+=row('app sha256',s.fw.sha256);
h+=row('build','ground_test='+s.build.ground_test+' single_event='+s.build.single_event,s.build.ground_test?'bad':'');
h+=row('battery V',s.vbat.toFixed(2),s.vbat<7.8?'bad':(s.vbat<8.0?'warn':'ok'));
h+=row('continuity','CH1 '+(s.cont.ch1?'YES':'no')+' / CH2 '+(s.cont.ch2?'YES':'no')+(s.cont.valid?'':' (INVALID)'),s.cont.valid?'':'bad');
h+=row('fired','CH1 '+s.fired[0]+' / CH2 '+s.fired[1]);
h+=row('baro',s.baro.p.toFixed(1)+' Pa  alt '+s.baro.alt.toFixed(1)+' m  AGL '+s.agl.toFixed(1)+' m  '+s.baro.t.toFixed(1)+' C',s.baro.ok?'':'bad');
h+=row('imu','a '+s.imu.a.map(x=>x.toFixed(2)).join(' ')+' |'+s.imu.at.toFixed(2)+'| g  w '+s.imu.g.map(x=>x.toFixed(1)).join(' ')+' dps',s.imu.ok?'':'bad');
h+=row('gps',(s.gps.fix?'FIX':'no fix')+' sats '+s.gps.sats+' '+s.gps.lat.toFixed(6)+','+s.gps.lon.toFixed(6)+' alt '+s.gps.alt.toFixed(1),s.gps.fix?'ok':'warn');
h+=row('lora',(s.lora.ok?'OK':'DOWN')+' tx '+s.lora.tx+' last '+(s.lora.last?'ok':'fail')+' skipped '+s.lora.skip,s.lora.ok?'ok':'bad');
h+=row('sd',s.sd.status+' '+s.sd.file+' dropped '+s.sd.dropped,s.sd.status=='LOGGING'?'ok':'bad');
h+=row('ota gate',s.ota_deny==0?'open (SAFE, no continuity)':'closed 0x'+s.ota_deny.toString(16));
document.getElementById('t').innerHTML=h;}catch(e){}}
setInterval(tick,P);tick();
async function logs(){const r=await fetch('/api/logs');if(!r.ok){document.getElementById('logs').textContent='SD download only in SAFE';return;}
const l=await r.json();document.getElementById('logs').innerHTML='Logs: '+l.map(f=>'<a href="/api/log?f='+encodeURIComponent(f.n)+'">'+f.n+'</a> ('+f.s+' B)').join(' ');}
logs();
</script></body></html>)HTML";

static const char* sdName(SdStatus s) {
  switch (s) {
    case SdStatus::LOGGING: return "LOGGING";
    case SdStatus::ERROR: return "ERROR";
    case SdStatus::CLOSED: return "CLOSED";
    default: return "NO_CARD";
  }
}

static uint32_t denyMask() {
#if IGNIS_ENABLE_OTA
  return otaCurrentDenyMask();
#else
  return OTA_DENY_BUILD;
#endif
}

static void handleState() {
  const FlightSnapshot& s = s_cache;
  snprintf(s_json, sizeof(s_json),
    "{\"state\":\"%s\",\"t\":%lu,\"agl\":%.2f,\"max_agl\":%.2f,"
    "\"baro\":{\"ok\":%d,\"p\":%.1f,\"alt\":%.2f,\"t\":%.2f},"
    "\"imu\":{\"ok\":%d,\"a\":[%.3f,%.3f,%.3f],\"at\":%.3f,\"g\":[%.2f,%.2f,%.2f],\"t\":%.1f},"
    "\"vbat\":%.3f,\"cont\":{\"ch1\":%d,\"ch2\":%d,\"valid\":%d},\"fired\":[%d,%d],"
    "\"gps\":{\"fix\":%d,\"sats\":%u,\"lat\":%.7f,\"lon\":%.7f,\"alt\":%.1f},"
    "\"lora\":{\"ok\":%d,\"tx\":%lu,\"last\":%d,\"skip\":%lu},"
    "\"sd\":{\"status\":\"%s\",\"file\":\"%s\",\"dropped\":%lu},"
    "\"reset_reason\":%u,\"reset_name\":\"%s\",\"boot_count\":%u,\"recovery\":%d,"
    "\"fw\":{\"name\":\"%s\",\"version\":\"%s\",\"git\":\"%s\",\"sha256\":\"%s\"},"
    "\"build\":{\"ground_test\":%d,\"single_event\":%d},\"ota_deny\":%lu}",
    flightStateName(s.state), (unsigned long)s.timestamp_ms, s.agl_m, s.max_agl_m,
    s.baro_valid, s.pressure_pa, s.baro_alt_m, s.baro_temp_c,
    s.imu_valid, s.accel_x_g, s.accel_y_g, s.accel_z_g, s.accel_total_g,
    s.gyro_x_dps, s.gyro_y_dps, s.gyro_z_dps, s.imu_temp_c,
    s.battery_v, s.cont.ch1, s.cont.ch2, s.cont.valid, s.pyro1_fired, s.pyro2_fired,
    s.gps_fix, (unsigned)s.gps_sats, s.gps_lat, s.gps_lon, s.gps_alt_m,
    s.lora_ok, (unsigned long)s.lora_tx_count, s.lora_last_tx_ok, (unsigned long)s.lora_skipped,
    sdName(s.sd), sdCurrentFile(), (unsigned long)s.sd_rows_dropped,
    (unsigned)s.reset_reason, resetReasonName(s.reset_reason), (unsigned)s.boot_count, s.recovery_boot,
    IGNIS_FW_NAME, IGNIS_FW_VERSION, IGNIS_GIT_HASH, g_boot.app_sha256,
    (int)GROUND_TEST_MODE, (int)SINGLE_EVENT_MODE, (unsigned long)denyMask());
  s_server->send(200, "application/json", s_json);
}

static bool safeOnly() {
  FlightSnapshot s;
  snapshotRead(s);
  if (s.state != FlightState::SAFE || g_flight_latched || g_recovery_boot) {
    s_server->send(403, "text/plain", "SD access only in SAFE state\n");
    return false;
  }
  return true;
}

struct ListCtx { String* out; bool first; };
static void listCb(const char* name, uint32_t size, void* p) {
  ListCtx* c = (ListCtx*)p;
  char item[80];
  snprintf(item, sizeof(item), "%s{\"n\":\"/%s\",\"s\":%lu}", c->first ? "" : ",", name[0] == '/' ? name + 1 : name,
           (unsigned long)size);
  *c->out += item;
  c->first = false;
}

static void handleLogs() {
  if (!safeOnly()) return;
  String out = "[";
  ListCtx c{&out, true};
  if (!sdListLogs(listCb, &c)) { s_server->send(503, "text/plain", "SD not available\n"); return; }
  out += "]";
  s_server->send(200, "application/json", out);
}

static void handleLogDownload() {
  if (!safeOnly()) return;
  String f = s_server->arg("f");
  static uint8_t buf[1024];
  uint32_t total = 0;
  int32_t n = sdReadChunk(f.c_str(), 0, buf, sizeof(buf), &total);
  if (n < 0) { s_server->send(404, "text/plain", "not found\n"); return; }
  s_server->setContentLength(total);
  s_server->send(200, "text/csv", "");
  uint32_t off = 0;
  while (n > 0) {
    s_server->sendContent((const char*)buf, (size_t)n);
    off += n;
    if (off >= total) break;
    FlightSnapshot s;
    snapshotRead(s);
    if (s.state != FlightState::SAFE || g_flight_latched) break;  // stop if no longer SAFE
    n = sdReadChunk(f.c_str(), off, buf, sizeof(buf), nullptr);  // one SPI lock per 1 KB
    vTaskDelay(1);
  }
}

static void startAp() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "%s%02X%02X", WIFI_AP_SSID_PREFIX, mac[4], mac[5]);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(ssid, IGNIS_WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CLIENTS)) {
    Serial.println("[WIFI] softAP start failed");
    WiFi.mode(WIFI_OFF);
    return;
  }
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
  esp_wifi_set_max_tx_power(WIFI_MAX_TX_POWER_QDBM);
  if (!s_server) {
    s_server = new WebServer(80);
    s_server->on("/", HTTP_GET, []() {
      String page = FPSTR(kPage);
      page.replace("%PERIOD%", String(1000 / WIFI_SNAPSHOT_HZ));
      s_server->send(200, "text/html", page);
    });
    s_server->on("/api/state", HTTP_GET, handleState);
    s_server->on("/api/logs", HTTP_GET, handleLogs);
    s_server->on("/api/log", HTTP_GET, handleLogDownload);
#if IGNIS_ENABLE_OTA
    otaRegister(*s_server);
#endif
    s_server->onNotFound([]() { s_server->send(404, "text/plain", "not found\n"); });
  }
  s_server->begin();
  s_status = (uint8_t)WifiStatus::AP_ON;
  Serial.printf("[WIFI] AP %s up (HT20, max %d client, tx %d qdBm)\n", ssid, WIFI_AP_MAX_CLIENTS,
                WIFI_MAX_TX_POWER_QDBM);
}

static void stopAp(WifiStatus next) {
  if (s_server) s_server->stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  s_status = (uint8_t)next;
  Serial.printf("[WIFI] off (%s)\n", next == WifiStatus::LATCHED_OFF ? "latched until power cycle" : "idle");
}

static void wifiTask(void*) {
  uint32_t held_since = 0;
  bool holding = false;
  uint32_t last_client_ms = millis();
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  for (;;) {
    uint32_t now = millis();
    WifiStatus st = wifiStatus();
    if (g_flight_latched || g_recovery_boot) {
      if (st == WifiStatus::AP_ON) stopAp(WifiStatus::LATCHED_OFF);
      s_status = (uint8_t)WifiStatus::LATCHED_OFF;
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;  // latched: never restarts this boot
    }
    if (st != WifiStatus::AP_ON) {
      FlightSnapshot s;
      snapshotRead(s);
      bool pressed = digitalRead(PIN_BOOT_BUTTON) == BOOT_BUTTON_ACTIVE_LEVEL;
      if (pressed && isPadState(s.state)) {
        if (!holding) { holding = true; held_since = now; }
        if (now - held_since >= WIFI_BOOT_HOLD_MS) {
          holding = false;
          startAp();
          last_client_ms = now;
        }
      } else {
        holding = false;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    // AP on
    if (now - s_cache_ms >= 1000 / WIFI_SNAPSHOT_HZ) { snapshotRead(s_cache); s_cache_ms = now; }
    s_server->handleClient();
#if IGNIS_ENABLE_OTA
    otaPoll();
#endif
    if (WiFi.softAPgetStationNum() > 0) last_client_ms = now;
    if (now - last_client_ms >= WIFI_IDLE_OFF_MS) stopAp(WifiStatus::OFF);
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void wifiDashboardStart() {
  s_status = (uint8_t)WifiStatus::OFF;
  xTaskCreatePinnedToCore(wifiTask, "wifi", 8192, nullptr, TASK_PRIO_WIFI, nullptr, TASK_CORE_WIFI);
}

// Backstop from loop(): if flight is latched and the WiFi task has not
// confirmed shutdown within 1 s (e.g. task hung inside the HTTP server),
// stop the radio directly. Never runs while the latch is clear.
void wifiBackstop(uint32_t now) {
  if (!g_flight_latched && !g_recovery_boot) { s_latched_at = 0; return; }
  if (s_latched_at == 0) { s_latched_at = now ? now : 1; return; }
  if (wifiStatus() == WifiStatus::AP_ON && now - s_latched_at > 1000) {
    esp_wifi_stop();
    s_status = (uint8_t)WifiStatus::LATCHED_OFF;
    Serial.println("[WIFI] backstop: esp_wifi_stop() (task did not respond)");
  }
}

}  // namespace ignis
#endif  // IGNIS_ENABLE_WIFI
