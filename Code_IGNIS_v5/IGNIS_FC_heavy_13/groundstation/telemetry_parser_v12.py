"""
IGNIS Ground Station — telemetry parser, packet version 11
Ignitia Rocket Lab — LASC 2026

Drop-in replacement for telemetry_parser.py in the PySide6 ground station.
Must stay byte-identical to Telemetry.h on the flight computer.

Verified sizes:
    v10 = 90 bytes
    v11 = 98 bytes   (+8: tilt_deg, boot_count, reset_reason, deploy_trigger)

Both layouts are accepted so old logs still replay. Length alone
distinguishes them.
"""

import struct
from dataclasses import dataclass, asdict
from typing import Optional

# '<' = little-endian, no padding — matches __attribute__((packed)) on ESP32-S3.
_FMT_V10 = "<IIB" "ffff" "B" "ffffffff" "B" "dd" "f" "BB" "f" "BBBB" "B"
_FMT_V11 = _FMT_V10 + "f" "H" "BB"

SIZE_V10 = struct.calcsize(_FMT_V10)   # 90
SIZE_V11 = struct.calcsize(_FMT_V11)   # 98

_FIELDS_V10 = [
    "seq", "timestamp_ms", "state",
    "pressure_pa", "baro_alt_m", "agl_m", "baro_temp_c", "baro_valid",
    "accel_x_g", "accel_y_g", "accel_z_g", "accel_total_g",
    "gyro_x_dps", "gyro_y_dps", "gyro_z_dps", "mpu_temp_c", "imu_valid",
    "gps_lat", "gps_lon", "gps_alt_m", "gps_sats", "gps_fix",
    "battery_v",
    "pyro1_continuity", "pyro2_continuity", "pyro1_fired", "pyro2_fired",
    "sd_logging",
]
_FIELDS_V11 = _FIELDS_V10 + ["tilt_deg", "boot_count", "reset_reason", "deploy_trigger"]

STATE_NAMES = {
    0: "PAD_IDLE", 1: "ARMED", 2: "BOOST", 3: "COAST",
    4: "APOGEE", 5: "DROGUE_DESCENT", 6: "MAIN_DESCENT", 7: "LANDED",
}

# CORRECTED in v12. The earlier table mapped 6 to BROWNOUT. In
# esp_reset_reason_t, 6 is TASK_WDT and BROWNOUT is 9.
RESET_REASONS = {
    0: "UNKNOWN", 1: "POWERON", 2: "EXT", 3: "SW", 4: "PANIC",
    5: "INT_WDT", 6: "TASK_WDT", 7: "WDT", 8: "DEEPSLEEP",
    9: "BROWNOUT", 10: "SDIO", 11: "USB", 12: "JTAG", 15: "CPU_LOCKUP",
}

RESET_BROWNOUT = 9

# Matches VBAT_NOGO_V / VBAT_WARN_V in Config.h. Keep these in sync.
VBAT_NOGO_V = 7.80
VBAT_WARN_V = 8.00

DEPLOY_TRIGGERS = {0: "none", 1: "baro-apogee", 2: "TIMEOUT-FAILSAFE"}

STATE_BOOST = 2


@dataclass
class Packet:
    raw: dict
    version: int

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
        if self.version < 11:
            return "n/a"
        return RESET_REASONS.get(self.raw["reset_reason"], f"OTHER({self.raw['reset_reason']})")

    @property
    def deploy_trigger_name(self) -> str:
        if self.version < 11:
            return "n/a"
        return DEPLOY_TRIGGERS.get(self.raw["deploy_trigger"], "?")

    def as_dict(self) -> dict:
        d = dict(self.raw)
        d["packet_version"] = self.version
        d["state_name"] = self.state_name
        return d


def parse(payload: bytes) -> Optional[Packet]:
    """Decode one LoRa payload. Returns None on a length mismatch.

    Never guess at a short packet. A truncated frame decoded as if it were
    whole produces plausible-looking wrong numbers, which is worse than a
    dropped packet.
    """
    n = len(payload)
    if n == SIZE_V11:
        return Packet(dict(zip(_FIELDS_V11, struct.unpack(_FMT_V11, payload))), 11)
    if n == SIZE_V10:
        return Packet(dict(zip(_FIELDS_V10, struct.unpack(_FMT_V10, payload))), 10)
    return None


