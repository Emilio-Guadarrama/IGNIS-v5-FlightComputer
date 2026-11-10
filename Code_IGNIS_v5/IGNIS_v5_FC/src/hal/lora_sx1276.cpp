#include "lora_sx1276.h"
#include <Arduino.h>
#include <SPI.h>
#include <atomic>
#include "app_state.h"
#include "spi_bus.h"
#include "../config/config_v5.h"
#include "../config/pins_v5.h"
#include "../core/telemetry_packet.h"

// SX1276 LoRa-mode registers (Semtech SX1276 datasheet, section 6.4).
// TODO(v5-verify): values checked from memory of the SX1276 register map,
// cross-check against RFM95W-915MHz-Transceptor-Lora-Datasheet.pdf in the repo.
namespace {
constexpr uint8_t REG_FIFO = 0x00, REG_OP_MODE = 0x01, REG_FRF_MSB = 0x06,
                  REG_PA_CONFIG = 0x09, REG_FIFO_ADDR_PTR = 0x0D,
                  REG_FIFO_TX_BASE = 0x0E, REG_IRQ_FLAGS = 0x12,
                  REG_MODEM_CONFIG1 = 0x1D, REG_MODEM_CONFIG2 = 0x1E,
                  REG_PREAMBLE_MSB = 0x20, REG_PREAMBLE_LSB = 0x21,
                  REG_PAYLOAD_LEN = 0x22, REG_MODEM_CONFIG3 = 0x26,
                  REG_SYNC_WORD = 0x39, REG_VERSION = 0x42;
constexpr uint8_t MODE_LORA = 0x80, MODE_SLEEP = 0x00, MODE_STDBY = 0x01, MODE_TX = 0x03;
constexpr uint8_t IRQ_TX_DONE = 0x08;

std::atomic<bool> s_ok{false};
std::atomic<bool> s_last_ok{false};
std::atomic<uint32_t> s_tx{0}, s_skipped{0}, s_hb{0};
uint32_t s_seq = 0;
const SPISettings kSpi(LORA_SPI_HZ, MSBFIRST, SPI_MODE0);

// Caller holds the SPI mutex.
uint8_t rd(uint8_t reg) {
  SPI.beginTransaction(kSpi);
  digitalWrite(PIN_LORA_CS, LOW);
  SPI.transfer(reg & 0x7F);
  uint8_t v = SPI.transfer(0);
  digitalWrite(PIN_LORA_CS, HIGH);
  SPI.endTransaction();
  return v;
}
void wr(uint8_t reg, uint8_t v) {
  SPI.beginTransaction(kSpi);
  digitalWrite(PIN_LORA_CS, LOW);
  SPI.transfer(reg | 0x80);
  SPI.transfer(v);
  digitalWrite(PIN_LORA_CS, HIGH);
  SPI.endTransaction();
}
void wrBurst(uint8_t reg, const uint8_t* b, size_t n) {
  SPI.beginTransaction(kSpi);
  digitalWrite(PIN_LORA_CS, LOW);
  SPI.transfer(reg | 0x80);
  for (size_t i = 0; i < n; i++) SPI.transfer(b[i]);
  digitalWrite(PIN_LORA_CS, HIGH);
  SPI.endTransaction();
}

bool radioInit() {
  // Reset pulse (RFM95W NRESET, active low).
  pinMode(PIN_LORA_RST, OUTPUT);
  digitalWrite(PIN_LORA_RST, LOW);
  delay(2);
  digitalWrite(PIN_LORA_RST, HIGH);
  delay(10);
  ignis::SpiGuard g(500);
  if (!g.ok) return false;
  uint8_t ver = rd(REG_VERSION);
  if (ver != 0x12) {
    Serial.printf("[LORA] RegVersion=0x%02X (expected 0x12)\n", ver);
    return false;
  }
  wr(REG_OP_MODE, MODE_LORA | MODE_SLEEP);   // LoRa bit only settable in sleep
  delay(10);
  wr(REG_OP_MODE, MODE_LORA | MODE_STDBY);
  uint64_t frf = ((uint64_t)LORA_FREQ_HZ << 19) / 32000000ULL;
  wr(REG_FRF_MSB, (uint8_t)(frf >> 16));
  wr(REG_FRF_MSB + 1, (uint8_t)(frf >> 8));
  wr(REG_FRF_MSB + 2, (uint8_t)frf);
  // BW125 (0111) | CR4/5 (001) | explicit header
  wr(REG_MODEM_CONFIG1, (0x07 << 4) | ((LORA_CR_DENOM - 4) << 1));
  // SF | CRC on
  wr(REG_MODEM_CONFIG2, (LORA_SF << 4) | 0x04);
  wr(REG_MODEM_CONFIG3, 0x04);               // AGC auto; LDRO off (symbol 4.1 ms < 16 ms)
  wr(REG_PREAMBLE_MSB, (LORA_PREAMBLE_LEN >> 8) & 0xFF);
  wr(REG_PREAMBLE_LSB, LORA_PREAMBLE_LEN & 0xFF);
  wr(REG_SYNC_WORD, LORA_SYNC_WORD);
  // PA_BOOST, Pout = 2 + OutputPower dBm. ASSUMPTION(v5): verify 17 dBm on the bench.
  wr(REG_PA_CONFIG, 0x80 | 0x70 | (uint8_t)(LORA_TX_POWER_DBM - 2));
  wr(REG_FIFO_TX_BASE, 0x00);
  bool ok = rd(REG_MODEM_CONFIG2) == ((LORA_SF << 4) | 0x04) && rd(REG_SYNC_WORD) == LORA_SYNC_WORD;
  Serial.printf("[LORA] init %s (915 MHz SF%d BW125 CR4/%d)\n", ok ? "OK" : "FAILED readback", LORA_SF, LORA_CR_DENOM);
  return ok;
}

// Loads the FIFO and starts TX. Returns false if the bus was busy.
bool startTx(const uint8_t* frame, size_t n) {
  ignis::SpiGuard g(SPI_MUTEX_TIMEOUT_LORA_MS);
  if (!g.ok) return false;
  wr(REG_OP_MODE, MODE_LORA | MODE_STDBY);
  wr(REG_FIFO_ADDR_PTR, 0x00);
  wrBurst(REG_FIFO, frame, n);
  wr(REG_PAYLOAD_LEN, (uint8_t)n);
  wr(REG_IRQ_FLAGS, 0xFF);
  wr(REG_OP_MODE, MODE_LORA | MODE_TX);
  return true;
}

// -1 = bus busy, 0 = not done, 1 = done
int pollTxDone() {
  ignis::SpiGuard g(SPI_MUTEX_TIMEOUT_LORA_MS);
  if (!g.ok) return -1;
  if (rd(REG_IRQ_FLAGS) & IRQ_TX_DONE) { wr(REG_IRQ_FLAGS, 0xFF); return 1; }
  return 0;
}

void abortTx() {
  ignis::SpiGuard g(200);
  if (g.ok) { wr(REG_OP_MODE, MODE_LORA | MODE_STDBY); wr(REG_IRQ_FLAGS, 0xFF); }
}

void loraTask(void*) {
  uint32_t last_attempt = 0, last_init = millis();
  for (;;) {
    s_hb++;
    vTaskDelay(pdMS_TO_TICKS(10));
    ignis::FlightSnapshot s;
    ignis::snapshotRead(s);
    if (!s_ok) {
      // Re-init only on the pad (a reset pulse in flight would only add risk).
      if (ignis::isPadState(s.state) && millis() - last_init > 10000) {
        last_init = millis();
        s_ok = radioInit();
      }
      continue;
    }
    uint32_t interval = ignis::isPadState(s.state) ? TELEMETRY_INTERVAL_MS_PAD : TELEMETRY_INTERVAL_MS_FLT;
    uint32_t now = millis();
    if (now - last_attempt < interval) continue;
    last_attempt = now;

    ignis::TelemetryPacket pkt;
    ignis::encodeTelemetry(s, s_seq, pkt);
    uint8_t frame[ignis::kTelemetryFrameLen];
    size_t n = ignis::buildRadioHeadFrame(pkt, frame, sizeof(frame));
    if (!startTx(frame, n)) { s_skipped++; continue; }
    s_seq++;
    s_tx++;
    // Poll for TxDone (~575 ms ToA at SF9/BW125 for 102 bytes).
    uint32_t t0 = millis();
    int done = 0;
    while (millis() - t0 < LORA_TX_TIMEOUT_MS) {
      vTaskDelay(pdMS_TO_TICKS(10));
      s_hb++;
      done = pollTxDone();
      if (done == 1) break;
    }
    if (done != 1) abortTx();
    s_last_ok = (done == 1);
  }
}
}  // namespace

namespace ignis {

bool loraStart() {
  s_ok = radioInit();
  xTaskCreatePinnedToCore(loraTask, "lora", 4096, nullptr, TASK_PRIO_LORA, nullptr, TASK_CORE_LORA);
  return s_ok;
}
bool loraOk() { return s_ok.load(); }
bool loraLastTxOk() { return s_last_ok.load(); }
uint32_t loraTxCount() { return s_tx.load(); }
uint32_t loraSkipped() { return s_skipped.load(); }
uint32_t loraHeartbeat() { return s_hb.load(); }

}  // namespace ignis
