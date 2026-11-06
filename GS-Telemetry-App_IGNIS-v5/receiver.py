"""
receiver.py
===========
`ReceiverWorker` runs entirely on its own `QThread` and owns exactly one
serial connection to one IGNIS v2 ground-station receiver board.

This replaces the old `reader_thread()` function. Key differences:

- Nothing here touches shared mutable state directly. Everything the
  GUI needs arrives via Qt signals, so the GUI thread never blocks on a
  lock and never polls.
- A receiver with no port assigned, or whose port disappears, does not
  raise or hang the app — it simply reports OFFLINE and keeps retrying
  on a timer, exactly like the other receiver would if it were absent.
  This is what makes Single-Receiver-A, Single-Receiver-B and
  Dual-Receiver all "the same code path" rather than three branches.
- `set_port()` can be called at any time (even while running) to move a
  receiver to a different COM port live, without recreating the thread.
"""

from __future__ import annotations

import time
from typing import Optional

import serial
from PySide6.QtCore import QThread, Signal

from config import RECONNECT_RETRY_INTERVAL_S, SERIAL_READ_TIMEOUT_S
from telemetry_parser import DashboardBlockParser, TelemetryFrame


class ReceiverWorker(QThread):
    """One serial receiver, one background thread.

    Signals
    -------
    connected(str)         -- receiver_id, port now open
    disconnected(str, str) -- receiver_id, human-readable reason
    frame_ready(object)    -- a fully parsed TelemetryFrame
    raw_line(str, str)     -- receiver_id, raw line (for the log panel)
    status_message(str, str) -- receiver_id, short status string (for log/events)
    """

    connected = Signal(str)
    disconnected = Signal(str, str)
    frame_ready = Signal(object)
    raw_line = Signal(str, str)
    status_message = Signal(str, str)

    def __init__(self, receiver_id: str, baud: int, parent=None) -> None:
        super().__init__(parent)
        self.receiver_id = receiver_id
        self.baud = baud
        self._port: Optional[str] = None
        self._port_lock_requested: Optional[str] = None  # pending port change
        self._reconnect_requested = False
        self._running = False
        self._ser: Optional[serial.Serial] = None
        self._parser = DashboardBlockParser(receiver_id)

    # ------------------------------------------------------------------
    # Public control API (safe to call from the GUI thread)
    # ------------------------------------------------------------------
    def set_port(self, port: Optional[str]) -> None:
        """Assign (or clear, with None) the serial port this worker
        should use. Takes effect on the next reconnect cycle."""
        self._port_lock_requested = port if port else "__NONE__"

    def current_port(self) -> Optional[str]:
        return self._port

    def request_reconnect(self) -> None:
        """Close the current handle on the worker thread and reopen it."""
        self._reconnect_requested = True

    def stop(self) -> None:
        self._running = False
        self._close_serial()

    # ------------------------------------------------------------------
    # QThread entrypoint
    # ------------------------------------------------------------------
    def run(self) -> None:
        self._running = True
        while self._running:
            self._apply_pending_port_change()

            if not self._port:
                # No port assigned yet -- idle politely instead of
                # spinning the CPU or raising.
                time.sleep(0.25)
                continue

            if self._ser is None or not self._ser.is_open:
                self._try_open()
                if self._ser is None:
                    time.sleep(RECONNECT_RETRY_INTERVAL_S)
                    continue

            self._read_loop()

        self._close_serial()

    # ------------------------------------------------------------------
    # Internals
    # ------------------------------------------------------------------
    def _apply_pending_port_change(self) -> None:
        if self._reconnect_requested:
            self._reconnect_requested = False
            self._close_serial()
            self.status_message.emit(self.receiver_id, "Reconnecting…")
        if self._port_lock_requested is None:
            return
        new_port = None if self._port_lock_requested == "__NONE__" else self._port_lock_requested
        self._port_lock_requested = None
        if new_port != self._port:
            self._close_serial()
            self._port = new_port
            if self._port is None:
                self.disconnected.emit(self.receiver_id, "Port cleared")

    def _try_open(self) -> None:
        try:
            self._ser = serial.Serial(self._port, self.baud, timeout=SERIAL_READ_TIMEOUT_S)
            self.connected.emit(self.receiver_id)
            self.status_message.emit(self.receiver_id, f"Connected on {self._port}")
        except serial.SerialException as exc:
            self._ser = None
            self.disconnected.emit(self.receiver_id, f"Could not open {self._port}: {exc}")

    def _read_loop(self) -> None:
        try:
            while self._running:
                self._apply_pending_port_change()
                
                local_ser = self._ser  # local copy to avoid race
                if local_ser is None or not local_ser.is_open:
                    break
                raw = local_ser.readline()
                if not raw:
                    continue
                try:
                    line = raw.decode(errors="ignore").strip()
                except Exception:
                    continue
                if not line:
                    continue

                self.raw_line.emit(self.receiver_id, line)

                frame: Optional[TelemetryFrame] = self._parser.feed_line(line)
                if frame is not None:
                    self.frame_ready.emit(frame)
        
        except serial.SerialException as exc:
            self.disconnected.emit(self.receiver_id, f"Lost connection: {exc}")
            self._close_serial()
            time.sleep(RECONNECT_RETRY_INTERVAL_S)
        
        except OSError as exc:
            # Covers e.g. device unplugged mid-read on some platforms,
            # which pyserial sometimes surfaces as a plain OSError.
            self.disconnected.emit(self.receiver_id, f"I/O error: {exc}")
            self._close_serial()
            time.sleep(RECONNECT_RETRY_INTERVAL_S)

    def _close_serial(self) -> None:
        if self._ser is not None:
            try:
                if self._ser.is_open:
                    self._ser.close()
            except Exception:
                pass
        self._ser = None
