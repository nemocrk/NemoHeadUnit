"""
Header module with clock.
"""

from PyQt6.QtCore import QTimer, Qt, QTime
from PyQt6.QtWidgets import QWidget, QLabel, QHBoxLayout
from PyQt6.QtGui import QFont

from .base import UIModule


class HeaderModule(UIModule):
    def __init__(self, title: str = "NemoHeadUnit"):
        super().__init__(name="Header", region="header")
        self._title = title

    def build(self, parent=None):
        root = QWidget(parent)
        layout = QHBoxLayout(root)
        layout.setContentsMargins(24, 12, 24, 12)
        layout.setSpacing(12)

        title = QLabel(self._title)
        title.setObjectName("HeaderTitle")
        title.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        title.setFont(QFont("Montserrat", 16, QFont.Weight.DemiBold))

        clock = QLabel("--:--")
        clock.setObjectName("HeaderClock")
        clock.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        clock.setFont(QFont("Montserrat", 18, QFont.Weight.Bold))

        layout.addWidget(title, 1)
        layout.addWidget(clock, 0)

        timer = QTimer(root)
        timer.setInterval(1000)

        def _tick():
            clock.setText(QTime.currentTime().toString("HH:mm"))

        timer.timeout.connect(_tick)
        timer.start()
        _tick()

        return root
