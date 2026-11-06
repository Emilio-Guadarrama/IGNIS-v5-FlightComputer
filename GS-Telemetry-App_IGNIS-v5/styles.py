"""Offline QSS theme for the IGNIS single-screen launch dashboard."""

from __future__ import annotations

from config import ThemeColors


def build_stylesheet(c: ThemeColors) -> str:
    return f"""
    QWidget {{
        background-color: {c.bg_root};
        color: {c.text_primary};
        font-family: "Segoe UI", Arial, sans-serif;
        font-size: 12px;
    }}
    QMainWindow, QDialog {{ background-color: {c.bg_root}; }}
    QLabel {{ background: transparent; }}

    #TopBar {{
        background-color: {c.bg_panel};
        border-bottom: 1px solid {c.border};
    }}
    #MissionTitle {{
        color: {c.text_primary};
        font-size: 23px;
        font-weight: 800;
        letter-spacing: 2px;
    }}
    QLabel.HeaderSubtitle {{
        color: {c.text_muted};
        font-size: 10px;
        font-weight: 700;
        letter-spacing: 1px;
    }}
    QLabel.HeaderStatus {{
        color: {c.text_primary};
        font-size: 11px;
        font-weight: 800;
        letter-spacing: 1px;
    }}
    #MissionTimer {{
        color: {c.receiver_a};
        font-family: "Consolas", monospace;
        font-size: 20px;
        font-weight: 700;
        padding: 0 10px;
    }}
    #FlightStateBadge {{
        color: {c.receiver_b};
        background-color: {c.accent_soft};
        border: 1px solid {c.accent};
        border-radius: 10px;
        padding: 5px 12px;
        font-family: "Consolas", monospace;
        font-weight: 700;
    }}
    #TopBarLED {{
        border-radius: 6px;
        background-color: #FF1744;
        border: 2px solid #7A0B24;
    }}
    #TopBarLED[state="online"] {{ background-color: #00E676; border-color: #007D40; }}
    #TopBarLED[state="warning"] {{ background-color: #FFEA00; border-color: #7D7300; }}
    #TopBarLED[state="offline"] {{ background-color: #FF1744; border-color: #7A0B24; }}

    #ConnectionBanner {{
        background-color: {c.bad_bg};
        border-bottom: 1px solid {c.bad};
    }}
    #ConnectionBanner[state="online"] {{
        background-color: {c.good_bg};
        border-bottom-color: {c.good};
    }}
    #ConnectionBanner[state="warning"] {{
        background-color: {c.warn_bg};
        border-bottom-color: {c.warn};
    }}
    #ConnectionBannerText {{
        color: {c.text_primary};
        font-size: 10px;
        font-weight: 800;
        letter-spacing: 1px;
    }}

    QFrame.ReceiverCard {{
        background-color: {c.bg_card};
        border: 1px solid {c.border};
        border-radius: 10px;
    }}
    QLabel.ReceiverName {{
        color: {c.text_primary};
        font-size: 13px;
        font-weight: 800;
    }}
    #ReceiverLED {{
        background-color: #FF1744;
        border: 2px solid #7A0B24;
        border-radius: 6px;
    }}
    #ReceiverLED[state="online"] {{ background-color: #00E676; border-color: #007D40; }}
    #ReceiverLED[state="stale"] {{ background-color: #FFEA00; border-color: #7D7300; }}
    #ReceiverLED[state="offline"] {{ background-color: #FF1744; border-color: #7A0B24; }}
    QLabel.StatusPill {{
        border-radius: 9px;
        padding: 3px 9px;
        font-size: 9px;
        font-weight: 900;
        letter-spacing: 1px;
    }}
    QLabel.StatusPill[state="online"] {{ color: #001C0E; background-color: #00E676; }}
    QLabel.StatusPill[state="stale"] {{ color: #1C1900; background-color: #FFEA00; }}
    QLabel.StatusPill[state="offline"] {{ color: #FFFFFF; background-color: #FF1744; }}
    QLabel.DeviceInfo {{ color: {c.text_secondary}; font-size: 10px; }}
    QLabel.VerificationUnknown {{ color: {c.text_muted}; font-size: 9px; font-weight: 700; }}
    QLabel.VerificationGood {{ color: #00E676; font-size: 9px; font-weight: 800; }}
    QLabel.MicroLabel {{ color: {c.text_muted}; font-size: 9px; font-weight: 700; }}
    QLabel.MicroValue {{ color: {c.text_primary}; font-family: "Consolas", monospace; font-size: 12px; font-weight: 700; }}
    QLabel.MicroUnit {{ color: {c.text_muted}; font-size: 9px; }}

    QFrame.ChartCard {{
        background-color: {c.bg_card};
        border: 1px solid {c.border};
        border-radius: 10px;
    }}
    QLabel.ChartTitle {{ color: {c.text_primary}; font-size: 11px; font-weight: 800; letter-spacing: 1px; }}
    QCheckBox {{ color: {c.receiver_a}; font-size: 9px; font-weight: 800; spacing: 5px; }}
    QCheckBox::indicator {{ width: 12px; height: 12px; border: 1px solid {c.receiver_a}; border-radius: 6px; }}
    QCheckBox::indicator:checked {{ background-color: {c.receiver_a}; border-color: {c.receiver_a}; }}

    QPushButton {{
        color: {c.text_primary};
        background-color: {c.bg_card};
        border: 1px solid {c.border};
        border-radius: 7px;
        padding: 6px 11px;
        font-size: 10px;
        font-weight: 800;
        letter-spacing: 0.5px;
    }}
    QPushButton:hover {{ background-color: {c.bg_card_hover}; border-color: {c.receiver_a}; }}
    QPushButton:pressed {{ background-color: {c.info_bg}; }}
    QPushButton:disabled {{ color: {c.text_muted}; background-color: {c.bg_panel}; border-color: {c.border}; }}
    #PrimaryButton {{ background-color: {c.accent}; border-color: #FF9100; color: #FFFFFF; }}
    #PrimaryButton:hover {{ background-color: #FF6A00; }}
    #PortButton {{ color: {c.receiver_a}; border-color: {c.receiver_a}; min-width: 76px; }}
    #PortArrowButton {{
        color: {c.receiver_a};
        border-color: {c.receiver_a};
        font-size: 17px;
        padding: 2px;
    }}
    #IconButton {{ color: {c.receiver_a}; font-size: 16px; padding: 2px; }}
    #ThemeButton {{ color: {c.receiver_a}; font-size: 17px; padding: 3px; }}
    QPushButton.PortChoice {{
        text-align: left;
        color: {c.text_primary};
        background-color: {c.bg_card};
        border-left: 3px solid {c.receiver_a};
        padding: 8px 12px;
    }}
    QPushButton.PortChoice:hover {{ background-color: {c.info_bg}; border-color: #FF9100; }}

    #FlightStrip {{
        background-color: {c.bg_panel};
        border: 1px solid {c.border};
        border-radius: 8px;
    }}
    QLabel.FlightStripValue {{
        color: #FFEA00;
        font-family: "Consolas", monospace;
        font-size: 10px;
        font-weight: 800;
    }}
    QLabel.FlightStripValue[state="good"] {{ color: #00E676; }}
    QLabel.FlightStripValue[state="bad"] {{ color: #FF1744; }}
    QLabel.LastEvent {{ color: {c.text_secondary}; font-size: 10px; font-weight: 700; }}
    QLabel.StorageStatus {{
        color: {c.text_muted};
        background-color: {c.bg_card};
        border: 1px solid {c.border};
        border-radius: 8px;
        padding: 4px 9px;
        margin-left: 6px;
        font-family: "Consolas", monospace;
        font-size: 9px;
        font-weight: 900;
    }}
    QLabel.StorageStatus[state="writing"] {{ color: {c.good}; border-color: {c.good}; background-color: {c.good_bg}; }}
    QLabel.StorageStatus[state="ok"] {{ color: {c.receiver_a}; border-color: {c.receiver_a}; background-color: {c.info_bg}; }}
    QLabel.StorageStatus[state="error"] {{ color: {c.bad}; border-color: {c.bad}; background-color: {c.bad_bg}; }}
    QLabel.StorageStatus[state="stale"] {{ color: {c.warn}; border-color: {c.warn}; background-color: {c.warn_bg}; }}
    QLabel.StorageStatus[state="no_data"] {{ color: {c.text_muted}; border-color: {c.border}; }}

    QLabel.DialogTitle {{ color: {c.text_primary}; font-size: 15px; font-weight: 800; letter-spacing: 1px; }}
    QLabel.MutedText {{ color: {c.text_muted}; }}
    QTableWidget {{
        background-color: {c.bg_panel};
        alternate-background-color: {c.bg_card};
        border: 1px solid {c.border};
        selection-background-color: {c.info_bg};
        gridline-color: {c.border};
    }}
    QTableWidget::item {{ font-family: "Consolas", monospace; font-size: 11px; padding: 4px; }}
    QHeaderView::section {{
        color: {c.text_secondary};
        background-color: {c.bg_card};
        border: none;
        border-bottom: 1px solid {c.border};
        padding: 6px;
        font-size: 9px;
        font-weight: 800;
    }}
    QStatusBar {{ color: {c.text_muted}; background-color: {c.bg_panel}; border-top: 1px solid {c.border}; font-size: 10px; }}
    QToolTip {{ color: {c.text_primary}; background-color: {c.bg_card}; border: 1px solid {c.receiver_a}; padding: 5px; }}

    QScrollBar:vertical {{ background: {c.bg_panel}; width: 9px; }}
    QScrollBar::handle:vertical {{ background: {c.border}; border-radius: 4px; min-height: 24px; }}
    QScrollBar::handle:vertical:hover {{ background: {c.receiver_a}; }}
    QScrollBar:horizontal {{ background: {c.bg_panel}; height: 9px; }}
    QScrollBar::handle:horizontal {{ background: {c.border}; border-radius: 4px; min-width: 24px; }}
    QScrollBar::handle:horizontal:hover {{ background: {c.receiver_a}; }}
    QScrollBar::add-line, QScrollBar::sub-line {{ width: 0; height: 0; }}
    """
