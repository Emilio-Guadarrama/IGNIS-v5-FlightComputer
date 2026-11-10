"""
IGNIS Ground Station: telemetry parser for IGNIS v5 firmware.

Same 98-byte packet as parser v12 (packet version 11). The byte layout is
UNCHANGED; the only difference is the state table:
    0 = SAFE (was PAD_IDLE; same pad slot, so `state <= 1` pad checks still hold)
    5 = DESCENT (single deployment: under the parachute until LANDED)
    8 = RECOVERY (rebooted after landing / without a usable flight record)
pyro1_fired / pyro2_fired = at least one attempt (CH1 primary, CH2 backup).

Derived from IGNIS_FC_heavy_13/groundstation/telemetry_parser_v12.py. Keep in
sync with src/core/telemetry_packet.h (static_assert == 98).
"""

import struct
from dataclasses import dataclass
from typing import Optional

_FMT_V10 = "<IIB" "ffff" "B" "ffffffff" "B" "dd" "f" "BB" "f" "BBBB" "B"
_FMT_V11 = _FMT_V10 + "f" "H" "BB"
SIZE_V11 = struct.calcsize(_FMT_V11)   # 98

_FIELDS_V11 = [
    "seq", "timestamp_ms", "state",
    "pressure_pa", "baro_alt_m", "agl_m", "baro_temp_c", "baro_valid",
    "accel_x_g", "accel_y_g", "accel_z_g", "accel_total_g",
    "gyro_x_dps", "gyro_y_dps", "gyro_z_dps", "mpu_temp_c", "imu_valid",
    "gps_lat", "gps_lon", "gps_alt_m", "gps_sats", "gps_fix",
    "battery_v",
    "pyro1_continuity", "pyro2_continuity", "pyro1_fired", "pyro2_fired",
    "sd_logging",
    "tilt_deg", "boot_count", "reset_reason", "deploy_trigger",
]

STATE_NAMES = {
    0: "SAFE", 1: "ARMED", 2: "BOOST", 3: "COAST", 4: "APOGEE",
    5: "DESCENT", 6: "MAIN_DESCENT(unused)", 7: "LANDED", 8: "RECOVERY",
}
STATE_BOOST = 2
STATE_RECOVERY = 8

# esp_reset_reason_t, verified against Arduino-ESP32 3.3.12 / ESP-IDF 5.5.
RESET_REASONS = {
    0: "UNKNOWN", 1: "POWERON", 2: "EXT", 3: "SW", 4: "PANIC",
    5: "INT_WDT", 6: "TASK_WDT", 7: "WDT", 8: "DEEPSLEEP",
    9: "BROWNOUT", 10: "SDIO", 11: "USB", 12: "JTAG", 13: "EFUSE",
    14: "PWR_GLITCH", 15: "CPU_LOCKUP",
}
DEPLOY_TRIGGERS = {0: "none", 1: "baro-apogee", 2: "TIMEOUT-FAILSAFE"}


@dataclass
class Packet:
    raw: dict

    def __getattr__(self, name):
        try:
            return self.raw[name]
        except KeyError as exc:
            raise AttributeError(name) from exc

    @property
    def state_name(self) -> str:
        return STATE_NAMES.get(self.raw["state"], f"UNKNOWN({self.raw['state']})")

    @property
    def reset_reason_name(self) -> str:
        return RESET_REASONS.get(self.raw["reset_reason"], f"OTHER({self.raw['reset_reason']})")


def parse(payload: bytes) -> Optional[Packet]:
    """Decode one 98-byte payload (RadioHead header already stripped)."""
    if len(payload) != SIZE_V11:
        return None
    return Packet(dict(zip(_FIELDS_V11, struct.unpack(_FMT_V11, payload))))


def recovery_alarm(p: Packet) -> Optional[str]:
    if p.state == STATE_RECOVERY:
        return (f"FC in RECOVERY after reset ({p.reset_reason_name}); "
                "pyro locked out until power cycle")
    return None


if __name__ == "__main__":
    assert SIZE_V11 == 98, "layout drift: check src/core/telemetry_packet.h"
    assert len(_FIELDS_V11) == len(struct.unpack(_FMT_V11, b"\x00" * SIZE_V11))
    sample = bytearray(98)
    sample[8] = 8      # state RECOVERY
    sample[96] = 9     # BROWNOUT
    p = parse(bytes(sample))
    assert p is not None and p.state_name == "RECOVERY" and p.reset_reason_name == "BROWNOUT"
    assert recovery_alarm(p)
    print("telemetry_parser_v5 self-check OK (98 bytes)")
