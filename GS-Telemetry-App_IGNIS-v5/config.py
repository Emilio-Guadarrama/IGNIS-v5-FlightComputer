"""
config.py
=========
Central configuration for the IGNIS v2 Ground Control Station.

All tunables that used to be scattered at the top of the old monolithic
script (PORT_A, BAUD, WINDOW_SIZE, regex patterns, etc.) live here so the
rest of the codebase never hardcodes them.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path
from typing import Final

# ----------------------------------------------------------------------
# Identity / branding
# ----------------------------------------------------------------------
APP_ORG: Final[str] = "IgnitiaRocketLab"
APP_NAME: Final[str] = "IGNIS Ground Control Station"
MISSION_NAME: Final[str] = "IGNIS v4 — LASC 2026 Rocket Challenge"
MIN_WINDOW_WIDTH: Final[int] = 1280
MIN_WINDOW_HEIGHT: Final[int] = 720

# Visual scales. Incoming values are never clamped in memory or CSV; only the
# progress drawing saturates at these limits.
ALTITUDE_GAUGE_MAX_M: Final[float] = 1500.0
VELOCITY_GAUGE_MIN_MPS: Final[float] = -100.0
VELOCITY_GAUGE_MAX_MPS: Final[float] = 300.0
ACCEL_GAUGE_MAX_G: Final[float] = 20.0
TEMPERATURE_GAUGE_MIN_C: Final[float] = -20.0
TEMPERATURE_GAUGE_MAX_C: Final[float] = 100.0
BATTERY_EMPTY_V: Final[float] = 6.4
BATTERY_FULL_V: Final[float] = 8.4
# Synchronized with Heavy12 Config.h and Heavy4's GS_VBAT_* values.
BATTERY_LOW_V: Final[float] = 7.80
BATTERY_WARN_V: Final[float] = 8.00

# ----------------------------------------------------------------------
# Serial defaults
# ----------------------------------------------------------------------
DEFAULT_BAUD: Final[int] = 115200
SERIAL_READ_TIMEOUT_S: Final[float] = 1.0
RECONNECT_RETRY_INTERVAL_S: Final[float] = 3.0
# How long with no data before a receiver is considered "stale" (still
# connected physically, but the FC link or the GS itself may be dead).
STALE_LINK_TIMEOUT_S: Final[float] = 5.0

RECEIVER_IDS: Final[tuple[str, str]] = ("A", "B")
RECEIVER_LABELS: Final[dict[str, str]] = {
    "A": "Receiver A (vertical)",
    "B": "Receiver B (tilted)",
}

# ----------------------------------------------------------------------
# Chart / rolling window
# ----------------------------------------------------------------------
CHART_WINDOW_SIZE: Final[int] = 150   # packets kept on-screen per receiver
LOG_TABLE_MAX_ROWS: Final[int] = 500  # cap on bottom scrolling log widget

# ----------------------------------------------------------------------
# Paths
# ----------------------------------------------------------------------
PROJECT_ROOT: Final[Path] = Path(__file__).resolve().parent
LOG_DIR: Final[Path] = PROJECT_ROOT / "telemetry_logs"
ASSET_DIR: Final[Path] = PROJECT_ROOT / "assets"
STAR_ASSET: Final[Path] = ASSET_DIR / "estrella.svg"

# ----------------------------------------------------------------------
# Parsing patterns
#
# These match the exact printf/Serial.printf layout emitted by
# printDashboard() in the IGNIS v2 Ground Station .ino sketch. Each
# ground-station receiver board re-prints the *entire* dashboard block
# (state, link, baro, imu, sht40, gps, pwr, sd, pyro) once per
# DASHBOARD_INTERVAL_MS (500 ms). We parse the whole block, not just
# seq/RSSI/SNR, so the GUI can show altitude, battery, GPS fix, etc.
# ----------------------------------------------------------------------
RE_BLOCK_DELIM = re.compile(r"^=+$")
RE_HEADER = re.compile(
    r"STATE:\s*(?P<state>\S+)\s*\|\s*seq\s*#(?P<seq>-?\d+)\s*\|\s*last RX\s*(?P<since_ms>\d+)ms ago"
)
RE_LINK = re.compile(
    r"LINK\s+RSSI:\s*(?P<rssi>-?\d+)\s*dBm\s+SNR:\s*(?P<snr>-?\d+\.?\d*)\s*dB\s+"
    r"RX:\s*(?P<rx>\d+)\s+lost:\s*(?P<lost>\d+)\s*\((?P<loss_pct>[\d.]+)%\)"
)
RE_BARO = re.compile(
    r"BARO\s+AGL:\s*(?P<agl>-?\d+\.?\d*)\s*m\s+ABS:\s*(?P<abs_alt>-?\d+\.?\d*)\s*m\s+"
    r"T:\s*(?P<baro_temp>-?\d+\.?\d*)\s*C\s+\[(?P<baro_valid>OK|STALE)\]"
)
RE_IMU = re.compile(
    r"IMU\s+accel X:\s*(?P<ax>-?\d+\.?\d*)\s+Y:\s*(?P<ay>-?\d+\.?\d*)\s+Z:\s*(?P<az>-?\d+\.?\d*)\s+"
    r"\|\s*(?P<a_total>-?\d+\.?\d*)\s*\|g"
    r"(?:\s+gyro X:\s*(?P<gx>-?\d+\.?\d*)\s+Y:\s*(?P<gy>-?\d+\.?\d*)\s+Z:\s*(?P<gz>-?\d+\.?\d*)\s+dps"
    r"\s+T:\s*(?P<imu_temp>-?\d+\.?\d*)\s*C\s+\[(?P<imu_valid>OK|STALE)\])?"
)
RE_GPS_FIX = re.compile(
    r"GPS\s+FIX\s+sats:\s*(?P<sats>\d+)\s+lat:\s*(?P<lat>-?\d+\.?\d*)\s+lon:\s*(?P<lon>-?\d+\.?\d*)\s+alt:\s*(?P<alt>-?\d+\.?\d*)\s*m"
)
RE_GPS_NOFIX = re.compile(r"GPS\s+NO FIX\s+sats:\s*(?P<sats>\d+)")
RE_PWR = re.compile(r"PWR\s+battery:\s*(?P<batt>-?\d+\.?\d*)\s*V")

# SD health is intentionally parsed from explicitly labelled sources. A bare
# ``SD: OK`` is ambiguous on a receiver dashboard, so it is not interpreted as
# either the flight computer or the ground station. Canonical firmware lines:
# ``FC_SD: WRITING`` and ``GS_SD: WRITING``.
SD_STATE_TOKEN: Final[str] = (
    r"WRITING|WRITE|LOGGING|SAVING|ACTIVE|GUARDANDO|"
    r"OK|READY|MOUNTED|LISTA|"
    r"NOT\s+WRITING|NOT\s+LOGGING|ERROR|FAIL(?:ED)?|OFFLINE|OFF|MISSING|NO\s+CARD|NOT\s+MOUNTED|"
    r"TRUE|FALSE|1|0"
)
# Exact Heavy4 printDashboard() output, e.g.
# ``SD      FC:LOGGING         GS:/GSLOG000.CSV``.
RE_SD_DASHBOARD = re.compile(
    r"^\s*SD\s+FC:\s*(?P<flight>NOT\s+logging|LOGGING)\s+GS:\s*(?P<ground>\S+)",
    re.IGNORECASE | re.MULTILINE,
)
RE_FLIGHT_SD_PATTERNS = (
    re.compile(
        rf"(?:FLIGHT(?:\s+COMPUTER)?|FC|ROCKET|ONBOARD)[\s_-]*SD(?:\s+CARD)?"
        rf"\s*[:=]\s*(?P<state>{SD_STATE_TOKEN})\b",
        re.IGNORECASE,
    ),
    re.compile(
        rf"SD(?:\s+CARD)?[\s_-]*(?:FLIGHT(?:\s+COMPUTER)?|FC|ROCKET|ONBOARD)"
        rf"\s*[:=]\s*(?P<state>{SD_STATE_TOKEN})\b",
        re.IGNORECASE,
    ),
)
RE_GROUND_SD_PATTERNS = (
    re.compile(
        rf"(?:GROUND(?:\s+STATION)?|GS|RECEIVER)[\s_-]*SD(?:\s+CARD)?"
        rf"\s*[:=]\s*(?P<state>{SD_STATE_TOKEN})\b",
        re.IGNORECASE,
    ),
    re.compile(
        rf"SD(?:\s+CARD)?[\s_-]*(?:GROUND(?:\s+STATION)?|GS|RECEIVER)"
        rf"\s*[:=]\s*(?P<state>{SD_STATE_TOKEN})\b",
        re.IGNORECASE,
    ),
)
RE_PYRO = re.compile(
    r"PYRO\s+CH1\(drogue\)\s+cont:\s*(?P<c1_cont>OK|OPEN)\s+fired:\s*(?P<c1_fired>YES|no)\s+\|\s+"
    r"CH2\(main\)\s+cont:\s*(?P<c2_cont>OK|OPEN)\s+fired:\s*(?P<c2_fired>YES|no)"
)
RE_FSM = re.compile(
    r"FSM\s+tilt:\s*(?P<tilt>-?\d+\.?\d*)\s+deg.*?deploy_trigger:\s*(?P<trigger>\S+)"
)
RE_BOOT = re.compile(
    r"BOOT\s+count:\s*(?P<count>\d+)\s+reset_reason:\s*(?P<reason>\d+)\s*(?P<reason_name>\S+)?"
)

# Flight states in the order the FC firmware enumerates them.
FLIGHT_STATES: Final[list[str]] = [
    "PAD_IDLE", "ARMED", "BOOST", "COAST", "APOGEE",
    "DROGUE_DESCENT", "MAIN_DESCENT", "LANDED",
]
APOGEE_OR_LATER_STATES: Final[set[str]] = {
    "APOGEE", "DROGUE_DESCENT", "MAIN_DESCENT", "LANDED",
}

# ----------------------------------------------------------------------
# CSV
# ----------------------------------------------------------------------
CSV_HEADER: Final[list[str]] = [
    "datetime_utc", "receiver_id", "event_type",
    "flight_state", "seq", "rssi_dbm", "snr_db",
    "agl_m", "abs_alt_m", "baro_temp_c", "baro_valid",
    "accel_total_g",
    "battery_v",
    "gps_fix", "gps_sats", "gps_lat", "gps_lon", "gps_alt_m",
    "pyro1_cont", "pyro2_cont", "pyro1_fired", "pyro2_fired",
    "detail",
]


@dataclass
class ThemeColors:
    """Palette for the dark 'mission control' theme. Kept as plain data
    so styles.py (QSS) and charts.py (pyqtgraph) both pull from one
    source of truth instead of duplicating hex codes."""

    bg_root: str = "#090C10"
    bg_panel: str = "#0D1117"
    bg_card: str = "#161B22"
    bg_card_hover: str = "#1F2630"
    border: str = "#30363D"
    text_primary: str = "#F0F6FC"
    text_secondary: str = "#B1BAC4"
    text_muted: str = "#7D8590"
    accent: str = "#E15A00"
    accent_soft: str = "#3B1D0A"
    good: str = "#00E676"
    warn: str = "#FFEA00"
    bad: str = "#FF1744"
    receiver_a: str = "#00B0FF"
    receiver_b: str = "#FF9100"
    good_bg: str = "#06261A"
    warn_bg: str = "#2B2400"
    bad_bg: str = "#330914"
    info_bg: str = "#082231"


@dataclass
class LightThemeColors(ThemeColors):
    bg_root: str = "#eef2f6"
    bg_panel: str = "#ffffff"
    bg_card: str = "#f7f9fb"
    bg_card_hover: str = "#eef3f8"
    border: str = "#d6dee6"
    text_primary: str = "#101820"
    text_secondary: str = "#4a5b6c"
    text_muted: str = "#8494a3"
    accent: str = "#E15A00"
    accent_soft: str = "#FFF0E6"
    good: str = "#1a9850"
    warn: str = "#b8860b"
    bad: str = "#d92c2c"
    receiver_a: str = "#0d6efd"
    receiver_b: str = "#e8730c"
    good_bg: str = "#DDF7E8"
    warn_bg: str = "#FFF7CC"
    bad_bg: str = "#FDE3EA"
    info_bg: str = "#E2F3FF"


DARK = ThemeColors()
LIGHT = LightThemeColors()
