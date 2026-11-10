#include "spi_bus.h"
#include <SPI.h>
#include "freertos/semphr.h"
#include "../config/pins_v5.h"

namespace ignis {

static SemaphoreHandle_t s_mutex = nullptr;

void spiBusBegin() {
  // Both chip-selects idle HIGH before the bus starts clocking.
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_LORA_CS, OUTPUT);
  digitalWrite(PIN_LORA_CS, HIGH);
  // Must run unconditionally, even with no SD card (reference DataLogger.h:143-149).
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);
  if (!s_mutex) s_mutex = xSemaphoreCreateMutex();  // priority inheritance
}

bool spiLock(uint32_t timeout_ms) {
  if (!s_mutex) return false;
  return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void spiUnlock() {
  if (s_mutex) xSemaphoreGive(s_mutex);
}

}  // namespace ignis
