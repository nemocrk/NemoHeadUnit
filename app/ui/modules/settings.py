"""
Settings page (instance configuration).
"""

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QWidget, QLabel, QVBoxLayout, QGridLayout, QLineEdit, QComboBox
from PyQt6.QtGui import QFont

from .base import UIModule


class SettingsModule(UIModule):
    def __init__(self, instance_name="Nemo", mode="USB", bt_address=""):
        super().__init__(name="Config", region="page")
        self._instance_name = instance_name
        self._mode = mode
        self._bt_address = bt_address

    def build(self, parent=None):
        root = QWidget(parent)
        layout = QVBoxLayout(root)
        layout.setContentsMargins(24, 24, 24, 24)
        layout.setSpacing(16)

        title = QLabel("Configurazione")
        title.setObjectName("PageTitle")
        title.setFont(QFont("Montserrat", 20, QFont.Weight.DemiBold))

        form = QWidget()
        grid = QGridLayout(form)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setHorizontalSpacing(12)
        grid.setVerticalSpacing(12)

        name_label = QLabel("Nome istanza")
        name_input = QLineEdit()
        name_input.setText(self._instance_name)
        name_input.setObjectName("ConfigInput")

        mode_label = QLabel("Modalità")
        mode_input = QComboBox()
        mode_input.addItems(["USB", "WiFi"])
        if self._mode in ["USB", "WiFi"]:
            mode_input.setCurrentText(self._mode)
        mode_input.setObjectName("ConfigInput")

        bt_label = QLabel("BT Address")
        bt_input = QLineEdit()
        bt_input.setText(self._bt_address)
        bt_input.setPlaceholderText("00:11:22:33:44:55")
        bt_input.setObjectName("ConfigInput")

        for lbl in [name_label, mode_label, bt_label]:
            lbl.setObjectName("ConfigLabel")
            lbl.setFont(QFont("Montserrat", 12, QFont.Weight.Medium))

        grid.addWidget(name_label, 0, 0)
        grid.addWidget(name_input, 0, 1)
        grid.addWidget(mode_label, 1, 0)
        grid.addWidget(mode_input, 1, 1)
        grid.addWidget(bt_label, 2, 0)
        grid.addWidget(bt_input, 2, 1)

        layout.addWidget(title)
        layout.addWidget(form)
        layout.addStretch(1)

        return root
