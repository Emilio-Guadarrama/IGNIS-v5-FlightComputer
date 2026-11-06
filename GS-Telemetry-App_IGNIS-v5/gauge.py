"""Minimal Dragon/Grafana-inspired gauges rendered entirely with Qt."""

from __future__ import annotations

from typing import Optional

from PySide6.QtCore import Property, QEasingCurve, QPropertyAnimation, QRectF, Qt
from PySide6.QtGui import QColor, QFont, QPainter, QPen
from PySide6.QtWidgets import QSizePolicy, QWidget

from config import BATTERY_EMPTY_V, BATTERY_FULL_V, BATTERY_LOW_V, BATTERY_WARN_V, ThemeColors


class _AnimatedValueWidget(QWidget):
    """Shared non-blocking interpolation for live instruments."""

    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._display_value = 0.0
        self._has_value = False
        self._animation = QPropertyAnimation(self, b"displayValue", self)
        self._animation.setDuration(160)
        self._animation.setEasingCurve(QEasingCurve.Type.OutCubic)

    def _get_display_value(self) -> float:
        return self._display_value

    def _set_display_value(self, value: float) -> None:
        self._display_value = float(value)
        self.update()

    displayValue = Property(float, _get_display_value, _set_display_value)

    def set_value(self, value: Optional[float], animated: bool = True) -> None:
        self._animation.stop()
        if value is None:
            self._has_value = False
            self._display_value = 0.0
            self.update()
            return
        self._has_value = True
        target = float(value)
        if not animated:
            self._set_display_value(target)
            return
        self._animation.setStartValue(self._display_value)
        self._animation.setEndValue(target)
        self._animation.start()


class ArcGauge(_AnimatedValueWidget):
    """Thin semicircular progress arc with a prominent digital value."""

    def __init__(self, title: str, unit: str, minimum: float, maximum: float,
                 neon_color: str, colors: ThemeColors,
                 parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self.title = title
        self.unit = unit
        self.minimum = minimum
        self.maximum = maximum
        self.neon_color = neon_color
        self.colors = colors
        self.setMinimumSize(250, 155)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

    def set_colors(self, colors: ThemeColors) -> None:
        self.colors = colors
        self.update()

    def paintEvent(self, event) -> None:  # noqa: N802
        del event
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        card = QRectF(1, 1, self.width() - 2, self.height() - 2)
        painter.setPen(QPen(QColor(self.colors.border), 1))
        painter.setBrush(QColor(self.colors.bg_card))
        painter.drawRoundedRect(card, 12, 12)

        painter.setPen(QColor(self.colors.text_secondary))
        painter.setFont(QFont("Segoe UI", 9, QFont.Weight.DemiBold))
        painter.drawText(QRectF(14, 10, self.width() - 28, 20),
                         Qt.AlignmentFlag.AlignCenter, self.title.upper())

        diameter = min(self.width() - 44, (self.height() - 42) * 1.65)
        arc_rect = QRectF((self.width() - diameter) / 2, 35, diameter, diameter)
        arc_width = max(7.0, min(10.0, self.width() / 36))
        painter.setPen(QPen(QColor(self.colors.border), arc_width,
                            Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawArc(arc_rect, 180 * 16, -180 * 16)

        fraction = 0.0
        if self._has_value:
            span = max(0.0001, self.maximum - self.minimum)
            fraction = max(0.0, min(1.0, (self._display_value - self.minimum) / span))
        progress_color = self.colors.bad if fraction >= 0.95 else self.neon_color
        painter.setPen(QPen(QColor(progress_color), arc_width,
                            Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.drawArc(arc_rect, 180 * 16, int(-180 * fraction * 16))

        value_text = "—" if not self._has_value else f"{self._display_value:,.1f}"
        painter.setPen(QColor(self.colors.text_primary))
        painter.setFont(QFont("Consolas", 24, QFont.Weight.Bold))
        painter.drawText(QRectF(8, self.height() * 0.48, self.width() - 16, 40),
                         Qt.AlignmentFlag.AlignCenter, value_text)
        painter.setPen(QColor(self.colors.text_muted))
        painter.setFont(QFont("Segoe UI", 10, QFont.Weight.Medium))
        painter.drawText(QRectF(8, self.height() * 0.72, self.width() - 16, 24),
                         Qt.AlignmentFlag.AlignCenter, self.unit)


class BatteryIndicator(_AnimatedValueWidget):
    """Physical battery silhouette with voltage-aware animated fill."""

    def __init__(self, colors: ThemeColors, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self.colors = colors
        self.setMinimumSize(250, 155)
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)

    def set_colors(self, colors: ThemeColors) -> None:
        self.colors = colors
        self.update()

    def _fill_color(self, fraction: float) -> str:
        if not self._has_value:
            return self.colors.text_muted
        if self._display_value <= BATTERY_LOW_V:
            return "#FF1744"
        if self._display_value <= BATTERY_WARN_V:
            return "#FF9100"
        if fraction < 0.65:
            return "#FFEA00"
        return "#00E676"

    def paintEvent(self, event) -> None:  # noqa: N802
        del event
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)

        card = QRectF(1, 1, self.width() - 2, self.height() - 2)
        painter.setPen(QPen(QColor(self.colors.border), 1))
        painter.setBrush(QColor(self.colors.bg_card))
        painter.drawRoundedRect(card, 12, 12)

        painter.setPen(QColor(self.colors.text_secondary))
        painter.setFont(QFont("Segoe UI", 9, QFont.Weight.DemiBold))
        painter.drawText(QRectF(14, 10, self.width() - 28, 20),
                         Qt.AlignmentFlag.AlignCenter, "BATTERY VOLTAGE")

        body = QRectF(28, 48, self.width() - 68, 58)
        terminal = QRectF(body.right() + 4, body.center().y() - 11, 10, 22)
        painter.setPen(QPen(QColor(self.colors.text_secondary), 3,
                            Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        painter.setBrush(Qt.BrushStyle.NoBrush)
        painter.drawRoundedRect(body, 8, 8)
        painter.fillRect(terminal, QColor(self.colors.text_secondary))

        fraction = 0.0
        if self._has_value:
            span = BATTERY_FULL_V - BATTERY_EMPTY_V
            fraction = max(0.0, min(1.0, (self._display_value - BATTERY_EMPTY_V) / span))
        inner = body.adjusted(7, 7, -7, -7)
        fill = QRectF(inner.left(), inner.top(), inner.width() * fraction, inner.height())
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(self._fill_color(fraction)))
        painter.drawRoundedRect(fill, 4, 4)

        voltage = "—" if not self._has_value else f"{self._display_value:.2f} V"
        painter.setPen(QColor(self.colors.text_primary))
        painter.setFont(QFont("Consolas", 17, QFont.Weight.Bold))
        painter.drawText(body, Qt.AlignmentFlag.AlignCenter, voltage)

        percentage = "SIN DATOS" if not self._has_value else f"{fraction * 100:.0f}% estimado"
        painter.setPen(QColor(self._fill_color(fraction)))
        painter.setFont(QFont("Segoe UI", 9, QFont.Weight.DemiBold))
        painter.drawText(QRectF(10, 116, self.width() - 20, 22),
                         Qt.AlignmentFlag.AlignCenter, percentage)
