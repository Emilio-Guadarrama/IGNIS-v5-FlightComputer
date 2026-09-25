#!/usr/bin/env bash
# IGNIS FC v11 — library install + version capture
# Run once per machine, then paste the printed versions into the mission report.
set -euo pipefail

CORE_VER="3.3.10"

echo "==> Updating index"
arduino-cli core update-index

echo "==> Installing esp32:esp32@${CORE_VER}"
arduino-cli core install "esp32:esp32@${CORE_VER}"

echo "==> Installing libraries"
arduino-cli lib install "RadioHead"
arduino-cli lib install "TinyGPSPlus@1.1.0"
arduino-cli lib install "Adafruit NeoPixel@1.12.3"

echo
echo "==================== RESOLVED VERSIONS ===================="
arduino-cli core list   | grep -i esp32 || true
arduino-cli lib list    | grep -Ei "radiohead|tinygps|neopixel" || true
echo "==========================================================="
echo
echo "NOTE: RadioHead's installed build has no setSyncWord() on RH_RF95."
echo "      Telemetry.h writes RegSyncWord (0x39) directly. Do not 'fix' that."
