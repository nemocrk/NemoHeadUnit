"""
Disconnected page with clock and accessory info.
"""

from PyQt6.QtCore import QTimer, Qt, QTime
from PyQt6.QtWidgets import QWidget, QLabel, QVBoxLayout, QHBoxLayout
from PyQt6.QtGui import QFont

from .base import UIModule


class DisconnectedModule(UIModule):
    def __init__(self, info_lines=None):
        super().__init__(name="Home", region="page")
        self._info_lines = info_lines or [
            "Nessun dispositivo collegato",
            "USB pronto — in attesa",
        ]

    def build(self, parent=None):
        root = QWidget(parent)
        layout = QVBoxLayout(root)
        layout.setContentsMargins(24, 24, 24, 24)
        layout.setSpacing(16)

        clock = QLabel("--:--")
        clock.setObjectName("HeroClock")
        clock.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter)
        clock.setFont(QFont("Montserrat", 56, QFont.Weight.Bold))

        info_box = QWidget()
        info_layout = QVBoxLayout(info_box)
        info_layout.setContentsMargins(0, 0, 0, 0)
        info_layout.setSpacing(8)

        for line in self._info_lines:
            label = QLabel(line)
            label.setObjectName("InfoLine")
            label.setFont(QFont("Montserrat", 14, QFont.Weight.Medium))
            info_layout.addWidget(label)

        layout.addWidget(clock)
        layout.addWidget(info_box)
        layout.addStretch(1)

        timer = QTimer(root)
        timer.setInterval(1000)

        def _tick():
            clock.setText(QTime.currentTime().toString("HH:mm"))

        timer.timeout.connect(_tick)
        timer.start()
        _tick()

        return root
