// =============================================================================
// IGNIS v5 Flight Computer: config_v5.h
// Flight constants, feature flags and task/timing parameters.
// Pure preprocessor + constexpr: included by firmware AND host tests, so no
// Arduino or ESP-IDF headers here.
//
// Flight thresholds are carried over unchanged from the reference firmware
// IGNIS_FC_heavy_13/Config.h. The cited line is the origin of each value and
// its justification (flight/pulley-test evidence lives in that file).
// =============================================================================
#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
// BUILD PROFILE
// ---------------------------------------------------------------------------
// IGNIS_FLIGHT_BUILD = 1 is the default. Any bench-only relaxation is a
// compile error in a flight build (see the GROUND_TEST_MODE guard below).
#ifndef IGNIS_FLIGHT_BUILD
#define IGNIS_FLIGHT_BUILD 1
#endif

// Ground testing lowers the altitude interlock and boost debounce
// (reference Config.h:220-225). MUST be false for flight. Compile-time only:
// there is no runtime toggle anywhere (dashboard included).
#ifndef GROUND_TEST_MODE
#define GROUND_TEST_MODE 0
#endif
#if IGNIS_FLIGHT_BUILD && GROUND_TEST_MODE
#error "GROUND_TEST_MODE must be 0 when IGNIS_FLIGHT_BUILD is 1."
#endif
#define GROUND_TEST_MIN_ALT_M      1.5f   // reference Config.h:224
#define GROUND_TEST_DEBOUNCE_MS      40   // reference Config.h:225

// Single-event recovery: CH2 (main) is never fired (reference Config.h:227-237).
// Compile-time only, no runtime toggle.
#ifndef SINGLE_EVENT_MODE
#define SINGLE_EVENT_MODE 1
#endif

// ---------------------------------------------------------------------------
// FEATURE FLAGS (compile-time gates for requirement B and C)
// ---------------------------------------------------------------------------
#ifndef IGNIS_ENABLE_WIFI
#define IGNIS_ENABLE_WIFI 1        // WiFi SoftAP dashboard (read-only)
#endif
#ifndef IGNIS_ENABLE_OTA
#define IGNIS_ENABLE_OTA 1         // OTA upload endpoint; requires IGNIS_ENABLE_WIFI
#endif
#if IGNIS_ENABLE_OTA && !IGNIS_ENABLE_WIFI
#error "IGNIS_ENABLE_OTA requires IGNIS_ENABLE_WIFI."
#endif

// ---------------------------------------------------------------------------
// REBOOT POLICY (requirement A). SAFETY-CRITICAL, PENDING SIGN-OFF.
// ---------------------------------------------------------------------------
// After a non-power-on reset with the flight-in-progress or post-fire flag
// set, the FC boots into RECOVERY: logging + telemetry only, the FSM can never
// produce a fire request, and pyro_hw refuses every fire call.
// This is the ONLY policy implemented. Any other value fails to compile.
#define REBOOT_POLICY_NEVER_FIRE_AFTER_REBOOT 1
#define REBOOT_FIRE_POLICY REBOOT_POLICY_NEVER_FIRE_AFTER_REBOOT
#if REBOOT_FIRE_POLICY != REBOOT_POLICY_NEVER_FIRE_AFTER_REBOOT
#error "Only REBOOT_POLICY_NEVER_FIRE_AFTER_REBOOT is implemented and reviewed."
#endif

// The flags are cleared ONLY on this reset reason (esp_reset_reason_t value).
// Verified against Arduino-ESP32 3.3.12 / ESP-IDF 5.5 esp_system.h:
// ESP_RST_POWERON == 1. An RBF power cut produces a power-on reset.
#define RESET_REASON_POWERON_VALUE 1

// ---------------------------------------------------------------------------
// FLIGHT PROFILE (unchanged from reference)
// ---------------------------------------------------------------------------
#define SEA_LEVEL_PRESSURE_PA        101325.0f // Config.h:104
#define GROUND_PRESSURE_CAL_SAMPLES  20        // Config.h:115
#define MAIN_DEPLOY_ALT_AGL_M        450.0f    // Config.h:143
#define BOOST_ACCEL_THRESHOLD_G      1.8f      // Config.h:154
#define BOOST_DEBOUNCE_MS            250       // Config.h:168
#define BOOST_BARO_CLIMB_MPS         15.0f     // Config.h:175
#define BOOST_BARO_SUSTAIN_MS        150       // Config.h:176
#define BARO_VELOCITY_WINDOW_MS      200       // Config.h:177
#define BURNOUT_ACCEL_THRESHOLD_G    1.3f      // Config.h:180
#define APOGEE_FALLING_SAMPLES       4         // Config.h:183
#define APOGEE_MIN_SAMPLE_INTERVAL_MS 50       // Config.h:184
#define BURNOUT_TIMEOUT_MS           4000      // Config.h:196
// *** RE-TUNE BEFORE EVERY FLIGHT *** OpenRocket burnout->apogee + margin.
#define APOGEE_TIMEOUT_MS            20000     // Config.h:197
#define APOGEE_TIMEOUT_ENABLED       1         // Config.h:198
#define MAIN_MIN_DELAY_AFTER_DROGUE_MS 1500    // Config.h:204
#define MIN_APOGEE_ALT_M             30.0f     // Config.h:218
#define PYRO_FIRE_PULSE_MS           1000      // Config.h:240
#define PYRO_FORCE_FLUSH_MS          4000      // Config.h:245
#define PYRO_WINDOW_FLUSH_INTERVAL_MS 100      // Config.h:309
#define TILT_WARN_DEG                15.0f     // Config.h:257 (diagnostic only)
#define VBAT_NOGO_V                  7.80f     // Config.h:273 (advisory only)
#define VBAT_WARN_V                  8.00f     // Config.h:274
#define LANDED_ALT_DELTA_M           2.0f      // Config.h:276
#define LANDED_STABLE_MS             5000      // Config.h:277

