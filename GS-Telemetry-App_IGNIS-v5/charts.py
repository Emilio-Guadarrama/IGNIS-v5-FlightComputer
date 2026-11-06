"""Compact, self-contained live charts for the telemetry dashboard."""

from __future__ import annotations

import math
from typing import Optional

import pyqtgraph as pg
from PySide6.QtCore import Qt
from PySide6.QtWidgets import QCheckBox, QFrame, QHBoxLayout, QLabel, QVBoxLayout, QWidget

from config import CHART_WINDOW_SIZE, RECEIVER_IDS, ThemeColors


class RollingSeries:
    def __init__(self) -> None:
        self.x: list[float] = []
        self.y: list[float] = []

    def append(self, x: float, y: float) -> None:
        self.x.append(x)
        self.y.append(y)


class LiveMetricChart(QFrame):
    """A chart card whose controls and plot always stay inside its layout."""

    def __init__(self, title: str, y_label: str, colors: ThemeColors,
                 y_range: Optional[tuple[float, float]] = None,
                 x_label: str = "Packet",
                 compact: bool = False,
                 adaptive_y: bool = False,
                 initial_y_range: Optional[tuple[float, float]] = None,
                 x_window: Optional[float] = None,
                 parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self.setProperty("class", "ChartCard")
        self._compact = compact
        self.setMinimumSize(360 if compact else 520, 112 if compact else 230)
        self._colors = colors
        self._y_range = y_range
        self._adaptive_y = adaptive_y
        self._initial_y_range = initial_y_range
        self._x_window = float(x_window or CHART_WINDOW_SIZE)
        pg.setConfigOptions(antialias=True)

        root = QVBoxLayout(self)
        root.setContentsMargins(10 if compact else 12, 6 if compact else 9,
                                10 if compact else 12, 7 if compact else 10)
        root.setSpacing(3 if compact else 5)

        header = QHBoxLayout()
        title_label = QLabel(title)
        title_label.setProperty("class", "ChartTitle")
        self.auto_scroll_cb = QCheckBox("LIVE")
        self.auto_scroll_cb.setChecked(True)
        self.auto_scroll_cb.setToolTip(
            "Mantiene visible la ventana más reciente. Desactívalo para revisar el historial."
        )
        self.auto_scroll_cb.stateChanged.connect(self._on_scroll_toggled)
        header.addWidget(title_label)
        header.addStretch(1)
        header.addWidget(self.auto_scroll_cb)
        root.addLayout(header)

        self.plot_widget = pg.PlotWidget(background=colors.bg_card)
        self.plot_widget.setMinimumHeight(68 if compact else 178)
        self.plot_widget.setLabel("left", y_label, color=colors.text_secondary)
        self.plot_widget.setLabel("bottom", x_label, color=colors.text_secondary)
        self.plot_widget.showGrid(x=True, y=True, alpha=0.12)
        self.plot_widget.setMouseEnabled(x=True, y=False)
        self.plot_widget.setMenuEnabled(False)
        self.plot_widget.hideButtons()
        self.plot_widget.setClipToView(True)
        self.plot_widget.setDownsampling(auto=True, mode="peak")
        if y_range is not None:
            self.plot_widget.setYRange(*y_range, padding=0.03)
        elif initial_y_range is not None:
            self.plot_widget.setYRange(*initial_y_range, padding=0)
        root.addWidget(self.plot_widget, 1)

        self._series = {rid: RollingSeries() for rid in RECEIVER_IDS}
        # Index where the current flight begins. Earlier samples remain in
        # memory and are available through history(), but are not rendered.
        self._visible_from = {rid: 0 for rid in RECEIVER_IDS}
        self._curves: dict[str, pg.PlotDataItem] = {}
        self.plot_widget.addLegend(offset=(8, 8))
        names = {"A": "Receptor A", "B": "Receptor B"}
        for rid in RECEIVER_IDS:
            self._curves[rid] = self.plot_widget.plot([], [], name=names.get(rid, rid))
        self.set_colors(colors)

    def set_colors(self, colors: ThemeColors) -> None:
        self._colors = colors
        self.plot_widget.setBackground(colors.bg_card)
        for axis_name in ("left", "bottom"):
            axis = self.plot_widget.getAxis(axis_name)
            axis.setTextPen(colors.text_secondary)
            axis.setPen(colors.border)
        curve_colors = {"A": colors.receiver_a, "B": colors.receiver_b}
        for rid, curve in self._curves.items():
            curve.setPen(pg.mkPen(color=curve_colors.get(rid, colors.text_primary), width=2.2))
        legend = self.plot_widget.plotItem.legend
        if legend is not None:
            legend.setLabelTextColor(colors.text_secondary)
            legend.setBrush(pg.mkBrush(colors.bg_panel))
            legend.setPen(pg.mkPen(colors.border))

    def _on_scroll_toggled(self, state: int) -> None:
        if state == Qt.Unchecked.value:
            self.plot_widget.enableAutoRange(axis="x")

    def add_point(self, receiver_id: str, x: float, y: float) -> None:
        if receiver_id not in self._series:
            return
        series = self._series[receiver_id]
        series.append(x, y)
        visible_from = self._visible_from[receiver_id]
        self._curves[receiver_id].setData(
            series.x[visible_from:], series.y[visible_from:], skipFiniteCheck=True
        )
        if self._adaptive_y:
            self._update_adaptive_y_range()
        if self.auto_scroll_cb.isChecked():
            self.plot_widget.setXRange(
                max(0, x - self._x_window), max(self._x_window, x), padding=0
            )

    def _update_adaptive_y_range(self) -> None:
        """Keep low-altitude detail legible while expanding for the full flight."""
        visible_values = [
            value
            for rid, series in self._series.items()
            for value in series.y[self._visible_from[rid]:]
            if math.isfinite(value)
        ]
        if not visible_values:
            if self._initial_y_range is not None:
                self.plot_widget.setYRange(*self._initial_y_range, padding=0)
            return
        highest = max(visible_values)
        lowest = min(visible_values)
        # 15% headroom, rounded to clean 25 m divisions; never less than 100 m.
        upper = max(100.0, math.ceil(max(0.0, highest) * 1.15 / 25.0) * 25.0)
        lower = min(0.0, math.floor(lowest * 1.10 / 10.0) * 10.0)
        self.plot_widget.setYRange(lower, upper, padding=0)

    def set_receiver_visible(self, receiver_id: str, visible: bool) -> None:
        curve = self._curves.get(receiver_id)
        if curve is not None:
            curve.setVisible(visible)

    def start_new_flight(self) -> None:
        """Hide the previous flight without deleting its in-memory samples."""
        for rid, series in self._series.items():
            self._visible_from[rid] = len(series.x)
            self._curves[rid].setData([], [])
        self.plot_widget.setXRange(0, self._x_window, padding=0)
        if self._initial_y_range is not None:
            self.plot_widget.setYRange(*self._initial_y_range, padding=0)

    def history(self, receiver_id: str) -> tuple[list[float], list[float]]:
        """Return copies of the complete cross-flight history for analysis."""
        series = self._series[receiver_id]
        return list(series.x), list(series.y)

    def clear(self) -> None:
        """Compatibility alias: visually starts a flight and preserves data."""
        self.start_new_flight()
