"""
telemetry_parser.py
====================
Turns raw serial text lines from an IGNIS v2 Ground Station receiver
board into structured `TelemetryFrame` objects.

The receiver's .ino prints its *entire* dashboard once per
DASHBOARD_INTERVAL_MS, delimited by a line of "===...===" at the start
and end of every block. This parser is a small state machine: it
accumulates lines between delimiters and, once a block closes, extracts
every field it can find with the regexes in config.py.

Design notes
------------
- The parser is intentionally line-oriented and tolerant: a field that
  fails to match just stays None on the frame rather than raising, since
  a torn/garbled serial line should degrade gracefully, not crash the
  reader thread.
- This module has zero Qt / threading / serial dependency so it can be
  unit-tested in isolation and reused (e.g. for offline CSV replay).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Optional

from config import (
    RE_BARO,
    RE_BLOCK_DELIM,
    RE_BOOT,
    RE_FSM,
    RE_GPS_FIX,
    RE_GPS_NOFIX,
    RE_HEADER,
    RE_IMU,
    RE_LINK,
    RE_FLIGHT_SD_PATTERNS,
    RE_GROUND_SD_PATTERNS,
    RE_PWR,
    RE_PYRO,
    RE_SD_DASHBOARD,
)


@dataclass
class TelemetryFrame:
    """One fully-parsed dashboard block from a single receiver."""

    receiver_id: str
    received_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))

    flight_state: Optional[str] = None
    seq: Optional[int] = None
    since_last_rx_ms: Optional[int] = None

    rssi_dbm: Optional[int] = None
    snr_db: Optional[float] = None
    rx_count_onboard: Optional[int] = None
    lost_count_onboard: Optional[int] = None
    loss_pct_onboard: Optional[float] = None

    agl_m: Optional[float] = None
    abs_alt_m: Optional[float] = None
    baro_temp_c: Optional[float] = None
    baro_valid: Optional[bool] = None

    accel_x_g: Optional[float] = None
    accel_y_g: Optional[float] = None
    accel_z_g: Optional[float] = None
    accel_total_g: Optional[float] = None
    gyro_x_dps: Optional[float] = None
    gyro_y_dps: Optional[float] = None
    gyro_z_dps: Optional[float] = None
    mpu_temp_c: Optional[float] = None
    imu_valid: Optional[bool] = None

    battery_v: Optional[float] = None

    # Explicit storage states reported by the firmware. None means that the
    # source did not report a status; the GUI renders that as NO DATA.
    flight_sd_state: Optional[str] = None
    ground_sd_state: Optional[str] = None

    gps_fix: Optional[bool] = None
    gps_sats: Optional[int] = None
    gps_lat: Optional[float] = None
    gps_lon: Optional[float] = None
    gps_alt_m: Optional[float] = None

    pyro1_cont: Optional[bool] = None
    pyro2_cont: Optional[bool] = None
    pyro1_fired: Optional[bool] = None
    pyro2_fired: Optional[bool] = None

    tilt_deg: Optional[float] = None
    boot_count: Optional[int] = None
    reset_reason: Optional[int] = None
    reset_reason_name: Optional[str] = None
    deploy_trigger: Optional[str] = None

    @property
    def is_apogee_or_later(self) -> bool:
        from config import APOGEE_OR_LATER_STATES
        return self.flight_state in APOGEE_OR_LATER_STATES

    def is_valid(self) -> bool:
        """A frame is considered usable if it at least carries a seq
        number and a link RSSI reading — everything else is optional
        (e.g. GPS may have no fix, sensors may be STALE)."""
        return self.seq is not None and self.rssi_dbm is not None


class DashboardBlockParser:
    """Stateful, per-receiver accumulator that feeds one raw serial line
    at a time and yields a `TelemetryFrame` whenever a full block closes.

    Usage:
        parser = DashboardBlockParser(receiver_id="A")
        frame = parser.feed_line(line)
        if frame is not None:
            ... handle frame ...
    """

    def __init__(self, receiver_id: str) -> None:
        self.receiver_id = receiver_id
        self._in_block = False
        self._buffer: list[str] = []

    def feed_line(self, line: str) -> Optional[TelemetryFrame]:
        line = line.rstrip("\r\n")
        if not line:
            return None

        if RE_BLOCK_DELIM.match(line.strip()):
            if not self._in_block:
                # Opening delimiter of a new block.
                self._in_block = True
                self._buffer = []
                return None
            else:
                # Closing delimiter -> parse what we accumulated.
                self._in_block = False
                block_lines = self._buffer
                self._buffer = []
                return self._parse_block(block_lines)

        if self._in_block:
            self._buffer.append(line)
        return None

    def _parse_block(self, lines: list[str]) -> Optional[TelemetryFrame]:
        text = "\n".join(lines)
        frame = TelemetryFrame(receiver_id=self.receiver_id)

        m = RE_HEADER.search(text)
        if m:
            frame.flight_state = m.group("state")
            frame.seq = int(m.group("seq"))
            frame.since_last_rx_ms = int(m.group("since_ms"))
        else:
            # No header usually means "waiting for first packet..." block.
            return None

        m = RE_LINK.search(text)
        if m:
            frame.rssi_dbm = int(m.group("rssi"))
            frame.snr_db = float(m.group("snr"))
            frame.rx_count_onboard = int(m.group("rx"))
            frame.lost_count_onboard = int(m.group("lost"))
            frame.loss_pct_onboard = float(m.group("loss_pct"))

        m = RE_BARO.search(text)
        if m:
            frame.agl_m = float(m.group("agl"))
            frame.abs_alt_m = float(m.group("abs_alt"))
            frame.baro_temp_c = float(m.group("baro_temp"))
            frame.baro_valid = m.group("baro_valid") == "OK"

        m = RE_IMU.search(text)
        if m:
            frame.accel_x_g = float(m.group("ax"))
            frame.accel_y_g = float(m.group("ay"))
            frame.accel_z_g = float(m.group("az"))
            frame.accel_total_g = float(m.group("a_total"))
            if m.group("gx") is not None:
                frame.gyro_x_dps = float(m.group("gx"))
                frame.gyro_y_dps = float(m.group("gy"))
                frame.gyro_z_dps = float(m.group("gz"))
                frame.mpu_temp_c = float(m.group("imu_temp"))
                frame.imu_valid = m.group("imu_valid") == "OK"

        m = RE_PWR.search(text)
        if m:
            frame.battery_v = float(m.group("batt"))

        # Heavy4 reports both cards on one line. Prefer that exact protocol;
        # generic labelled forms remain supported for recorded older builds.
        m = RE_SD_DASHBOARD.search(text)
        if m:
            frame.flight_sd_state = self._normalize_sd_state(m.group("flight"))
            ground_value = m.group("ground").strip()
            frame.ground_sd_state = (
                "ERROR" if ground_value.upper() in {"OFF", "ERROR", "FAILED"}
                else "WRITING"
            )
        else:
            frame.flight_sd_state = self._extract_sd_state(text, RE_FLIGHT_SD_PATTERNS)
            frame.ground_sd_state = self._extract_sd_state(text, RE_GROUND_SD_PATTERNS)

        m = RE_GPS_FIX.search(text)
        if m:
            frame.gps_fix = True
            frame.gps_sats = int(m.group("sats"))
            frame.gps_lat = float(m.group("lat"))
            frame.gps_lon = float(m.group("lon"))
            frame.gps_alt_m = float(m.group("alt"))
        else:
            m = RE_GPS_NOFIX.search(text)
            if m:
                frame.gps_fix = False
                frame.gps_sats = int(m.group("sats"))

        m = RE_PYRO.search(text)
        if m:
            frame.pyro1_cont = m.group("c1_cont") == "OK"
            frame.pyro2_cont = m.group("c2_cont") == "OK"
            frame.pyro1_fired = m.group("c1_fired") == "YES"
            frame.pyro2_fired = m.group("c2_fired") == "YES"

        m = RE_FSM.search(text)
        if m:
            frame.tilt_deg = float(m.group("tilt"))
            frame.deploy_trigger = m.group("trigger")

        m = RE_BOOT.search(text)
        if m:
            frame.boot_count = int(m.group("count"))
            frame.reset_reason = int(m.group("reason"))
            frame.reset_reason_name = m.group("reason_name")

        return frame if frame.is_valid() else None

    @staticmethod
    def _extract_sd_state(text: str, patterns: tuple) -> Optional[str]:
        """Return a stable UI state from tolerant firmware status aliases."""
        for pattern in patterns:
            match = pattern.search(text)
            if not match:
                continue
            return DashboardBlockParser._normalize_sd_state(match.group("state"))
        return None

    @staticmethod
    def _normalize_sd_state(value: str) -> str:
        raw = " ".join(value.upper().split())
        if raw in {
            "WRITING", "WRITE", "LOGGING", "SAVING", "ACTIVE",
            "GUARDANDO", "TRUE", "1",
        }:
            return "WRITING"
        if raw in {"OK", "READY", "MOUNTED", "LISTA"}:
            return "OK"
        return "ERROR"
