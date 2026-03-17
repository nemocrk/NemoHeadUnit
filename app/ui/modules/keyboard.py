"""
On-screen keyboard for tablet input.
"""

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtWidgets import QWidget, QPushButton, QGridLayout, QHBoxLayout, QVBoxLayout, QLabel
from PyQt6.QtGui import QFont


class OnScreenKeyboard(QWidget):
    text_committed = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self._target = None
        self.setObjectName("OnScreenKeyboard")
        self.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self._build_ui()
        self.hide()

    def _build_ui(self):
        root = QVBoxLayout(self)
        root.setContentsMargins(12, 12, 12, 12)
        root.setSpacing(8)

        title = QLabel("Tastiera")
        title.setFont(QFont("Montserrat", 12, QFont.Weight.DemiBold))
        title.setStyleSheet("color: #d8e3ec;")
        root.addWidget(title)

        grid = QGridLayout()
        grid.setHorizontalSpacing(6)
        grid.setVerticalSpacing(6)

        rows = [
            list("1234567890"),
            list("QWERTYUIOP"),
            list("ASDFGHJKL"),
            list("ZXCVBNM"),
        ]
        for r, row in enumerate(rows):
            for c, key in enumerate(row):
                grid.addWidget(self._make_key(key), r, c)

        root.addLayout(grid)

        bottom = QHBoxLayout()
        bottom.setSpacing(8)
        bottom.addWidget(self._make_key("SPACE", text="Space", width=3))
        bottom.addWidget(self._make_key("BACKSPACE", text="⌫"))
        bottom.addWidget(self._make_key("CLEAR", text="Clear"))
        bottom.addWidget(self._make_key("ENTER", text="Enter"))
        bottom.addWidget(self._make_key("HIDE", text="Hide"))
        root.addLayout(bottom)

        self.setStyleSheet("""
            QWidget#OnScreenKeyboard {
                background: #0b0f13;
                border: 1px solid #22303b;
                border-radius: 14px;
            }
            QPushButton#KeyButton {
                background: #121a22;
                color: #eaf2f8;
                border: 1px solid #22303b;
                border-radius: 10px;
                padding: 8px 10px;
                font-family: "Montserrat";
                font-size: 13px;
            }
            QPushButton#KeyButton:hover {
                background: #1f2a35;
            }
        """)

    def _make_key(self, key: str, text: str | None = None, width: int = 1) -> QPushButton:
        btn = QPushButton(text or key)
        btn.setObjectName("KeyButton")
        btn.setFont(QFont("Montserrat", 11, QFont.Weight.Medium))
        btn.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        btn.setProperty("key", key)
        btn.clicked.connect(self._on_key)
        if width > 1:
            btn.setMinimumWidth(70 * width)
        return btn

    def _on_key(self):
        if self._target is None:
            return
        key = self.sender().property("key")
        if key == "SPACE":
            self._target.insert(" ")
        elif key == "BACKSPACE":
            self._target.backspace()
        elif key == "CLEAR":
            self._target.clear()
        elif key == "ENTER":
            self.text_committed.emit()
            self.hide()
        elif key == "HIDE":
            self.hide()
        else:
            self._target.insert(key)

    def show_for(self, target):
        self._target = target
        self.show()
