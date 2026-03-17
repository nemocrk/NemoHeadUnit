"""
Main window for NemoHeadUnit UI.
"""

import sys

from PyQt6.QtCore import Qt, QPropertyAnimation, QEasingCurve, QEvent, QTimer
from PyQt6.QtWidgets import (
    QApplication,
    QMainWindow,
    QWidget,
    QVBoxLayout,
    QStackedWidget,
    QGraphicsOpacityEffect,
    QLabel,
    QLineEdit,
)
from PyQt6.QtGui import QFont

from .modules.base import UIModule
from .modules.header import HeaderModule
from .modules.footer import FooterModule
from .modules.android_auto import AndroidAutoModule
from .modules.disconnected import DisconnectedModule
from .modules.config_module import ConfigModule
from .modules.keyboard import OnScreenKeyboard


def _app_font():
    font = QFont()
    font.setFamilies(["Montserrat", "Poppins", "Noto Sans", "DejaVu Sans"])
    font.setPointSize(10)
    return font


def _app_stylesheet():
    return """
    QMainWindow {
        background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                                    stop:0 #0c141a, stop:1 #1a2631);
        color: #f2f4f7;
    }
    #HeaderTitle { color: #cfe3f6; letter-spacing: 1px; }
    #HeaderClock { color: #ffffff; }
    #VideoFrame {
        background: #0b0f13;
        border: 1px solid #1f2a35;
        border-radius: 14px;
    }
    #VideoSurface { background: #000000; }
    #MutedHint { color: #6b8194; }
    #HeroClock { color: #ffffff; }
    #InfoLine { color: #9fb2c2; }
    #PageTitle { color: #eaf2f8; }
    #ConfigLabel { color: #a4b6c6; }
    #ConfigInput {
        background: #121a22;
        border: 1px solid #22303b;
        border-radius: 8px;
        padding: 6px 10px;
        color: #eaf2f8;
    }
    QPushButton#PrimaryActionButton {
        background: #1f2a35;
        border: 1px solid #2a3a46;
        border-radius: 12px;
        padding: 10px 14px;
        color: #f1f5f9;
    }
    QPushButton#PrimaryActionButton:hover { background: #263645; }
    QPushButton#TabButton {
        background: #0f151b;
        border: 1px solid #273542;
        border-radius: 12px;
        padding: 10px 14px;
        color: #9fb2c2;
    }
    QPushButton#TabButton[selected="true"] {
        background: #1d4b5a;
        border: 1px solid #2b6376;
        color: #e8f7ff;
    }
    #FooterVolumeLabel, #FooterVolumeValue { color: #cfe3f6; }
    QSlider#FooterVolumeSlider::groove:horizontal {
        border: 1px solid #22303b;
        height: 8px;
        background: #0f151b;
        border-radius: 4px;
    }
    QSlider#FooterVolumeSlider::handle:horizontal {
        background: #2b6376;
        border: 1px solid #2b6376;
        width: 16px;
        margin: -5px 0;
        border-radius: 8px;
    }
    #MicToast {
        background: #d23b3b;
        color: #ffffff;
        border-radius: 28px;
        font-weight: 700;
        letter-spacing: 1px;
    }
    #VolumeToast {
        background: #1d4b5a;
        color: #e8f7ff;
        border-radius: 16px;
        padding: 8px 12px;
        font-weight: 700;
    }
    """


def _supported_qt_keys():
    keys = []
    # Letters
    for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        qt_key = getattr(Qt.Key, f"Key_{ch}", None)
        if qt_key is not None:
            keys.append(int(qt_key))
    # Digits
    for d in range(10):
        qt_key = getattr(Qt.Key, f"Key_{d}", None)
        if qt_key is not None:
            keys.append(int(qt_key))
    # Navigation and controls
    for name in [
        "Key_Left", "Key_Right", "Key_Up", "Key_Down",
        "Key_Return", "Key_Enter", "Key_Backspace", "Key_Delete",
        "Key_Escape", "Key_Tab", "Key_Space",
        "Key_Home", "Key_End", "Key_PageUp", "Key_PageDown",
        "Key_MediaPlay", "Key_MediaPause", "Key_MediaPlayPause",
        "Key_MediaStop", "Key_MediaNext", "Key_MediaPrevious",
        "Key_VolumeUp", "Key_VolumeDown", "Key_VolumeMute",
        "Key_Back", "Key_Menu",
    ]:
        qt_key = getattr(Qt.Key, name, None)
        if qt_key is not None:
            keys.append(int(qt_key))
    return keys


