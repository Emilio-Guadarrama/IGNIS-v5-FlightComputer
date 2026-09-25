/*
  ================================================================
  IGNIS v2 Flight Computer — DataLogger.h
  SD card CSV logger. Same design as the standalone version you
  already have, adapted to log the shared FlightData struct directly
  so nothing has to be duplicated/kept in sync between modules.

  Pin map (CONFIRMED from schematic):
    SD_MOSI -> IO11, SD_MISO -> IO13, SD_SCK -> IO12,
    SD_CS -> IO42, SD_DETECT -> IO41 (assumed active-LOW = card present)
  ================================================================
*/

#pragma once
#include <SPI.h>
#include <SD.h>
#include <string.h>   // strncpy — v13, used when reopening a resumed log filename
#include "Config.h"
#include "FlightData.h"

#define SD_DETECT_ACTIVE_LOW true
// Dropped from 25MHz to 4MHz: if a trace on this bus is electrically
// marginal (nicked, partially bridged — a real possibility given this
// board's documented I2C solder-joint issues, and the SD bus is on the
// same PCB) rather than fully open/shorted, a slower clock is
// meaningfully more tolerant of poor signal integrity and may be the
// difference between a clean failure and a bus hang. This does NOT fix
// a genuine short or open trace — it only helps if the fault is
// marginal/high-impedance. If logging still fails at 4MHz, the problem
// is downstream of what a clock-speed change can address.
#define SD_SPI_FREQ_HZ         4000000
#define CSV_ROW_MAX_LEN         448   // widened for v11 forensic columns, +1 for v13's `resumed` column

// Shared bus note: SD and RFM95W share SNS_SPI_MISO/MOSI/SCK (confirmed on
// schematic, R24-27 pull-ups feed both). The installed RadioHead build's
// RHHardwareSPI has NO constructor that accepts a custom SPIClass instance
// (only Frequency/BitOrder/DataMode) — so instead of a separate sdSPI
// object, we use Arduino's global default `SPI` object for BOTH SD and the
// radio. RH_RF95's default constructor already wraps this same global SPI
// internally, so as long as SPI.begin() is called once with our pins
// before either peripheral is used, both work over the same bus with their
// own CS pins. See Telemetry.h for the RF95 side of this.

static File   logFile;
static char   csvRowBuf[CSV_ROW_MAX_LEN];
static char   currentLogPath[32] = {0};
static bool   sdReady = false;
static bool   sdSessionEnded = false;  // true after closeLog() — stops maintainSDConnection() from re-mounting post-landing
static unsigned long lastLogFlushMs = 0;
static unsigned long lastMountAttemptMs = 0;

const char CSV_HEADER[] =
  "timestamp_ms,state,"
  "pressure_pa,baro_alt_m,agl_m,baro_temp_c,baro_valid,"
  "accel_x_g,accel_y_g,accel_z_g,accel_total_g,accel_total_g_filtered,gyro_x_dps,gyro_y_dps,gyro_z_dps,mpu_temp_c,imu_valid,"
  "gps_lat,gps_lon,gps_alt_m,gps_sats,gps_fix,"
  "battery_v,rssi_dbm,snr_db,"
  "pyro1_cont,pyro2_cont,pyro1_fired,pyro2_fired,"
  "tilt_deg,boot_count,reset_reason,deploy_trigger,resumed";   // resumed added v13 — see FlightData.h's resumed_this_boot

static bool isCardPresent() {
  int level = digitalRead(SD_DETECT_PIN);
  return SD_DETECT_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
}

static bool findNextLogPath(char* outPath, size_t outLen) {
  for (int i = 0; i < 1000; i++) {
    snprintf(outPath, outLen, "/FLIGHT%03d.CSV", i);
    if (!SD.exists(outPath)) return true;
  }
  return false;
}

