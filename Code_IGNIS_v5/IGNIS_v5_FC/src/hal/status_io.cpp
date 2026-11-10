#include "status_io.h"
#include <Arduino.h>
#include "../config/config_v5.h"
#include "../config/pins_v5.h"

namespace ignis {

static bool s_rmt_ok = false;
static uint32_t s_last_led_ms = 0;

struct Rgb { uint8_t r, g, b; };

static void ws2812Show(const Rgb* px, int n) {
  if (!s_rmt_ok) return;
  // 10 MHz RMT tick (100 ns): T0H 0.4 us / T0L 0.8 us, T1H 0.8 us / T1L 0.4 us.
  rmt_data_t sym[NUM_STATUS_LEDS * 24];
  int k = 0;
  for (int i = 0; i < n; i++) {
    uint8_t grb[3] = {px[i].g, px[i].r, px[i].b};
    for (int c = 0; c < 3; c++) {
      for (int bit = 7; bit >= 0; bit--) {
        bool one = (grb[c] >> bit) & 1;
        sym[k].level0 = 1; sym[k].duration0 = one ? 8 : 4;
        sym[k].level1 = 0; sym[k].duration1 = one ? 4 : 8;
        k++;
      }
    }
  }
  rmtWrite(PIN_WS2812_DATA, sym, k, 5);
}

static void tone(uint16_t hz) {
  if (hz == 0) ledcWrite(PIN_BUZZER, 0);
  else ledcWriteTone(PIN_BUZZER, hz);
}

void statusBegin() {
  s_rmt_ok = rmtInit(PIN_WS2812_DATA, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
  ledcAttach(PIN_BUZZER, 2000, 8);
  ledcWrite(PIN_BUZZER, 0);
}

// Pattern: list of (on_ms, hz) pairs then a gap. Evaluated from time, so no state.
static uint16_t patternTone(uint32_t now, uint32_t period, const uint16_t* seg, int nseg) {
  uint32_t t = now % period;
  for (int i = 0; i < nseg; i++) {
    uint16_t dur = seg[2 * i], hz = seg[2 * i + 1];
    if (t < dur) return hz;
    t -= dur;
  }
  return 0;
}

void statusUpdate(const FlightSnapshot& s, uint32_t now) {
  // ---- LEDs at 10 Hz ----
  if (now - s_last_led_ms >= 100) {
    s_last_led_ms = now;
    bool blink = (now / 250) % 2;
    Rgb px[NUM_STATUS_LEDS] = {};
    int sens = (s.imu_valid ? 1 : 0) + (s.baro_valid ? 1 : 0);
    px[0] = sens == 2 ? Rgb{0, 40, 0} : sens == 1 ? Rgb{40, 30, 0} : Rgb{40, 0, 0};
    int comms = (s.lora_ok ? 1 : 0) + (s.sd == SdStatus::LOGGING ? 1 : 0);
    px[1] = comms == 2 ? Rgb{0, 40, 0} : comms == 1 ? Rgb{40, 30, 0} : Rgb{40, 0, 0};
    switch (s.state) {
      case FlightState::SAFE:     px[2] = {0, 40, 0}; break;
      case FlightState::ARMED:    px[2] = blink ? Rgb{60, 0, 0} : Rgb{10, 0, 0}; break;
      case FlightState::LANDED:   px[2] = {30, 30, 30}; break;
      case FlightState::RECOVERY: px[2] = blink ? Rgb{40, 0, 40} : Rgb{0, 0, 0}; break;
      default:                    px[2] = {0, 0, 50}; break;
    }
    ws2812Show(px, NUM_STATUS_LEDS);
  }

  // ---- Buzzer ----
  uint16_t hz = 0;
  bool nogo = !s.cont.valid || (s.battery_v > 1.0f && s.battery_v < VBAT_NOGO_V);
  if (s.state == FlightState::RECOVERY || s.state == FlightState::LANDED) {
    static const uint16_t p[] = {600, 2700};
    hz = patternTone(now, 4000, p, 1);
  } else if (isPadState(s.state) && nogo) {
    static const uint16_t p[] = {150, 880, 150, 660, 300, 440};
    hz = patternTone(now, 2000, p, 3);
  } else if (s.state == FlightState::ARMED) {
    static const uint16_t p[] = {80, 3200, 80, 0, 80, 3200};
    hz = patternTone(now, 1000, p, 3);
  } else if (s.state == FlightState::SAFE) {
    static const uint16_t p[] = {60, 2400};
    hz = patternTone(now, 3000, p, 1);
  }
  static uint16_t s_last_hz = 0xFFFF;
  if (hz != s_last_hz) { tone(hz); s_last_hz = hz; }
}

}  // namespace ignis
