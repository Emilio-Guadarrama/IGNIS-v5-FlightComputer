"""
serial_manager.py
==================
Owns the (up to two) `ReceiverWorker` threads and exposes port
discovery. This is the layer that makes "Single-A / Single-B / Dual"
a non-issue: the manager doesn't care how many receivers are actually
assigned a port at any given moment. Zero, one, or two is all fine.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

import serial.tools.list_ports
from PySide6.QtCore import QObject, Signal

from config import DEFAULT_BAUD, RECEIVER_IDS
from receiver import ReceiverWorker


def list_available_ports() -> list[str]:
    """Return device names of all serial ports currently visible to the
    OS, e.g. ['COM8', 'COM9'] on Windows or ['/dev/ttyUSB0'] on Linux."""
    return [p.device for p in serial.tools.list_ports.comports()]


@dataclass(frozen=True)
class SerialPortInfo:
    """Friendly device information exposed by the operating system."""

    device: str
    description: str
    manufacturer: str
    hwid: str
    vid: Optional[int]
    pid: Optional[int]
    serial_number: str

    @property
    def transport(self) -> str:
        fingerprint = f"{self.description} {self.hwid}".lower()
        if "bluetooth" in fingerprint or "bthenum" in fingerprint:
            return "Bluetooth serial"
        if self.vid is not None or "usb" in fingerprint:
            return "USB"
        return "Puerto serial"

    @property
    def friendly_name(self) -> str:
        generic = {"n/a", "unknown", "usb serial port", "serial port"}
        description = (self.description or "").strip()
        manufacturer = (self.manufacturer or "").strip()
        if description and description.lower() not in generic:
            return description
        if manufacturer and manufacturer.lower() not in generic:
            return manufacturer
        return "Dispositivo serial"

    @property
    def combo_label(self) -> str:
        return f"{self.device} — {self.friendly_name}"

    @property
    def summary(self) -> str:
        parts = [self.transport, self.friendly_name]
        if self.vid is not None and self.pid is not None:
            parts.append(f"VID:PID {self.vid:04X}:{self.pid:04X}")
        return " · ".join(parts)

    @property
    def tooltip(self) -> str:
        lines = [f"Puerto: {self.device}", f"Tipo: {self.transport}",
                 f"Nombre: {self.friendly_name}"]
        if self.manufacturer:
            lines.append(f"Fabricante: {self.manufacturer}")
        if self.serial_number:
            lines.append(f"Número de serie: {self.serial_number}")
        if self.hwid:
            lines.append(f"Hardware ID: {self.hwid}")
        return "\n".join(lines)


def describe_available_ports() -> list[SerialPortInfo]:
    """Return every COM port together with Device Manager metadata."""
    result = []
    for port in serial.tools.list_ports.comports():
        result.append(SerialPortInfo(
            device=port.device,
            description=port.description or "",
            manufacturer=port.manufacturer or "",
            hwid=port.hwid or "",
            vid=port.vid,
            pid=port.pid,
            serial_number=port.serial_number or "",
        ))
    return sorted(result, key=lambda item: item.device)


class SerialManager(QObject):
    """Thin coordinator around the two ReceiverWorker threads.

    The GUI talks to this object, not to ReceiverWorker directly, so it
    never has to know how many receivers exist or juggle two sets of
    thread handles itself.
    """

    # Re-exposed / fanned-out signals, same shape as ReceiverWorker's.
    connected = Signal(str)
    disconnected = Signal(str, str)
    frame_ready = Signal(object)
    raw_line = Signal(str, str)
    status_message = Signal(str, str)

    def __init__(self, baud: int = DEFAULT_BAUD, parent: Optional[QObject] = None) -> None:
        super().__init__(parent)
        self.baud = baud
        self.workers: dict[str, ReceiverWorker] = {}
        for rid in RECEIVER_IDS:
            worker = ReceiverWorker(rid, baud, parent=self)
            worker.connected.connect(self.connected)
            worker.disconnected.connect(self.disconnected)
            worker.frame_ready.connect(self.frame_ready)
            worker.raw_line.connect(self.raw_line)
            worker.status_message.connect(self.status_message)
            self.workers[rid] = worker

    def start_all(self) -> None:
        for worker in self.workers.values():
            if not worker.isRunning():
                worker.start()

    def stop_all(self) -> None:
        for worker in self.workers.values():
            worker.stop()
        for worker in self.workers.values():
            worker.wait(2000)

    def assign_port(self, receiver_id: str, port: Optional[str]) -> None:
        """Assign/clear the port for one receiver. Safe to call at any
        time, including while the app is already running (this is what
        lets a receiver 'appear' or 'disappear' live)."""
        worker = self.workers.get(receiver_id)
        if worker is None:
            return
        worker.set_port(port)

    def current_ports(self) -> dict[str, Optional[str]]:
        return {rid: w.current_port() for rid, w in self.workers.items()}

    def reconnect(self, receiver_id: str) -> None:
        worker = self.workers.get(receiver_id)
        if worker is not None:
            worker.request_reconnect()

    def refresh_ports(self) -> list[SerialPortInfo]:
        """Re-scan the OS for serial ports. Does not auto-assign --
        the GUI decides what to do with the new list (e.g. repopulate
        combo boxes, or warn if an assigned port vanished)."""
        return describe_available_ports()