// Attempts a mount unconditionally — SD_DETECT is logged for visibility
// but does NOT gate the attempt. This protects against SD_DETECT being
// wrong (e.g. a card seated but not registering as present, or a bad
// connection on that specific net) — we just try the real mount and
// let it succeed or fail on its own. It does NOT protect against
// SD.begin() hanging on a genuinely damaged SPI bus; that's a hardware
// fault no software retry can route around. If the board resets near a
// mount attempt, that's the bus, not this logic — go check the trace.
// isResume (v13): when true AND rtcState has a logged filename from
// before the reset, reopen that SAME file instead of starting a new
// /FLIGHTxxx.CSV — this is what turns a pre-reset + post-reset flight
// into one continuous file instead of two (e.g. FLIGHT051+FLIGHT052).
// The Arduino SD library's FILE_WRITE mode opens positioned at EOF
// (append), it does not truncate, so reopening an existing path here
// just continues it.
static bool attemptSDMount(bool isResume) {
  Serial.printf("[SD] SD_DETECT reads: %s (informational only, not gating)\n",
                isCardPresent() ? "CARD PRESENT" : "NO CARD");

  rtcSetLastOp(OP_SD_MOUNT);

  if (!SD.begin(SD_CS_PIN, SPI, SD_SPI_FREQ_HZ)) {
    Serial.println("[SD] SD.begin() failed — continuing without SD logging.");
    rtcSetLastOp(OP_IDLE);
    return false;
  }

  if (SD.cardType() == CARD_NONE) {
    Serial.println("[SD] Card type NONE — continuing without SD logging.");
    rtcSetLastOp(OP_IDLE);
    return false;
  }

  bool reopenExisting = isResume && rtcState.logFileName[0] != '\0' &&
                         SD.exists(rtcState.logFileName);

  if (reopenExisting) {
    strncpy(currentLogPath, rtcState.logFileName, sizeof(currentLogPath) - 1);
    currentLogPath[sizeof(currentLogPath) - 1] = '\0';
  } else {
    if (!findNextLogPath(currentLogPath, sizeof(currentLogPath))) {
      Serial.println("[SD] Could not allocate a log filename.");
      rtcSetLastOp(OP_IDLE);
      return false;
    }
    strncpy(rtcState.logFileName, currentLogPath, sizeof(rtcState.logFileName) - 1);
    rtcState.logFileName[sizeof(rtcState.logFileName) - 1] = '\0';
  }

  logFile = SD.open(currentLogPath, FILE_WRITE);
  if (!logFile) {
    Serial.printf("[SD] Failed to create/reopen %s\n", currentLogPath);
    rtcSetLastOp(OP_IDLE);
    return false;
  }

  if (!reopenExisting) {
    logFile.println(CSV_HEADER);
  }
  logFile.flush();
  lastLogFlushMs = millis();
  Serial.printf("[SD] Logging to %s%s\n", currentLogPath, reopenExisting ? " (resumed — appending)" : "");
  rtcSetLastOp(OP_IDLE);
  return true;
}

static bool initDataLogger(bool isResume = false) {
  pinMode(SD_DETECT_PIN, INPUT); // external pull-up already on PCB (R28)

  // IMPORTANT: this bus is shared with the RFM95W (confirmed on schematic —
  // SNS_SPI_MISO/MOSI/SCK feed both the SD slot and the LoRa module, R24-27
  // pull-ups). SPI.begin() MUST run unconditionally, even if no SD card is
  // present, or Telemetry.h's rf95.init() will silently fail because the
  // SPI peripheral was never attached to these pins. Do NOT move this call
  // after the card-present check again.
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

  sdReady = attemptSDMount(isResume);
  lastMountAttemptMs = millis();
  return sdReady;
}

// Call once per loop() iteration. No-op if already logging, if the
// session has been intentionally ended (post-landing, see closeLog()),
// or if flight has actually started (see below for why).
//
// SAFETY NOTE: SD.begin() with no card physically present does NOT
// fail fast — the SD library's internal command handshake (CMD0/CMD8/
// ACMD41) retries waiting for a response that never comes, and the
// whole sequence blocks for ~1.8-2 SECONDS before giving up. That's
// tolerable on the pad (costs you a stale dashboard for 2s) but NOT
// once flying — a 2s stall during BOOST/COAST could directly delay
// apogee/main-deploy detection. So retries are gated on flight state:
// only attempted at/before STATE_ARMED, never once STATE_BOOST+.
static void maintainSDConnection(FlightState_t currentState) {
  if (sdReady || sdSessionEnded) return;
  if (currentState > STATE_ARMED) return; // already flying — don't risk the blocking stall
  unsigned long now = millis();
  if (now - lastMountAttemptMs < SD_MOUNT_RETRY_INTERVAL_MS) return;
  lastMountAttemptMs = now;
  Serial.println("[SD] Retry mount attempt...");
  // false: a retry within the SAME boot session is never a resume-reopen —
  // the resume path only applies once, in initDataLogger() at boot.
  sdReady = attemptSDMount(false);
}