CSV_HEADER_V11 = ",".join(
    ["datetime", "recv_millis", "rssi_dbm", "snr_db", "packet_version"] + _FIELDS_V11
)


class FlightAlarms:
    """Live go/no-go checks. See the FLIGHT072 post-mortem for why each exists."""

    def __init__(self):
        self._boot_count_at_arm: Optional[int] = None
        self._last_seq: Optional[int] = None

    def check(self, p: Packet) -> list:
        alarms = []

        # 1. Mid-flight reboot. FLIGHT072 rebooted ~65 m above ground, roughly
        #    2.5 s before impact, and the FC had no record of it — only the
        #    ground station's seq counter resetting to 0 revealed it.
        if p.version >= 11:
            if self._boot_count_at_arm is None and p.state <= 1:
                self._boot_count_at_arm = p.boot_count
            if (self._boot_count_at_arm is not None
                    and p.boot_count > self._boot_count_at_arm
                    and p.state >= STATE_BOOST):
                alarms.append(("CRITICAL", "FC REBOOTED IN FLIGHT "
                                           f"(boot_count {self._boot_count_at_arm} -> {p.boot_count}, "
                                           f"reason {p.reset_reason_name})"))
            if p.reset_reason == RESET_BROWNOUT:
                alarms.append(("CRITICAL", "Last reset was a BROWNOUT"))
            elif p.reset_reason in (4, 5, 6, 7, 15):
                alarms.append(("WARNING",
                               f"Last reset was {p.reset_reason_name} - firmware crash, not power"))

        # 2. Pyro continuity on the pad. FLIGHT072 sat on the rail for 382 s
        #    with CH1 open, printed it to a serial console nobody was reading,
        #    and flew anyway.
        if p.state <= 1:
            if not p.pyro1_continuity:
                alarms.append(("NO-GO", "CH1 (drogue) continuity OPEN"))
            if not p.pyro2_continuity:
                alarms.append(("NO-GO", "CH2 (main) continuity OPEN"))

        # 3. Failsafe deployment path taken.
        if p.version >= 11 and p.deploy_trigger == 2:
            alarms.append(("WARNING",
                           "Deployment fired on TIMEOUT FAILSAFE — barometric "
                           "apogee chain never closed"))

        # 4. Link quality. GSLOG003 lost 40.2% of packets (144 of 241) with one
        #    61-packet blackout, at pad RSSI -123.6 dBm / SNR -12.9 dB. The SF9
        #    demod floor is around -15 dB SNR.
        if self._last_seq is not None:
            gap = p.seq - self._last_seq
            if gap > 5:
                alarms.append(("WARNING", f"{gap - 1} packets lost"))
        self._last_seq = p.seq

        # 5. Battery. Thresholds raised in v12 to match Config.h. The old
        #    7.0 V warning would have shown FLIGHT072 (7.73 V) as green.
        if p.battery_v < 6.5:
            alarms.append(("CRITICAL", f"VBAT {p.battery_v:.2f} V — below AMS1117-5.0 dropout margin"))
        elif p.battery_v < VBAT_NOGO_V:
            level = "NO-GO" if p.state <= 1 else "WARNING"
            alarms.append((level, f"VBAT {p.battery_v:.2f} V — below the {VBAT_NOGO_V:.2f} V threshold"))
        elif p.battery_v < VBAT_WARN_V:
            alarms.append(("WARNING", f"VBAT {p.battery_v:.2f} V — consider a fresh pack"))

        return alarms


if __name__ == "__main__":
    print(f"v10 fmt {_FMT_V10}  size {SIZE_V10}")
    print(f"v11 fmt {_FMT_V11}  size {SIZE_V11}")
    assert SIZE_V10 == 90 and SIZE_V11 == 98, "layout drift — check against Telemetry.h"
    assert len(_FIELDS_V11) == len(struct.unpack(_FMT_V11, b"\x00" * SIZE_V11))
    print("field/format alignment OK")
