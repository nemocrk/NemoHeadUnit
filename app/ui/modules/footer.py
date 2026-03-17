"""
Footer module with primary buttons and tab navigation.
"""

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QWidget, QPushButton, QHBoxLayout, QLabel, QSlider
from PyQt6.QtGui import QFont

from .base import UIModule


class FooterModule(UIModule):
    def __init__(self, primary_actions=None):
        super().__init__(name="Footer", region="footer")
        self._primary_actions = primary_actions or [
            ("VOL+", "volume_up"),
            ("VOL-", "volume_down"),
            ("ASSIST", "assistant"),
            ("MUTE", "mute"),
        ]
        self._pages = []
        self._on_navigate = None
        self._on_action = None
        self._on_volume_change = None
        self._get_volume = None
        self._volume_label = None
        self._volume_slider = None

    def configure(self, pages, on_navigate, on_action=None, on_volume_change=None, get_volume=None):
        self._pages = pages
        self._on_navigate = on_navigate
        self._on_action = on_action
        self._on_volume_change = on_volume_change
        self._get_volume = get_volume

    def build(self, parent=None):
        root = QWidget(parent)
        layout = QHBoxLayout(root)
        layout.setContentsMargins(24, 12, 24, 16)
        layout.setSpacing(12)

        # Primary actions (left)
        for label, action_id in self._primary_actions:
            btn = QPushButton(label)
            btn.setObjectName("PrimaryActionButton")
            btn.setFont(QFont("Montserrat", 11, QFont.Weight.DemiBold))
            btn.clicked.connect(lambda _=False, a=action_id: self._emit_action(a))
            layout.addWidget(btn)

        # Volume bar (center)
        vol_wrap = QWidget(root)
        vol_layout = QHBoxLayout(vol_wrap)
        vol_layout.setContentsMargins(6, 0, 6, 0)
        vol_layout.setSpacing(8)

        vol_label = QLabel("VOL")
        vol_label.setObjectName("FooterVolumeLabel")
        vol_label.setFont(QFont("Montserrat", 10, QFont.Weight.DemiBold))

        slider = QSlider(Qt.Orientation.Horizontal)
        slider.setObjectName("FooterVolumeSlider")
        slider.setMinimum(0)
        slider.setMaximum(100)
        slider.setSingleStep(5)
        slider.setPageStep(5)
        slider.setFixedWidth(220)
        slider.setValue(50)
        slider.valueChanged.connect(self._emit_volume)

        value_label = QLabel("50%")
        value_label.setObjectName("FooterVolumeValue")
        value_label.setFont(QFont("Montserrat", 10, QFont.Weight.DemiBold))

        vol_layout.addWidget(vol_label)
        vol_layout.addWidget(slider)
        vol_layout.addWidget(value_label)
        self._volume_slider = slider
        self._volume_label = value_label
        if self._get_volume is not None:
            try:
                self.set_volume(self._get_volume())
            except Exception:
                pass
        layout.addWidget(vol_wrap)

        layout.addStretch(1)

        # Page tabs (right)
        for idx, page in enumerate(self._pages):
            btn = QPushButton(page["label"])
            btn.setObjectName("TabButton")
            btn.setFont(QFont("Montserrat", 11, QFont.Weight.DemiBold))
            btn.setProperty("selected", idx == 0)
            btn.clicked.connect(lambda _=False, i=idx: self._emit_navigate(i))
            layout.addWidget(btn)

        return root

    def get_supported_action_ids(self):
        return [action_id for _, action_id in self._primary_actions]

    def _emit_action(self, action_id: str):
        if self._on_action is not None:
            self._on_action(action_id)

    def _emit_navigate(self, index: int):
        if self._on_navigate is not None:
            self._on_navigate(index)

    def _emit_volume(self, value: int):
        if self._on_volume_change is not None:
            self._on_volume_change(value)
        if self._volume_label is not None:
            self._volume_label.setText(f"{value}%")

    def set_volume(self, value: int):
        if self._volume_slider is None or self._volume_label is None:
            return
        value = max(0, min(100, int(value)))
        self._volume_slider.blockSignals(True)
        self._volume_slider.setValue(value)
        self._volume_slider.blockSignals(False)
        self._volume_label.setText(f"{value}%")