class MainWindow(QMainWindow):
    def __init__(self, width=800, height=480, modules=None, on_action=None):
        super().__init__()
        self.setWindowTitle("NemoHeadUnit")
        self.setMinimumSize(width, height)
        self.resize(width, height)
        self.setFont(_app_font())
        self.setStyleSheet(_app_stylesheet())
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)

        self._modules = modules or self._default_modules()
        self._external_action_handler = on_action
        self._pages = []
        self._tab_buttons = []
        self._input_frontend = None
        self._input_enabled = False
        self._keyboard = None
        self._keyboard_hide_timer = QTimer(self)
        self._keyboard_hide_timer.setSingleShot(True)
        self._keyboard_hide_timer.timeout.connect(self._hide_keyboard)
        self._volume_getter = None
        self._volume_setter = None
        self._volume_toast_timer = QTimer(self)
        self._volume_toast_timer.setSingleShot(True)
        self._volume_toast_timer.timeout.connect(self._hide_volume_toast)

        root = QWidget(self)
        layout = QVBoxLayout(root)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        self._header_container = QWidget(root)
        self._content_stack = QStackedWidget(root)
        self._footer_container = QWidget(root)

        layout.addWidget(self._header_container, 0)
        layout.addWidget(self._content_stack, 1)
        layout.addWidget(self._footer_container, 0)
        self.setCentralWidget(root)

        self._build_modules()
        self._build_mic_toast()
        self._build_keyboard()
        self._build_volume_toast()
        self.installEventFilter(self)
        app = QApplication.instance()
        if app is not None:
            app.installEventFilter(self)

    def _default_modules(self):
        return [
            HeaderModule(),
            DisconnectedModule(),
            AndroidAutoModule(),
            ConfigModule(),
            FooterModule(),
        ]

    def _build_modules(self):
        header_modules = [m for m in self._modules if m.region == "header"]
        footer_modules = [m for m in self._modules if m.region == "footer"]
        page_modules = [m for m in self._modules if m.region == "page"]

        if header_modules:
            header = header_modules[0].build(self._header_container)
            header.setObjectName("Header")
            header_layout = QVBoxLayout(self._header_container)
            header_layout.setContentsMargins(0, 0, 0, 0)
            header_layout.addWidget(header)

        self._pages = []
        for mod in page_modules:
            page = mod.build(self._content_stack)
            self._content_stack.addWidget(page)
            self._pages.append({"label": mod.name, "widget": page, "module": mod})

        if footer_modules:
            footer = footer_modules[0]
            if hasattr(footer, "configure"):
                footer.configure(
                    self._pages,
                    self._on_navigate,
                    self._on_action,
                    on_volume_change=self._on_volume_change,
                    get_volume=self._get_volume,
                )
            footer_widget = footer.build(self._footer_container)
            footer_layout = QVBoxLayout(self._footer_container)
            footer_layout.setContentsMargins(0, 0, 0, 0)
            footer_layout.addWidget(footer_widget)
            self._tab_buttons = footer_widget.findChildren(QWidget, "TabButton")
            self._footer_module = footer

        if self._pages:
            self._content_stack.setCurrentIndex(0)

    def _build_keyboard(self):
        self._keyboard = OnScreenKeyboard(self)
        self._keyboard.setFixedHeight(260)
        self._keyboard.text_committed.connect(self._hide_keyboard)
        self._position_keyboard()

    def _build_volume_toast(self):
        toast = QLabel("Volume 50%", self)
        toast.setObjectName("VolumeToast")
        toast.setAlignment(Qt.AlignmentFlag.AlignCenter)
        toast.hide()
        toast.raise_()
        self._volume_toast = toast
        self._position_volume_toast()

    def _position_keyboard(self):
        if self._keyboard is None:
            return
        margin = 12
        width = self.width() - margin * 2
        height = self._keyboard.height()
        self._keyboard.setGeometry(margin, self.height() - height - margin, width, height)

    def _position_volume_toast(self):
        if not hasattr(self, "_volume_toast"):
            return
        margin = 18
        self._volume_toast.adjustSize()
        x = self.width() - self._volume_toast.width() - margin
        y = self._header_container.height() + margin + 72
        self._volume_toast.move(x, y)

    def _hide_keyboard(self):
        if self._keyboard is not None:
            self._keyboard.hide()

    def eventFilter(self, obj, event):
        if isinstance(obj, QLineEdit):
            if event.type() == QEvent.Type.FocusIn:
                if self._keyboard is not None:
                    self._keyboard.show_for(obj)
            elif event.type() == QEvent.Type.FocusOut:
                self._keyboard_hide_timer.start(250)
        if event.type() == QEvent.Type.MouseButtonPress and self._keyboard is not None:
            if not isinstance(obj, QLineEdit) and self._keyboard.isVisible():
                if obj is self._keyboard or self._keyboard.isAncestorOf(obj):
                    return super().eventFilter(obj, event)
                self._hide_keyboard()
        return super().eventFilter(obj, event)

    def set_volume_handler(self, get_volume, set_volume):
        self._volume_getter = get_volume
        self._volume_setter = set_volume
        if hasattr(self, "_footer_module") and self._footer_module is not None:
            try:
                self._footer_module.set_volume(self._get_volume())
            except Exception:
                pass

    def _get_volume(self) -> int:
        if self._volume_getter is None:
            return 50
        try:
            return int(self._volume_getter())
        except Exception:
            return 50

    def _on_volume_change(self, value: int):
        if self._volume_setter is not None:
            try:
                self._volume_setter(int(value))
            except Exception:
                pass
        self.show_volume_toast(int(value))

    def show_volume_toast(self, value: int, muted: bool = False):
        if not hasattr(self, "_volume_toast"):
            return
        label = f"Volume {value}%"
        if muted:
            label = "Volume Mute"
        self._volume_toast.setText(label)
        self._position_volume_toast()
        self._volume_toast.show()
        self._volume_toast.raise_()
        self._volume_toast_timer.start(1500)

    def _hide_volume_toast(self):
        if hasattr(self, "_volume_toast"):
            self._volume_toast.hide()

    def _on_navigate(self, index: int):
        if index < 0 or index >= len(self._pages):
            return
        page = self._pages[index]
        widget = page["widget"]
        module = page.get("module")
        if module is not None and hasattr(module, "reload"):
            module.reload()
        self._content_stack.setCurrentWidget(widget)
        self._animate_page(widget)
        self._update_tab_state(index)
        widget.update()
        widget.repaint()

    def _animate_page(self, widget):
        effect = QGraphicsOpacityEffect(widget)
        widget.setGraphicsEffect(effect)
        anim = QPropertyAnimation(effect, b"opacity", widget)
        anim.setDuration(220)
        anim.setStartValue(0.0)
        anim.setEndValue(1.0)
        anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        anim.start()
        widget._fade_anim = anim

    def _update_tab_state(self, selected_index: int):
        for i, btn in enumerate(self._footer_container.findChildren(QWidget, "TabButton")):
            btn.setProperty("selected", i == selected_index)
            btn.style().unpolish(btn)
            btn.style().polish(btn)

    def _on_action(self, action_id: str):
        if self._external_action_handler is not None:
            self._external_action_handler(action_id)
        else:
            print(f"[UI] Action: {action_id}")

    def set_action_handler(self, handler):
        self._external_action_handler = handler

    def get_supported_qt_keys(self):
        return _supported_qt_keys()

    def get_supported_action_ids(self):
        action_ids = []
        for mod in self._modules:
            if hasattr(mod, "get_supported_action_ids"):
                action_ids.extend(mod.get_supported_action_ids())
        return action_ids

    def set_input_frontend(self, frontend):
        self._input_frontend = frontend
        video_widget = self.get_android_auto_video_widget()
        if video_widget is not None:
            if hasattr(video_widget, "set_input_frontend"):
                video_widget.set_input_frontend(frontend)
            if hasattr(video_widget, "set_input_enabled"):
                video_widget.set_input_enabled(self._input_enabled)
        overlay = self.get_android_auto_input_overlay()
        if overlay is not None:
            overlay.set_input_frontend(frontend)
            overlay.set_input_enabled(self._input_enabled)

    def set_input_enabled(self, enabled: bool):
        self._input_enabled = bool(enabled)
        video_widget = self.get_android_auto_video_widget()
        if video_widget is not None:
            if hasattr(video_widget, "set_input_enabled"):
                video_widget.set_input_enabled(self._input_enabled)
        overlay = self.get_android_auto_input_overlay()
        if overlay is not None:
            overlay.set_input_enabled(self._input_enabled)

    def _build_mic_toast(self):
        toast = QLabel("MIC", self)
        toast.setObjectName("MicToast")
        toast.setFixedSize(56, 56)
        toast.setAlignment(Qt.AlignmentFlag.AlignCenter)
        toast.hide()
        toast.raise_()
        self._mic_toast = toast
        self._position_mic_toast()

    def _position_mic_toast(self):
        if not hasattr(self, "_mic_toast"):
            return
        margin = 18
        x = self.width() - self._mic_toast.width() - margin
        y = self._header_container.height() + margin
        self._mic_toast.move(x, y)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._position_keyboard()
        self._position_mic_toast()
        self._position_volume_toast()

    def set_mic_active(self, active: bool):
        if not hasattr(self, "_mic_toast"):
            return
        if active:
            self._mic_toast.show()
            self._mic_toast.raise_()
        else:
            self._mic_toast.hide()

    def get_android_auto_video_widget(self):
        for page in self._pages:
            mod = page["module"]
            if isinstance(mod, AndroidAutoModule):
                return mod.get_video_widget()
        return None

    def get_android_auto_input_overlay(self):
        for page in self._pages:
            mod = page["module"]
            if isinstance(mod, AndroidAutoModule):
                if hasattr(mod, "get_input_overlay"):
                    return mod.get_input_overlay()
        return None

    def show_android_auto(self):
        for i, page in enumerate(self._pages):
            if isinstance(page["module"], AndroidAutoModule):
                self._on_navigate(i)
                return

    def show_disconnected(self):
        for i, page in enumerate(self._pages):
            if isinstance(page["module"], DisconnectedModule):
                self._on_navigate(i)
                return

    def keyPressEvent(self, event):
        if self._input_enabled and self._input_frontend is not None and not event.isAutoRepeat():
            try:
                self._input_frontend.send_key_down_qt(event.key())
                return
            except Exception:
                pass
        super().keyPressEvent(event)

    def keyReleaseEvent(self, event):
        if self._input_enabled and self._input_frontend is not None and not event.isAutoRepeat():
            try:
                self._input_frontend.send_key_up_qt(event.key())
                return
            except Exception:
                pass
        super().keyReleaseEvent(event)


def run_demo():
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    run_demo()