// Pad SAFE<->ARMED. Entry to ARMED is immediate on any continuity (fail closed);
// return to SAFE needs continuity absent for this long.
// ASSUMPTION(v5): new constant, not in reference. Pending review.
#define SAFE_RETURN_DEBOUNCE_MS      1000

// Pyro gate reduced drive (reference Pyro.h:73-104). Kept as in reference.
#define PYRO_GATE_REDUCED_DRIVE      1

// ---------------------------------------------------------------------------
// I2C / sensors
// ---------------------------------------------------------------------------
#define I2C_CLOCK_HZ                 400000    // ASSUMPTION(v5): reference used 100 kHz (Sensors.h:507). Both v5 sensors support 400 kHz
#define I2C_TIMEOUT_MS               50        // Config.h:294
#define I2C_FAIL_RECOVERY_THRESHOLD  10        // Config.h:301

// ---------------------------------------------------------------------------
// LoRa (unchanged RF plan from reference Config.h:129-134)
// ---------------------------------------------------------------------------
#define LORA_FREQ_HZ                 915000000UL
#define LORA_SYNC_WORD               0x12
#define LORA_SF                      9
#define LORA_BW_HZ                   125000UL
#define LORA_CR_DENOM                5
#define LORA_TX_POWER_DBM            17        // PA_BOOST
#define LORA_PREAMBLE_LEN            8         // ASSUMPTION(v5): RadioHead default, matches ground station
#define LORA_TX_TIMEOUT_MS           1500      // abort TX if TxDone never asserts
#define LORA_SPI_HZ                  4000000UL

// ---------------------------------------------------------------------------
// Timing / tasks
// ---------------------------------------------------------------------------
#define SENSOR_LOOP_INTERVAL_MS      20        // Config.h:312 (50 Hz)
#define TELEMETRY_INTERVAL_MS_PAD    1000      // Config.h:313
#define TELEMETRY_INTERVAL_MS_FLT    600       // Config.h:322
#define LOG_FLUSH_INTERVAL_MS        1000      // Config.h:323
#define SD_MOUNT_RETRY_INTERVAL_MS   10000     // Config.h:325
#define SD_SPI_HZ                    4000000UL // DataLogger.h:31
#define SERIAL_STATUS_INTERVAL_MS    1000

#define SPI_MUTEX_TIMEOUT_LORA_MS    20        // LoRa skips its cycle if the bus is busy longer
#define SPI_MUTEX_TIMEOUT_SD_MS      50
#define SD_QUEUE_DEPTH               64        // rows (~1.3 s at 50 Hz)
#define LORA_QUEUE_DEPTH             2

// FreeRTOS priorities / cores. Flight path on core 1, WiFi on core 0.
#define TASK_PRIO_FLIGHT             10
#define TASK_PRIO_LORA               6
#define TASK_PRIO_SD                 5
#define TASK_PRIO_WIFI               1
#define TASK_CORE_FLIGHT             1
#define TASK_CORE_SD                 1
#define TASK_CORE_LORA               1
#define TASK_CORE_WIFI               0

// ---------------------------------------------------------------------------
// WiFi dashboard (requirement B)
// ---------------------------------------------------------------------------
#define WIFI_AP_SSID_PREFIX          "IGNIS-v5-"
#define WIFI_AP_CHANNEL              6
#define WIFI_AP_MAX_CLIENTS          1
// esp_wifi_set_max_tx_power units are 0.25 dBm. 34 = 8.5 dBm.
#define WIFI_MAX_TX_POWER_QDBM       34
#define WIFI_SNAPSHOT_HZ             4         // 2-5 Hz allowed
#define WIFI_BOOT_HOLD_MS            2000      // hold BOOT this long to start the AP
#define WIFI_IDLE_OFF_MS             600000UL  // AP turns off after 10 min with no client
#if WIFI_SNAPSHOT_HZ < 2 || WIFI_SNAPSHOT_HZ > 5
#error "WIFI_SNAPSHOT_HZ must be 2..5"
#endif

// ---------------------------------------------------------------------------
// Standby low power (proposal only, see CHANGES.md). Not implemented.
// ---------------------------------------------------------------------------
#define STANDBY_LOW_POWER_ENABLED    0
