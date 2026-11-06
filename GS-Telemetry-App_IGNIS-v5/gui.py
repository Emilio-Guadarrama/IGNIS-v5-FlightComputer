"""High-performance, single-screen IGNIS v4 Ground Control dashboard."""

from __future__ import annotations

import threading
import time
from datetime import datetime
from typing import Optional

import pyqtgraph.exporters
from PySide6.QtCore import Qt, QTimer, Signal
from PySide6.QtGui import QColor, QIcon, QPixmap
from PySide6.QtWidgets import (
    QApplication,
    QDialog,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QStatusBar,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from charts import LiveMetricChart
from config import (
    ACCEL_GAUGE_MAX_G,
    BATTERY_LOW_V,
    DARK,
    LIGHT,
    LOG_TABLE_MAX_ROWS,
    MIN_WINDOW_HEIGHT,
    MIN_WINDOW_WIDTH,
    MISSION_NAME,
    RECEIVER_IDS,
    RECEIVER_LABELS,
    STALE_LINK_TIMEOUT_S,
    STAR_ASSET,
    TEMPERATURE_GAUGE_MAX_C,
    TEMPERATURE_GAUGE_MIN_C,
    VELOCITY_GAUGE_MAX_MPS,
    VELOCITY_GAUGE_MIN_MPS,
    ThemeColors,
)
from gauge import ArcGauge, BatteryIndicator
from logger import SessionLogger
from serial_manager import SerialManager, SerialPortInfo, describe_available_ports
from styles import build_stylesheet
from telemetry_parser import TelemetryFrame
from utilities import (
    format_elapsed,
    format_optional,
    load_last_port,
    load_theme,
    save_last_port,
    save_theme,
)

try:
    import winsound
except ImportError:  # Cross-platform fallback below uses QApplication.beep().
    winsound = None


class MetricReadout(QWidget):
    """Small label/value pair used inside a receiver status panel."""

    def __init__(self, caption: str, unit: str = "") -> None:
        super().__init__()
        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(5)
        caption_label = QLabel(caption.upper())
        caption_label.setProperty("class", "MicroLabel")
        self.value_label = QLabel("—")
        self.value_label.setProperty("class", "MicroValue")
        unit_label = QLabel(unit)
        unit_label.setProperty("class", "MicroUnit")
        layout.addWidget(caption_label)
        layout.addWidget(self.value_label)
        if unit:
            layout.addWidget(unit_label)

    def set_value(self, value: str) -> None:
        self.value_label.setText(value)


class ReceiverStatusCard(QFrame):
    """Static horizontal receiver panel; no combo box or embedded scroll."""

    select_port_requested = Signal(str)
    reconnect_requested = Signal(str)
    cycle_port_requested = Signal(str, int)

    def __init__(self, receiver_id: str, colors: ThemeColors) -> None:
        super().__init__()
        self.receiver_id = receiver_id
        self.colors = colors
        self.received_count = 0
        self.port_info: Optional[SerialPortInfo] = None
        self.assigned_port: Optional[str] = None
        self.setProperty("class", "ReceiverCard")
        self.setMinimumHeight(105)
        self.setMaximumHeight(118)

        root = QVBoxLayout(self)
        root.setContentsMargins(13, 9, 13, 9)
        root.setSpacing(5)

        header = QHBoxLayout()
        self.led = QLabel()
        self.led.setObjectName("ReceiverLED")
        self.led.setFixedSize(12, 12)
        self.name_label = QLabel(RECEIVER_LABELS.get(receiver_id, f"Receiver {receiver_id}"))
        self.name_label.setProperty("class", "ReceiverName")
        self.status_badge = QLabel("OFFLINE")
        self.status_badge.setProperty("state", "offline")
        self.status_badge.setProperty("class", "StatusPill")
        self.port_button = QPushButton("ASIGNAR PUERTO")
        self.port_button.setObjectName("PortButton")
        self.port_button.clicked.connect(lambda: self.select_port_requested.emit(self.receiver_id))
        previous_port = QPushButton("‹")
        previous_port.setObjectName("PortArrowButton")
        previous_port.setFixedWidth(27)
        previous_port.setToolTip("Puerto anterior")
        previous_port.clicked.connect(
            lambda: self.cycle_port_requested.emit(self.receiver_id, -1)
        )
        next_port = QPushButton("›")
        next_port.setObjectName("PortArrowButton")
        next_port.setFixedWidth(27)
        next_port.setToolTip("Puerto siguiente")
        next_port.clicked.connect(
            lambda: self.cycle_port_requested.emit(self.receiver_id, 1)
        )
        reconnect_button = QPushButton("↻")
        reconnect_button.setObjectName("IconButton")
        reconnect_button.setFixedWidth(34)
        reconnect_button.setToolTip("Reconectar este receptor")
        reconnect_button.clicked.connect(lambda: self.reconnect_requested.emit(self.receiver_id))
        header.addWidget(self.led)
        header.addWidget(self.name_label)
        header.addWidget(self.status_badge)
        header.addStretch(1)
        header.addWidget(previous_port)
        header.addWidget(self.port_button)
        header.addWidget(next_port)
        header.addWidget(reconnect_button)
        root.addLayout(header)

        detail_row = QHBoxLayout()
        self.device_label = QLabel("Sin puerto asignado")
        self.device_label.setProperty("class", "DeviceInfo")
        self.device_label.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.verification_label = QLabel("NO VERIFICADO")
        self.verification_label.setProperty("class", "VerificationUnknown")
        detail_row.addWidget(self.device_label, 1)
        detail_row.addWidget(self.verification_label)
        root.addLayout(detail_row)

        metrics = QHBoxLayout()
        metrics.setSpacing(18)
        self.rssi = MetricReadout("RSSI", "dBm")
        self.snr = MetricReadout("SNR", "dB")
        self.rx = MetricReadout("RX")
        self.loss = MetricReadout("Loss", "%")
        for metric in (self.rssi, self.snr, self.rx, self.loss):
            metrics.addWidget(metric)
        metrics.addStretch(1)
        root.addLayout(metrics)
        self.set_state("OFFLINE")

    def _repolish(self, widget: QWidget) -> None:
        widget.style().unpolish(widget)
        widget.style().polish(widget)

    def set_port(self, info: Optional[SerialPortInfo], raw_port: Optional[str] = None) -> None:
        self.port_info = info
        self.assigned_port = info.device if info else raw_port
        if info:
            self.port_button.setText(info.device)
            self.port_button.setToolTip(info.tooltip)
            self.device_label.setText(f"{info.transport} · {info.friendly_name}")
            self.device_label.setToolTip(info.tooltip)
        elif raw_port:
            self.port_button.setText(raw_port)
            self.device_label.setText("Puerto guardado no disponible")
            self.device_label.setToolTip(raw_port)
        else:
            self.port_button.setText("ASIGNAR PUERTO")
            self.device_label.setText("Sin puerto asignado")
            self.device_label.setToolTip("")
        self.set_verified(False)

    def set_state(self, state: str) -> None:
        normalized = state.lower()
        self.status_badge.setText(state)
        self.status_badge.setProperty("state", normalized)
        self.led.setProperty("state", normalized)
        self._repolish(self.status_badge)
        self._repolish(self.led)

    def set_verified(self, verified: bool) -> None:
        self.verification_label.setText("IGNIS VERIFIED" if verified else "NO VERIFICADO")
        self.verification_label.setProperty(
            "class", "VerificationGood" if verified else "VerificationUnknown"
        )
        self._repolish(self.verification_label)

    def update_frame(self, frame: TelemetryFrame) -> None:
        self.received_count += 1
        self.rssi.set_value(format_optional(frame.rssi_dbm, "", "{:d}"))
        self.snr.set_value(format_optional(frame.snr_db, "", "{:.1f}"))
        self.rx.set_value(str(self.received_count))
        self.loss.set_value(format_optional(frame.loss_pct_onboard, "", "{:.1f}"))
        self.set_verified(True)

    def reset_flight_values(self) -> None:
        self.received_count = 0
        self.rssi.set_value("—")
        self.snr.set_value("—")
        self.rx.set_value("0")
        self.loss.set_value("—")


class PortPickerDialog(QDialog):
    """Button grid for choosing a COM port without a dropdown/list."""

    def __init__(self, receiver_id: str, ports: list[SerialPortInfo],
                 unavailable: set[str], parent: QWidget) -> None:
        super().__init__(parent)
        self.selected_port: Optional[str] = None
        self.setWindowTitle(f"Asignar puerto · Receptor {receiver_id}")
        self.setMinimumWidth(720)
        root = QVBoxLayout(self)
        root.setContentsMargins(18, 18, 18, 18)
        root.setSpacing(12)
        title = QLabel(f"SELECCIONA EL PUERTO PARA EL RECEPTOR {receiver_id}")
        title.setProperty("class", "DialogTitle")
        root.addWidget(title)
        hint = QLabel("Los nombres provienen del Administrador de dispositivos del sistema.")
        hint.setProperty("class", "MutedText")
        root.addWidget(hint)

        grid = QGridLayout()
        grid.setSpacing(8)
        if not ports:
            empty = QLabel("No se detectaron puertos seriales. Conecta la placa y pulsa Actualizar puertos.")
            empty.setWordWrap(True)
            grid.addWidget(empty, 0, 0)
        for index, info in enumerate(ports):
            button = QPushButton(f"{info.device}\n{info.friendly_name}")
            button.setProperty("class", "PortChoice")
            button.setMinimumHeight(58)
            button.setToolTip(info.tooltip)
            button.setEnabled(info.device not in unavailable)
            button.clicked.connect(lambda checked=False, port=info.device: self._choose(port))
            grid.addWidget(button, index // 3, index % 3)
        root.addLayout(grid)

        actions = QHBoxLayout()
        clear_button = QPushButton("Dejar sin asignar")
        clear_button.clicked.connect(lambda: self._choose(None))
        cancel_button = QPushButton("Cancelar")
        cancel_button.clicked.connect(self.reject)
        actions.addWidget(clear_button)
        actions.addStretch(1)
        actions.addWidget(cancel_button)
        root.addLayout(actions)

    def _choose(self, port: Optional[str]) -> None:
        self.selected_port = port
        self.accept()


class EventLogDialog(QDialog):
    def __init__(self, colors: ThemeColors, parent: QWidget) -> None:
        super().__init__(parent)
        self.colors = colors
        self.setWindowTitle("IGNIS · Telemetry & Event Log")
        self.resize(980, 460)
        layout = QVBoxLayout(self)
        header = QHBoxLayout()
        title = QLabel("TELEMETRY & EVENT LOG")
        title.setProperty("class", "DialogTitle")
        clear_button = QPushButton("Limpiar vista")
        clear_button.clicked.connect(lambda: self.table.setRowCount(0))
        header.addWidget(title)
        header.addStretch(1)
        header.addWidget(clear_button)
        layout.addLayout(header)
        self.table = QTableWidget(0, 6)
        self.table.setHorizontalHeaderLabels(
            ["Timestamp", "Receiver", "Packet", "RSSI", "SNR", "Note"]
        )
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        self.table.setAlternatingRowColors(True)
        self.table.setShowGrid(False)
        header_view = self.table.horizontalHeader()
        for column in range(5):
            header_view.setSectionResizeMode(column, QHeaderView.ResizeMode.ResizeToContents)
        header_view.setSectionResizeMode(5, QHeaderView.ResizeMode.Stretch)
        layout.addWidget(self.table)

    def append(self, receiver_id: str, seq: str, rssi: str, snr: str, note: str) -> None:
        row = self.table.rowCount()
        self.table.insertRow(row)
        values = [datetime.now().strftime("%H:%M:%S"), receiver_id, seq, rssi, snr, note]
        for column, value in enumerate(values):
            item = QTableWidgetItem(value)
            lowered = note.lower()
            if any(word in lowered for word in ("error", "lost", "desconect", "could not")):
                item.setForeground(QColor(self.colors.bad))
            elif any(word in lowered for word in ("connected", "conectado", "apogee", "apogeo")):
                item.setForeground(QColor(self.colors.good))
            self.table.setItem(row, column, item)
        if self.table.rowCount() > LOG_TABLE_MAX_ROWS:
            self.table.removeRow(0)
        self.table.scrollToBottom()

    def set_colors(self, colors: ThemeColors) -> None:
        """Recolor existing event rows when the application theme changes."""
        self.colors = colors
        for row in range(self.table.rowCount()):
            note_item = self.table.item(row, 5)
            lowered = note_item.text().lower() if note_item else ""
            color = colors.text_primary
            if any(word in lowered for word in ("error", "lost", "desconect", "could not")):
                color = colors.bad
            elif any(word in lowered for word in ("connected", "conectado", "apogee", "apogeo")):
                color = colors.good
            for column in range(self.table.columnCount()):
                item = self.table.item(row, column)
                if item is not None:
                    item.setForeground(QColor(color))


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle(MISSION_NAME)
        self.resize(1600, 900)
        self.setMinimumSize(MIN_WINDOW_WIDTH, MIN_WINDOW_HEIGHT)
        if STAR_ASSET.exists():
            self.setWindowIcon(QIcon(str(STAR_ASSET)))

        saved_theme = load_theme()
        self._theme_name = saved_theme if saved_theme in {"dark", "light"} else "dark"
        self.colors = LIGHT if self._theme_name == "light" else DARK
        self.serial_manager = SerialManager()
        self.session_logger = SessionLogger()
        self._start_time = time.monotonic()
        self._flight_number = 1
        self._telemetry_history: list[TelemetryFrame] = []
        self._last_frame_time: dict[str, float] = {}
        self._receiver_state = {rid: "OFFLINE" for rid in RECEIVER_IDS}
        self._disconnect_reason: dict[str, str] = {}
        self._available_ports: list[SerialPortInfo] = []
        self._assigned_ports: dict[str, Optional[str]] = {rid: None for rid in RECEIVER_IDS}
        self._last_physics_seq: Optional[int] = None
        self._prev_altitude_sample: Optional[tuple[float, float]] = None
        self._apogee_seen = False
        self._battery_alert_triggered = False
        self._flight_sd_state: Optional[str] = None
        self._flight_sd_updated_at: Optional[float] = None
        self._ground_sd_states: dict[str, Optional[str]] = {rid: None for rid in RECEIVER_IDS}
        self._ground_sd_updated_at: dict[str, Optional[float]] = {rid: None for rid in RECEIVER_IDS}

        self.receiver_cards: dict[str, ReceiverStatusCard] = {}
        self._build_ui()
        self._wire_signals()
        self._apply_theme()
        self._restore_ports()

        self.mission_timer = QTimer(self)
        self.mission_timer.timeout.connect(self._tick)
        self.mission_timer.start(1000)
        self.serial_manager.start_all()
        self.status_bar.showMessage("Ground station lista · operación 100% offline", 4000)

    @property
    def telemetry_history(self) -> tuple[TelemetryFrame, ...]:
        return tuple(self._telemetry_history)

    def _build_ui(self) -> None:
        central = QWidget()
        self.setCentralWidget(central)
        outer = QVBoxLayout(central)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)
        outer.addWidget(self._build_header())

        self.alert_banner = QFrame()
        self.alert_banner.setObjectName("ConnectionBanner")
        self.alert_banner.setFixedHeight(30)
        banner_layout = QHBoxLayout(self.alert_banner)
        banner_layout.setContentsMargins(16, 0, 16, 0)
        self.alert_label = QLabel("ALERTA · receptores A y B sin conexión")
        self.alert_label.setObjectName("ConnectionBannerText")
        banner_layout.addWidget(self.alert_label)
        banner_layout.addStretch(1)
        outer.addWidget(self.alert_banner)

        dashboard = QWidget()
        dashboard_layout = QVBoxLayout(dashboard)
        dashboard_layout.setContentsMargins(12, 9, 12, 8)
        dashboard_layout.setSpacing(8)
        dashboard_layout.addLayout(self._build_receivers_row())
        dashboard_layout.addLayout(self._build_instruments_row())
        dashboard_layout.addLayout(self._build_charts_row(), 1)
        dashboard_layout.addWidget(self._build_flight_strip())
        outer.addWidget(dashboard, 1)

        self.event_dialog = EventLogDialog(self.colors, self)
        self.status_bar = QStatusBar()
        self.status_bar.setSizeGripEnabled(False)
        self.setStatusBar(self.status_bar)

    def _build_header(self) -> QWidget:
        header = QFrame()
        header.setObjectName("TopBar")
        header.setFixedHeight(64)
        layout = QHBoxLayout(header)
        layout.setContentsMargins(16, 7, 16, 7)
        layout.setSpacing(9)
        logo = QLabel()
        pixmap = QPixmap(str(STAR_ASSET))
        if not pixmap.isNull():
            logo.setPixmap(pixmap.scaledToHeight(38, Qt.TransformationMode.SmoothTransformation))
        logo.setFixedWidth(44)
        title = QLabel("IGNIS")
        title.setObjectName("MissionTitle")
        subtitle = QLabel("GROUND CONTROL · LASC 2026")
        subtitle.setProperty("class", "HeaderSubtitle")
        self.overall_led = QLabel()
        self.overall_led.setObjectName("TopBarLED")
        self.overall_led.setFixedSize(12, 12)
        self.overall_status = QLabel("NO LINK")
        self.overall_status.setProperty("class", "HeaderStatus")
        self.flight_state_badge = QLabel("PAD_IDLE")
        self.flight_state_badge.setObjectName("FlightStateBadge")
        self.timer_label = QLabel("00:00:00")
        self.timer_label.setObjectName("MissionTimer")

        new_flight = QPushButton("NUEVO VUELO")
        new_flight.setObjectName("PrimaryButton")
        new_flight.clicked.connect(self._on_new_flight)
        ports = QPushButton("AUTO COM")
        ports.setToolTip("Asigna automáticamente los dos puertos más probables")
        ports.clicked.connect(self._auto_assign_ports)
        export = QPushButton("EXPORTAR")
        export.clicked.connect(self._on_export_charts)
        events = QPushButton("EVENTOS")
        events.clicked.connect(self.event_log_show)
        self.theme_button = QPushButton()
        self.theme_button.setObjectName("ThemeButton")
        self.theme_button.setFixedWidth(38)
        self.theme_button.clicked.connect(self._toggle_theme)
        self._update_theme_button()

        layout.addWidget(logo)
        layout.addWidget(title)
        layout.addWidget(subtitle)
        layout.addSpacing(12)
        layout.addWidget(self.overall_led)
        layout.addWidget(self.overall_status)
        layout.addStretch(1)
        layout.addWidget(self.flight_state_badge)
        layout.addWidget(self.timer_label)
        layout.addWidget(new_flight)
        layout.addWidget(ports)
        layout.addWidget(export)
        layout.addWidget(events)
        layout.addWidget(self.theme_button)
        return header

    def _build_receivers_row(self) -> QHBoxLayout:
        row = QHBoxLayout()
        row.setSpacing(8)
        for rid in RECEIVER_IDS:
            card = ReceiverStatusCard(rid, self.colors)
            self.receiver_cards[rid] = card
            row.addWidget(card, 1)
        return row

    def _build_instruments_row(self) -> QHBoxLayout:
        row = QHBoxLayout()
        row.setSpacing(8)
        self.temperature_gauge = ArcGauge(
            "Temperatura del cohete", "°C", TEMPERATURE_GAUGE_MIN_C,
            TEMPERATURE_GAUGE_MAX_C, "#F50057", self.colors
        )
        self.velocity_gauge = ArcGauge(
            "Velocidad vertical", "m/s", VELOCITY_GAUGE_MIN_MPS,
            VELOCITY_GAUGE_MAX_MPS, "#FF9100", self.colors
        )
        self.acceleration_gauge = ArcGauge(
            "Aceleración total", "g", 0, ACCEL_GAUGE_MAX_G, "#E15A00", self.colors
        )
        self.battery_indicator = BatteryIndicator(self.colors)
        for instrument in (
            self.temperature_gauge, self.velocity_gauge,
            self.acceleration_gauge, self.battery_indicator,
        ):
            row.addWidget(instrument, 1)
        return row

    def _build_charts_row(self) -> QHBoxLayout:
        row = QHBoxLayout()
        row.setSpacing(8)

        link_column = QVBoxLayout()
        link_column.setSpacing(8)
        self.rssi_chart = LiveMetricChart(
            "RSSI · LINK QUALITY", "dBm", self.colors, (-140, -20), compact=True
        )
        self.snr_chart = LiveMetricChart(
            "SNR · SIGNAL CLARITY", "dB", self.colors, (-20, 15), compact=True
        )
        link_column.addWidget(self.rssi_chart, 1)
        link_column.addWidget(self.snr_chart, 1)

        self.altitude_chart = LiveMetricChart(
            "ALTITUD AGL · TRAYECTORIA DE VUELO", "Altitud (m)", self.colors,
            y_range=None, x_label="Tiempo de misión (s)", compact=False,
            adaptive_y=True, initial_y_range=(0, 100), x_window=60,
        )
        self.altitude_chart.setToolTip(
            "Escala automática: inicia en 0–100 m y se amplía conforme aumenta la altitud."
        )
        row.addLayout(link_column, 2)
        row.addWidget(self.altitude_chart, 3)
        return row

    def _build_flight_strip(self) -> QWidget:
        strip = QFrame()
        strip.setObjectName("FlightStrip")
        strip.setFixedHeight(40)
        layout = QHBoxLayout(strip)
        layout.setContentsMargins(14, 0, 14, 0)
        self.gps_status = QLabel("GPS  — · SAT —")
        self.apogee_status = QLabel("APOGEE  NO")
        self.pyro_status = QLabel("PYRO  —")
        for label in (self.gps_status, self.apogee_status, self.pyro_status):
            label.setProperty("class", "FlightStripValue")
            layout.addWidget(label)
            layout.addStretch(1)
        self.flight_sd_status = QLabel()
        self.ground_sd_status = QLabel()
        for label in (self.flight_sd_status, self.ground_sd_status):
            label.setProperty("class", "StorageStatus")
            layout.addWidget(label)
        self._set_storage_label(self.flight_sd_status, "FLIGHT SD", "NO_DATA")
        self._set_storage_label(self.ground_sd_status, "GROUND SD", "NO_DATA")
        return strip

    def event_log_show(self) -> None:
        self.event_dialog.show()
        self.event_dialog.raise_()
        self.event_dialog.activateWindow()

    def _wire_signals(self) -> None:
        self.serial_manager.connected.connect(self._on_receiver_connected)
        self.serial_manager.disconnected.connect(self._on_receiver_disconnected)
        self.serial_manager.frame_ready.connect(self._on_frame_ready)
        self.serial_manager.status_message.connect(
            lambda rid, message: self.status_bar.showMessage(f"[{rid}] {message}", 3000)
        )
        for card in self.receiver_cards.values():
            card.select_port_requested.connect(self._on_select_port)
            card.reconnect_requested.connect(self._on_reconnect)
            card.cycle_port_requested.connect(self._cycle_port)

    def _restore_ports(self) -> None:
        self._available_ports = describe_available_ports()
        by_device = {item.device: item for item in self._available_ports}
        for rid in RECEIVER_IDS:
            saved = load_last_port(rid)
            self._assigned_ports[rid] = saved
            self.receiver_cards[rid].set_port(by_device.get(saved), saved)
            if saved and saved in by_device:
                self.serial_manager.assign_port(rid, saved)

    def _on_refresh_ports(self) -> None:
        self._available_ports = self.serial_manager.refresh_ports()
        by_device = {item.device: item for item in self._available_ports}
        for rid, port in self._assigned_ports.items():
            self.receiver_cards[rid].set_port(by_device.get(port), port)
        self.status_bar.showMessage(
            f"{len(self._available_ports)} puerto(s) detectado(s). Pulsa el COM de cada receptor para asignar.",
            5000,
        )

    def _on_select_port(self, receiver_id: str) -> None:
        self._available_ports = self.serial_manager.refresh_ports()
        unavailable = {
            port for rid, port in self._assigned_ports.items()
            if rid != receiver_id and port is not None
        }
        dialog = PortPickerDialog(receiver_id, self._available_ports, unavailable, self)
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return
        self._assign_port(receiver_id, dialog.selected_port)

    def _assign_port(self, receiver_id: str, port: Optional[str]) -> None:
        """Single assignment path used by dialog, arrows and auto-select."""
        self._assigned_ports[receiver_id] = port
        save_last_port(receiver_id, port)
        info = next((item for item in self._available_ports if item.device == port), None)
        self.receiver_cards[receiver_id].set_port(info, port)
        self.serial_manager.assign_port(receiver_id, port)
        self.session_logger.log_event(receiver_id, "PORT_CHANGE", port or "cleared")
        self.status_bar.showMessage(
            f"Receptor {receiver_id}: {port or 'sin puerto'}", 3500
        )

    def _cycle_port(self, receiver_id: str, direction: int) -> None:
        """Change COM with one click while preventing duplicate assignments."""
        self._available_ports = self.serial_manager.refresh_ports()
        other_ports = {
            port for rid, port in self._assigned_ports.items()
            if rid != receiver_id and port is not None
        }
        candidates = [item.device for item in self._available_ports if item.device not in other_ports]
        if not candidates:
            self.status_bar.showMessage("No hay otro puerto disponible.", 3000)
            return
        current = self._assigned_ports.get(receiver_id)
        if current not in candidates:
            target = candidates[0 if direction >= 0 else -1]
        else:
            index = candidates.index(current)
            target = candidates[(index + direction) % len(candidates)]
        self._assign_port(receiver_id, target)

    @staticmethod
    def _port_priority(info: SerialPortInfo) -> int:
        """Rank likely telemetry adapters above audio/Bluetooth profile ports."""
        fingerprint = f"{info.friendly_name} {info.manufacturer} {info.hwid}".lower()
        score = 0
        if info.transport == "USB":
            score += 30
        for token in ("ignis", "esp32", "cp210", "ch340", "ftdi", "uart", "usb serial"):
            if token in fingerprint:
                score += 20
        if "bt04" in fingerprint:
            score += 15
        for token in ("airpods", "headset", "headphone", "audio"):
            if token in fingerprint:
                score -= 100
        return score

    def _auto_assign_ports(self) -> None:
        """Assign A/B using OS metadata, without selecting probable audio ports."""
        self._available_ports = self.serial_manager.refresh_ports()
        ranked = sorted(
            self._available_ports,
            key=lambda item: (self._port_priority(item), item.device),
            reverse=True,
        )
        candidates = [item for item in ranked if self._port_priority(item) > -50]
        if not candidates:
            QMessageBox.warning(
                self, "Sin puertos", "No se detectaron puertos aptos para telemetría."
            )
            return
        for rid, info in zip(RECEIVER_IDS, candidates[:2]):
            self._assign_port(rid, info.device)
        if len(candidates) < 2:
            self._assign_port("B", None)
        self.status_bar.showMessage(
            "Puertos asignados automáticamente. Confirma IGNIS VERIFIED en cada tarjeta.",
            6000,
        )

    def _on_reconnect(self, receiver_id: str) -> None:
        if self._assigned_ports.get(receiver_id):
            self.serial_manager.reconnect(receiver_id)
            self.status_bar.showMessage(f"Reconectando receptor {receiver_id}…", 3000)
        else:
            self._on_select_port(receiver_id)

    def _on_receiver_connected(self, receiver_id: str) -> None:
        previous = self._receiver_state[receiver_id]
        self._receiver_state[receiver_id] = "ONLINE"
        self._last_frame_time[receiver_id] = time.monotonic()
        self.receiver_cards[receiver_id].set_state("ONLINE")
        self._disconnect_reason.pop(receiver_id, None)
        if previous != "ONLINE":
            self._append_log(receiver_id, "", "", "", "Puerto conectado")
            self.session_logger.log_event(receiver_id, "CONNECTED")
        self._update_link_status()

    def _on_receiver_disconnected(self, receiver_id: str, reason: str) -> None:
        previous = self._receiver_state[receiver_id]
        repeated = self._disconnect_reason.get(receiver_id) == reason
        self._disconnect_reason[receiver_id] = reason
        self._receiver_state[receiver_id] = "OFFLINE"
        self.receiver_cards[receiver_id].set_state("OFFLINE")
        self.receiver_cards[receiver_id].set_verified(False)
        if previous != "OFFLINE" or not repeated:
            self._append_log(receiver_id, "", "", "", f"Desconectado: {reason}")
            self.session_logger.log_event(receiver_id, "DISCONNECTED", reason)
        if previous in {"ONLINE", "STALE"}:
            self._connection_alarm()
        self._update_link_status()

    def _update_link_status(self) -> None:
        online = [rid for rid, state in self._receiver_state.items() if state == "ONLINE"]
        stale = [rid for rid, state in self._receiver_state.items() if state == "STALE"]
        offline = [rid for rid, state in self._receiver_state.items() if state == "OFFLINE"]
        if len(online) == len(RECEIVER_IDS):
            state, label, message = "online", "FULL LINK", "LINK NOMINAL · A + B ONLINE"
        elif online:
            state, label = "warning", f"PARTIAL {len(online)}/2"
            message = "DEGRADED LINK · CHECK " + ", ".join(stale + offline)
        else:
            state, label = "offline", "NO LINK"
            message = "ALERT · " + ("NO DATA " + ", ".join(stale) if stale else "A + B OFFLINE")
        self.overall_status.setText(label)
        self.overall_led.setProperty("state", state)
        self.alert_banner.setProperty("state", state)
        self.alert_label.setText(message)
        for widget in (self.overall_led, self.alert_banner):
            widget.style().unpolish(widget)
            widget.style().polish(widget)

    def _on_frame_ready(self, frame: TelemetryFrame) -> None:
        rid = frame.receiver_id
        self._telemetry_history.append(frame)
        self._last_frame_time[rid] = time.monotonic()
        self._receiver_state[rid] = "ONLINE"
        card = self.receiver_cards[rid]
        card.set_state("ONLINE")
        card.update_frame(frame)

        if frame.rssi_dbm is not None:
            self.rssi_chart.add_point(rid, frame.seq or 0, frame.rssi_dbm)
        if frame.snr_db is not None:
            self.snr_chart.add_point(rid, frame.seq or 0, frame.snr_db)
        if frame.agl_m is not None:
            elapsed = time.monotonic() - self._start_time
            self.altitude_chart.add_point(rid, elapsed, frame.agl_m)
        self._update_storage_status(frame)
        self.session_logger.log_frame(frame)
        self._append_log(
            rid, str(frame.seq) if frame.seq is not None else "",
            format_optional(frame.rssi_dbm, "", "{:d}"),
            format_optional(frame.snr_db, "", "{:.1f}"), "",
        )
        if frame.seq != self._last_physics_seq:
            self._last_physics_seq = frame.seq
            self._update_flight_metrics(frame)
        self._update_link_status()

    def _update_flight_metrics(self, frame: TelemetryFrame) -> None:
        if frame.flight_state:
            self.flight_state_badge.setText(frame.flight_state)
        if frame.agl_m is not None:
            now = time.monotonic()
            if self._prev_altitude_sample:
                previous_time, previous_altitude = self._prev_altitude_sample
                delta = now - previous_time
                if delta > 0.05:
                    self.velocity_gauge.set_value((frame.agl_m - previous_altitude) / delta)
            self._prev_altitude_sample = (now, frame.agl_m)
        if frame.baro_temp_c is not None:
            self.temperature_gauge.set_value(frame.baro_temp_c)
        if frame.accel_total_g is not None:
            self.acceleration_gauge.set_value(abs(frame.accel_total_g))
        if frame.battery_v is not None:
            self.battery_indicator.set_value(frame.battery_v)
            if frame.battery_v <= BATTERY_LOW_V and not self._battery_alert_triggered:
                self._battery_alert_triggered = True
                self._battery_alarm()
            elif frame.battery_v > BATTERY_LOW_V:
                self._battery_alert_triggered = False
        if frame.gps_fix is not None:
            gps_text = "GPS FIX" if frame.gps_fix else "GPS NO FIX"
            sats_text = str(frame.gps_sats) if frame.gps_sats is not None else "—"
            self.gps_status.setText(f"{gps_text} · SAT {sats_text}")
            self.gps_status.setProperty("state", "good" if frame.gps_fix else "bad")
            self.gps_status.style().unpolish(self.gps_status)
            self.gps_status.style().polish(self.gps_status)
        if frame.gps_sats is not None:
            gps_text = "GPS FIX" if frame.gps_fix else "GPS NO FIX" if frame.gps_fix is False else "GPS —"
            self.gps_status.setText(f"{gps_text} · SAT {frame.gps_sats}")
        if frame.is_apogee_or_later and not self._apogee_seen:
            self._apogee_seen = True
            self.apogee_status.setText("APOGEE  CONFIRMED")
            self.apogee_status.setProperty("state", "good")
            self.apogee_status.style().unpolish(self.apogee_status)
            self.apogee_status.style().polish(self.apogee_status)
            self.session_logger.log_event(frame.receiver_id, "APOGEE_DETECTED")
            self._append_log(frame.receiver_id, "", "", "", "Apogeo confirmado")
        continuity = []
        if frame.pyro1_cont is not None:
            continuity.append("CH1 OK" if frame.pyro1_cont else "CH1 OPEN")
        if frame.pyro2_cont is not None:
            continuity.append("CH2 OK" if frame.pyro2_cont else "CH2 OPEN")
        if continuity:
            self.pyro_status.setText("PYRO  " + " · ".join(continuity))

    def _update_storage_status(self, frame: TelemetryFrame) -> None:
        """Update SD indicators only when firmware supplies explicit evidence."""
        now = time.monotonic()
        if frame.flight_sd_state is not None:
            self._flight_sd_state = frame.flight_sd_state
            self._flight_sd_updated_at = now
            self._set_storage_label(
                self.flight_sd_status, "FLIGHT SD", frame.flight_sd_state
            )
        if frame.ground_sd_state is not None:
            self._ground_sd_states[frame.receiver_id] = frame.ground_sd_state
            self._ground_sd_updated_at[frame.receiver_id] = now
            self._refresh_ground_sd_label(now)

    def _refresh_ground_sd_label(self, now: Optional[float] = None) -> None:
        """Aggregate A/B ground-station SD reports without hiding an error."""
        now = time.monotonic() if now is None else now
        fresh_states = [
            state for rid, state in self._ground_sd_states.items()
            if state is not None
            and self._ground_sd_updated_at[rid] is not None
            and now - self._ground_sd_updated_at[rid] <= STALE_LINK_TIMEOUT_S
        ]
        if not fresh_states:
            state = "STALE" if any(self._ground_sd_states.values()) else "NO_DATA"
        elif "ERROR" in fresh_states:
            state = "ERROR"
        elif "WRITING" in fresh_states:
            state = "WRITING"
        else:
            state = "OK"
        details = " · ".join(
            f"{rid}:{self._ground_sd_states[rid] or '—'}" for rid in RECEIVER_IDS
        )
        self.ground_sd_status.setToolTip(f"Estado reportado por receptor: {details}")
        self._set_storage_label(self.ground_sd_status, "GROUND SD", state)

    @staticmethod
    def _set_storage_label(label: QLabel, source: str, state: str) -> None:
        display = "NO DATA" if state == "NO_DATA" else state
        label.setText(f"●  {source}  {display}")
        label.setProperty("state", state.lower())
        label.style().unpolish(label)
        label.style().polish(label)

    def _on_new_flight(self) -> None:
        self._flight_number += 1
        self._start_time = time.monotonic()
        self._last_physics_seq = None
        self._prev_altitude_sample = None
        self._apogee_seen = False
        self._battery_alert_triggered = False
        self.rssi_chart.start_new_flight()
        self.snr_chart.start_new_flight()
        self.altitude_chart.start_new_flight()
        for instrument in (
            self.temperature_gauge, self.velocity_gauge,
            self.acceleration_gauge, self.battery_indicator,
        ):
            instrument.set_value(None, animated=False)
        for card in self.receiver_cards.values():
            card.reset_flight_values()
        self.flight_state_badge.setText("PAD_IDLE")
        self.timer_label.setText("00:00:00")
        self.gps_status.setText("GPS  — · SAT —")
        self.apogee_status.setText("APOGEE  NO")
        self.pyro_status.setText("PYRO  —")
        for label in (self.gps_status, self.apogee_status):
            label.setProperty("state", "")
            label.style().unpolish(label)
            label.style().polish(label)
        self._flight_sd_state = None
        self._flight_sd_updated_at = None
        self._ground_sd_states = {rid: None for rid in RECEIVER_IDS}
        self._ground_sd_updated_at = {rid: None for rid in RECEIVER_IDS}
        self._set_storage_label(self.flight_sd_status, "FLIGHT SD", "NO_DATA")
        self._set_storage_label(self.ground_sd_status, "GROUND SD", "NO_DATA")
        self.session_logger.start_new_flight(self._flight_number)
        self._append_log("SYSTEM", "", "", "", f"Nuevo vuelo #{self._flight_number}")
        self.status_bar.showMessage(
            "Lienzo reiniciado; historial completo conservado en memoria y CSV.", 5000
        )

    def _append_log(self, receiver_id: str, seq: str, rssi: str,
                    snr: str, note: str) -> None:
        self.event_dialog.append(receiver_id, seq, rssi, snr, note)
        if note:
            self.status_bar.showMessage(note, 5000)

    def _tick(self) -> None:
        self.timer_label.setText(format_elapsed(time.monotonic() - self._start_time))
        now = time.monotonic()
        if (
            self._flight_sd_state is not None
            and self._flight_sd_updated_at is not None
            and now - self._flight_sd_updated_at > STALE_LINK_TIMEOUT_S
        ):
            self._set_storage_label(self.flight_sd_status, "FLIGHT SD", "STALE")
        self._refresh_ground_sd_label(now)
        for rid in RECEIVER_IDS:
            last = self._last_frame_time.get(rid)
            if self._receiver_state[rid] == "ONLINE" and last is not None:
                if now - last > STALE_LINK_TIMEOUT_S:
                    self._receiver_state[rid] = "STALE"
                    self.receiver_cards[rid].set_state("STALE")
                    self.receiver_cards[rid].set_verified(False)
                    self.session_logger.log_event(rid, "STALE_LINK", "No telemetry > 5 s")
                    self._append_log(rid, "", "", "", "Sin telemetría por más de 5 s")
                    self._connection_alarm()
                    self._update_link_status()

    def _apply_theme(self) -> None:
        app = QApplication.instance()
        if app:
            app.setStyleSheet(build_stylesheet(self.colors))

    def _toggle_theme(self) -> None:
        self._theme_name = "light" if self._theme_name == "dark" else "dark"
        self.colors = LIGHT if self._theme_name == "light" else DARK
        save_theme(self._theme_name)
        self._apply_theme()
        for chart in (self.rssi_chart, self.snr_chart, self.altitude_chart):
            chart.set_colors(self.colors)
        for instrument in (
            self.temperature_gauge, self.velocity_gauge,
            self.acceleration_gauge, self.battery_indicator,
        ):
            instrument.set_colors(self.colors)
        for card in self.receiver_cards.values():
            card.colors = self.colors
        self.event_dialog.set_colors(self.colors)
        self._update_theme_button()
        self.status_bar.showMessage(
            "Modo claro activado" if self._theme_name == "light" else "Modo oscuro activado",
            2500,
        )

    def _update_theme_button(self) -> None:
        if self._theme_name == "dark":
            self.theme_button.setText("☀")
            self.theme_button.setToolTip("Cambiar a modo claro")
        else:
            self.theme_button.setText("☾")
            self.theme_button.setToolTip("Cambiar a modo oscuro")

    def _on_export_charts(self) -> None:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        names = []
        for prefix, chart in (
            ("altitude", self.altitude_chart),
            ("rssi", self.rssi_chart),
            ("snr", self.snr_chart),
        ):
            name = f"{prefix}_chart_{timestamp}.png"
            exporter = pyqtgraph.exporters.ImageExporter(chart.plot_widget.plotItem)
            exporter.parameters()["width"] = 1200
            exporter.export(name)
            names.append(name)
        QMessageBox.information(self, "Gráficas exportadas", "\n".join(names))

    def _battery_alarm(self) -> None:
        self._play_tones([(1000, 240), (1000, 240), (1000, 300)])

    def _connection_alarm(self) -> None:
        self._play_tones([(720, 170), (520, 260)])

    def _play_tones(self, tones: list[tuple[int, int]]) -> None:
        if winsound is None:
            QApplication.beep()
            return

        def task() -> None:
            for frequency, duration in tones:
                winsound.Beep(frequency, duration)
                time.sleep(0.07)

        threading.Thread(target=task, daemon=True).start()

    def closeEvent(self, event) -> None:  # noqa: N802
        self.serial_manager.stop_all()
        self.session_logger.close()
        self.event_dialog.close()
        super().closeEvent(event)