static void buildCSVRow(const FlightData& d, char* buf, size_t bufLen) {
  snprintf(buf, bufLen,
    "%lu,%u,"
    "%.1f,%.2f,%.2f,%.2f,%d,"
    "%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%d,"
    "%.6f,%.6f,%.2f,%u,%d,"
    "%.2f,%d,%.1f,"
    "%d,%d,%d,%d,"
    "%.1f,%u,%u,%u,%d",
    d.timestamp_ms, d.state,
    d.pressure_pa, d.baro_alt_m, d.agl_m, d.baro_temp_c, d.baro_reading_valid ? 1 : 0,
    d.accel_x_g, d.accel_y_g, d.accel_z_g, d.accel_total_g, d.accel_total_g_filtered,
    d.gyro_x_dps, d.gyro_y_dps, d.gyro_z_dps, d.mpu_temp_c, d.imu_reading_valid ? 1 : 0,
    d.gps_lat, d.gps_lon, d.gps_alt_m, d.gps_sats, d.gps_fix ? 1 : 0,
    d.battery_v, d.rssi_dbm, d.snr_db,
    d.pyro1_continuity ? 1 : 0, d.pyro2_continuity ? 1 : 0,
    d.pyro1_fired ? 1 : 0, d.pyro2_fired ? 1 : 0,
    d.tilt_deg, (unsigned)d.boot_count, (unsigned)d.reset_reason, (unsigned)d.deploy_trigger,
    d.resumed_this_boot ? 1 : 0   // v13
  );
}

// forceFlush: pass true on state transitions / pyro events.
static bool logFlightData(const FlightData& d, bool forceFlush = false) {
  if (!sdReady || !logFile) return false;

  rtcSetLastOp(OP_SD_WRITE);
  buildCSVRow(d, csvRowBuf, sizeof(csvRowBuf));
  logFile.println(csvRowBuf);

  unsigned long now = millis();
  // POST-PYRO FORENSIC WINDOW (v11). On FLIGHT072, ZERO rows survived between
  // the drogue fire at 388.116 s and the reset ~1.2 s later - the state-change
  // forceFlush committed the fire row itself, then the next scheduled flush
  // never happened before power was lost. That 1.2 s window is exactly where
  // the diagnostic value was. Every row inside PYRO_FORCE_FLUSH_MS of any fire
  // event used to be committed to the card immediately — meaning EVERY row.
  //
  // v13: that was itself a watchdog-reset candidate — up to ~50 blocking SD
  // writes/sec during exactly the highest-vibration part of the flight.
  // Flushing on a bounded interval instead still bounds worst-case data
  // loss to well under a second, for far fewer blocking SD ops.
  bool pyroWindow = (lastPyroEventMs != 0) &&
                    ((now - lastPyroEventMs) < PYRO_FORCE_FLUSH_MS);
  bool pyroWindowFlushDue = pyroWindow && (now - lastLogFlushMs >= PYRO_WINDOW_FLUSH_INTERVAL_MS);
  bool normalFlushDue     = !pyroWindow && (now - lastLogFlushMs >= LOG_FLUSH_INTERVAL_MS);
  if (forceFlush || pyroWindowFlushDue || normalFlushDue) {
    logFile.flush();
    lastLogFlushMs = now;
  }
  rtcSetLastOp(OP_IDLE);
  return true;
}

static void closeLog() {
  if (sdReady && logFile) {
    logFile.flush();
    logFile.close();
    sdReady = false;
  }
  sdSessionEnded = true;
}
