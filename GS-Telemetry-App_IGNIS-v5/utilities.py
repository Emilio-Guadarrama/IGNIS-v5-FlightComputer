"""
utilities.py
============
Small stateless/near-stateless helpers used across the app: persisted
settings (last-used COM ports, theme choice) and common formatting
functions for the GUI, so gui.py doesn't accumulate one-off `f"{x:.1f}"`
snippets scattered through widget code.
"""

from __future__ import annotations

from typing import Optional

from PySide6.QtCore import QSettings

from config import APP_NAME, APP_ORG


def get_settings() -> QSettings:
    return QSettings(APP_ORG, APP_NAME)


def save_last_port(receiver_id: str, port: Optional[str]) -> None:
    s = get_settings()
    s.setValue(f"ports/{receiver_id}", port or "")


def load_last_port(receiver_id: str) -> Optional[str]:
    s = get_settings()
    val = s.value(f"ports/{receiver_id}", "")
    return val or None


def save_theme(theme_name: str) -> None:
    s = get_settings()
    s.setValue("ui/theme", theme_name)


def load_theme() -> str:
    s = get_settings()
    return s.value("ui/theme", "dark")


def format_elapsed(seconds: float) -> str:
    """MET-style HH:MM:SS, matches the convention on the FC's own
    dashboard output (mission-elapsed-time framing)."""
    total = int(max(0, seconds))
    h, rem = divmod(total, 3600)
    m, s = divmod(rem, 60)
    return f"{h:02d}:{m:02d}:{s:02d}"


def format_optional(value, unit: str = "", fmt: str = "{:.2f}", none_text: str = "—") -> str:
    if value is None:
        return none_text
    try:
        return f"{fmt.format(value)}{unit}"
    except (ValueError, TypeError):
        return f"{value}{unit}"


def format_bool_badge(value: Optional[bool], true_text: str, false_text: str, none_text: str = "—") -> str:
    if value is None:
        return none_text
    return true_text if value else false_text
