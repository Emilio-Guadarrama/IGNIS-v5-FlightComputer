#!/usr/bin/env python3
"""
main.py
=======
Entry point for the IGNIS v4 Ground Control Station.

Run with:
    python main.py

See README.md for setup / dependency instructions.
"""

from __future__ import annotations

import sys

from PySide6.QtWidgets import QApplication

from gui import MainWindow


def main() -> int:
    app = QApplication(sys.argv)
    app.setStyle("Fusion")  # consistent offline rendering on Windows/macOS/Linux
    app.setApplicationName("IGNIS Ground Control Station")
    app.setOrganizationName("IgnitiaRocketLab")

    window = MainWindow()
    window.show()

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
