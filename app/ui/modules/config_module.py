"""Configuration UI module.

Provides a tabbed UI for editing application configuration and persists it on disk.
"""

from __future__ import annotations
from enum import Enum

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import (
    QWidget,
    QVBoxLayout,
    QHBoxLayout,
    QFormLayout,
    QTabWidget,
    QScrollArea,
    QLabel,
    QLineEdit,
    QSlider,
    QPushButton,
    QComboBox,
    QCheckBox,
    QGroupBox,
    QScroller,
)
from PyQt6.QtGui import QFont

from .base import UIModule
from handlers.control.control_modules import proto
from app.config import (
    get_config,
    save_config,
    system_volume_set,
    system_volume_get,
    system_volume_change,
    system_volume_toggle_mute,
    AudioStreamConfig,
    VideoConfig,
    VideoCodec,
    VideoResolution,
    VideoFrameRate,
    AudioCodec,
    LogLevel,
    audio_codec_to_proto,
    video_codec_to_proto,
    video_resolution_to_proto,
    video_frame_rate_to_proto,
    proto_to_audio_codec,
    proto_to_video_codec,
    proto_to_video_resolution,
    proto_to_video_frame_rate,
)


# Mappa dei canali standard AASDK per la UI
KNOWN_CHANNELS = {
    1: "Sensor",
    3: "Video",
    4: "Media Audio",
    5: "Guidance Audio",
    6: "System Audio",
    8: "Input",
    9: "Microphone",
    10: "Bluetooth",
    11: "Phone Status",
    12: "Navigation",
    13: "Media Playback",
    14: "Media Browser",
    15: "Vendor Extension",
    16: "Notification",
    17: "WiFi Projection",
    18: "Radio",
}

def _styled_label(text: str) -> QLabel:
    lbl = QLabel(text)
    lbl.setObjectName("ConfigLabel")
    lbl.setFont(QFont("Montserrat", 12, QFont.Weight.Medium))
    return lbl

def _create_enum_combobox(enum_cls: type[Enum]) -> QComboBox:
    combo = QComboBox()
    combo.addItems([item.value for item in enum_cls])
    combo.setObjectName("ConfigInput")
    return combo


def _create_enum_combobox_from_keys(keys: list[str]) -> QComboBox:
    combo = QComboBox()
    combo.addItems(keys)
    combo.setObjectName("ConfigInput")
    return combo


class NumberControl(QWidget):
    def __init__(self, value: int, min_value: int, max_value: int, step: int = 1):
        super().__init__()
        self._min = min_value
        self._max = max_value
        self._step = max(1, step)

        self._slider = QSlider(Qt.Orientation.Horizontal)
        self._slider.setObjectName("ConfigSlider")
        self._slider.setMinimum(min_value)
        self._slider.setMaximum(max_value)
        self._slider.setSingleStep(self._step)
        self._slider.setPageStep(self._step)
        self._slider.setValue(value)
        self._slider.setFixedHeight(36)

        self._value_label = QLabel(str(value))
        self._value_label.setObjectName("ConfigValueLabel")
        self._value_label.setFont(QFont("Montserrat", 11, QFont.Weight.DemiBold))

        minus = QPushButton("-")
        minus.setObjectName("StepperButton")
        minus.setFixedSize(44, 44)
        minus.clicked.connect(lambda: self._bump(-self._step))
        plus = QPushButton("+")
        plus.setObjectName("StepperButton")
        plus.setFixedSize(44, 44)
        plus.clicked.connect(lambda: self._bump(self._step))

        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(8)
        layout.addWidget(minus)
        layout.addWidget(self._slider, 1)
        layout.addWidget(plus)
        layout.addWidget(self._value_label)

        self._slider.valueChanged.connect(self._on_change)

    def _bump(self, delta: int):
        self._slider.setValue(max(self._min, min(self._max, self._slider.value() + delta)))

    def _on_change(self, value: int):
        self._value_label.setText(str(value))

    def value(self) -> int:
        return int(self._slider.value())

    def set_value(self, value: int):
        self._slider.setValue(max(self._min, min(self._max, value)))

    def on_change(self, fn):
        self._slider.valueChanged.connect(fn)

class ConfigModule(UIModule):
    def __init__(self):
        super().__init__(name="Config", region="page")
        self._widgets: dict[str, dict[str, QWidget]] = {}

    def build(self, parent=None):
        root = QWidget(parent)
        root.setObjectName("ConfigRoot")
        root.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        # Local stylesheet to match main_window theme for Tabs and GroupBoxes
        root.setStyleSheet("""
            #ConfigRoot {
                background: #0b0f13;
            }
            QTabWidget::pane {
                border: 1px solid #22303b;
                background: #0b0f13;
                border-radius: 12px;
            }
            QTabBar::tab {
                background: #0f151b;
                color: #9fb2c2;
                padding: 10px 14px;
                border: 1px solid #273542;
                border-radius: 12px;
                margin-right: 6px;
            }
            QTabBar::tab:selected {
                background: #1d4b5a;
                border: 1px solid #2b6376;
                color: #e8f7ff;
            }
            QGroupBox {
                border: 1px solid #22303b;
                border-radius: 8px;
                margin-top: 24px;
                font-weight: bold;
                color: #eaf2f8;
                font-family: "Montserrat";
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                subcontrol-position: top left;
                padding: 0 5px;
                left: 10px;
            }
            QCheckBox {
                color: #eaf2f8;
                spacing: 8px;
                font-size: 14px;
                font-family: "Montserrat";
            }
            QCheckBox::indicator {
                width: 20px;
                height: 20px;
                background: #121a22;
                border: 1px solid #22303b;
                border-radius: 4px;
            }
            QCheckBox::indicator:checked {
                background: #2b6376;
                border-color: #2b6376;
            }
            /* Keep dropdowns cute with dark background + light text */
            QComboBox {
                background: #0f151b;
                color: #eaf2f8;
                border: 1px solid #22303b;
                padding: 4px 6px;
                border-radius: 6px;
            }
            QComboBox::drop-down {
                subcontrol-origin: padding;
                subcontrol-position: top right;
                width: 26px;
                border-left: 1px solid #22303b;
            }
            QComboBox QAbstractItemView {
                background: #0f151b;
                color: #eaf2f8;
                border: 1px solid #22303b;
                selection-background-color: #2b6376;
                selection-color: #f1f5f9;
            }
            QSlider::groove:horizontal {
                border: 1px solid #22303b;
                height: 12px;
                background: #0f151b;
                border-radius: 6px;
            }
            QSlider::handle:horizontal {
                background: #2b6376;
                border: 1px solid #2b6376;
                width: 28px;
                height: 28px;
                margin: -10px 0;
                border-radius: 14px;
            }
            QLabel#ConfigValueLabel {
                color: #eaf2f8;
                min-width: 52px;
            }
            QPushButton#StepperButton {
                background: #121a22;
                color: #eaf2f8;
                border: 1px solid #22303b;
                border-radius: 14px;
                padding: 10px 16px;
                min-width: 44px;
                min-height: 44px;
            }
            QScrollArea {
                background: #0b0f13;
                border: none;
            }
            QScrollArea QWidget {
                background: #0b0f13;
            }
            QScrollBar:vertical {
                background: transparent;
                width: 10px;
                margin: 6px 2px 6px 2px;
            }
            QScrollBar::handle:vertical {
                background: #2b6376;
                min-height: 24px;
                border-radius: 5px;
            }
            QScrollBar::add-line:vertical,
            QScrollBar::sub-line:vertical {
                height: 0px;
            }
        """)

        layout = QVBoxLayout(root)
        layout.setContentsMargins(24, 24, 24, 24)
        layout.setSpacing(12)

        title = QLabel("Configurazione")
        title.setObjectName("PageTitle")
        title.setFont(QFont("Montserrat", 20, QFont.Weight.DemiBold))
        layout.addWidget(title)

        tabs = QTabWidget(root)
        tabs.addTab(self._wrap_scroll(self._build_general_tab()), "General")
        tabs.addTab(self._wrap_scroll(self._build_audio_tab()), "Audio")
        tabs.addTab(self._wrap_scroll(self._build_video_tab()), "Video")
        tabs.addTab(self._wrap_scroll(self._build_services_tab()), "Services")
        tabs.addTab(self._wrap_scroll(self._build_logging_tab()), "Logging")
        layout.addWidget(tabs)

        btn_layout = QHBoxLayout()
        btn_layout.addStretch(1)
        save_btn = QPushButton("Save")
        save_btn.setObjectName("PrimaryActionButton")
        save_btn.clicked.connect(self.save)
        reload_btn = QPushButton("Reload")
        reload_btn.setObjectName("PrimaryActionButton")
        reload_btn.clicked.connect(self.reload)
        btn_layout.addWidget(reload_btn)
        btn_layout.addWidget(save_btn)
        layout.addLayout(btn_layout)

        self.reload()
        return root

    def _wrap_scroll(self, content: QWidget) -> QScrollArea:
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QScrollArea.Shape.NoFrame)
        scroll.setWidget(content)
        scroll.setObjectName("ConfigScroll")
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        scroll.setVerticalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAsNeeded)
        scroll.viewport().setAttribute(Qt.WidgetAttribute.WA_AcceptTouchEvents, True)
        scroll.viewport().setAutoFillBackground(True)
        scroll.viewport().setStyleSheet("background: #0b0f13;")
        content.setAttribute(Qt.WidgetAttribute.WA_StyledBackground, True)
        content.setStyleSheet("background: #0b0f13;")
        QScroller.grabGesture(scroll.viewport(), QScroller.ScrollerGestureType.LeftMouseButtonGesture)
        QScroller.grabGesture(scroll.viewport(), QScroller.ScrollerGestureType.TouchGesture)
        QScroller.grabGesture(content, QScroller.ScrollerGestureType.LeftMouseButtonGesture)
        QScroller.grabGesture(content, QScroller.ScrollerGestureType.TouchGesture)
        return scroll

    def _build_general_tab(self) -> QWidget:
        widget = QWidget()
        form = QFormLayout(widget)
        config = get_config()

        self._widgets["general"] = {}

        name_input = QLineEdit(config.ui.instance_name)
        name_input.setObjectName("ConfigInput")
        form.addRow(_styled_label("Instance name"), name_input)
        self._widgets["general"]["instance_name"] = name_input

        mode_input = QComboBox()
        mode_input.addItems(["USB", "WiFi"])
        mode_input.setCurrentText(config.ui.mode)
        mode_input.setObjectName("ConfigInput")
        form.addRow(_styled_label("Mode"), mode_input)
        self._widgets["general"]["mode"] = mode_input

        bt_input = QLineEdit(config.ui.bt_address)
        bt_input.setObjectName("ConfigInput")
        form.addRow(_styled_label("BT address"), bt_input)
        self._widgets["general"]["bt_address"] = bt_input

        return widget

    def _build_audio_tab(self) -> QWidget:
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(12)

        config = get_config()
        self._widgets["audio"] = {}
        self._widgets["audio_output"] = {}

        # Audio output / mixer configuration
        out_box = QGroupBox("Audio Output")
        out_layout = QFormLayout(out_box)

        sink_input = QComboBox()
        sink_input.addItems(["pulsesink", "alsasink", "autoaudiosink"])
        sink_input.setCurrentText(config.audio_output.sink)
        sink_input.setObjectName("ConfigInput")

        backend_input = QComboBox()
        backend_input.addItems(["pactl", "wpctl"])
        backend_input.setCurrentText(config.audio_output.volume_backend)
        backend_input.setObjectName("ConfigInput")

        buffer_ctrl = NumberControl(config.audio_output.buffer_ms, 20, 500, 10)
        max_queue_ctrl = NumberControl(config.audio_output.max_queue_frames, 4, 64, 1)
        mic_frame_ctrl = NumberControl(config.audio_output.mic_frame_ms, 0, 100, 5)
        mic_batch_ctrl = NumberControl(config.audio_output.mic_batch_ms, 20, 400, 20)
        step_ctrl = NumberControl(config.audio_output.volume_step, 1, 20, 1)

        vol_current, muted = system_volume_get()
        vol_percent = vol_current if vol_current is not None else config.audio_output.volume_percent
        volume_ctrl = NumberControl(vol_percent, 0, 100, 5)
        mute_btn = QPushButton("Toggle Mute")
        mute_btn.setObjectName("PrimaryActionButton")
        mute_btn.clicked.connect(system_volume_toggle_mute)

        device_input = QLineEdit(config.audio_output.volume_device)
        device_input.setObjectName("ConfigInput")

        def _apply_volume(value: int):
            system_volume_set(value)

        volume_ctrl.on_change(_apply_volume)

        out_layout.addRow(_styled_label("Sink"), sink_input)
        out_layout.addRow(_styled_label("Buffer (ms)"), buffer_ctrl)
        out_layout.addRow(_styled_label("Max buffer (frames)"), max_queue_ctrl)
        out_layout.addRow(_styled_label("Mic frame (ms)"), mic_frame_ctrl)
        out_layout.addRow(_styled_label("Mic batch (ms)"), mic_batch_ctrl)
        out_layout.addRow(_styled_label("Volume backend"), backend_input)
        out_layout.addRow(_styled_label("Volume step (%)"), step_ctrl)
        out_layout.addRow(_styled_label("Volume device (optional)"), device_input)
        out_layout.addRow(_styled_label("Volume"), volume_ctrl)
        out_layout.addRow(_styled_label("Mute"), mute_btn)

        self._widgets["audio_output"] = {
            "sink": sink_input,
            "buffer_ms": buffer_ctrl,
            "max_queue_frames": max_queue_ctrl,
            "mic_frame_ms": mic_frame_ctrl,
            "mic_batch_ms": mic_batch_ctrl,
            "volume_backend": backend_input,
            "volume_step": step_ctrl,
            "volume_device": device_input,
            "volume": volume_ctrl,
        }
        layout.addWidget(out_box)

        # Define known audio channels to display even if not yet in config
        audio_channels = [4, 5, 6, 9]  # Media, Guidance, System, Mic
        audio_keys = [
            k for k in proto.MediaCodecType_pb2.MediaCodecType.keys() if k.startswith("MEDIA_CODEC_AUDIO_")
        ]

        for ch_id in audio_channels:
            name = KNOWN_CHANNELS.get(ch_id, f"Channel {ch_id}")
            stream_cfg = config.service_discovery.audio_streams.get(ch_id, AudioStreamConfig())

            box = QGroupBox(f"{name} (ID: {ch_id})")
            box_layout = QFormLayout(box)

            codec = _create_enum_combobox_from_keys(audio_keys)
            codec.setCurrentText(audio_codec_to_proto(stream_cfg.codec))

            rate = NumberControl(stream_cfg.sample_rate, 8000, 96000, 1000)
            bits = NumberControl(stream_cfg.bits, 8, 32, 1)
            channels = NumberControl(stream_cfg.channels, 1, 8, 1)

            box_layout.addRow(_styled_label("Codec"), codec)
            box_layout.addRow(_styled_label("Sample rate"), rate)
            box_layout.addRow(_styled_label("Bits"), bits)
            box_layout.addRow(_styled_label("Channels"), channels)
            layout.addWidget(box)
            self._widgets["audio"][str(ch_id)] = {
                "codec": codec,
                "sample_rate": rate,
                "bits": bits,
                "channels": channels,
            }

        layout.addStretch(1)
        return widget

    def _build_video_tab(self) -> QWidget:
        widget = QWidget()
        form = QFormLayout(widget)
        config = get_config()
        self._widgets["video"] = {}

        # Use proto enum keys directly for Video settings to avoid duplicated definitions
        video_keys = [k for k in proto.VideoCodecResolutionType_pb2.VideoCodecResolutionType.keys() if k.startswith("VIDEO_")]
        framerate_keys = [k for k in proto.VideoFrameRateType_pb2.VideoFrameRateType.keys() if k.startswith("VIDEO_FPS_")]
        codec_keys = [k for k in proto.MediaCodecType_pb2.MediaCodecType.keys() if k.startswith("MEDIA_CODEC_VIDEO_")]

        codec_input = _create_enum_combobox_from_keys(codec_keys)
        codec_input.setCurrentText(video_codec_to_proto(config.service_discovery.video.codec))
        
        resolution_input = _create_enum_combobox_from_keys(video_keys)
        resolution_input.setCurrentText(video_resolution_to_proto(config.service_discovery.video.resolution))
        
        frame_rate_input = _create_enum_combobox_from_keys(framerate_keys)
        frame_rate_input.setCurrentText(video_frame_rate_to_proto(config.service_discovery.video.frame_rate))
        
        density_input = NumberControl(config.service_discovery.video.density, 80, 320, 5)
        width_margin_input = NumberControl(config.service_discovery.video.width_margin, 0, 200, 5)
        height_margin_input = NumberControl(config.service_discovery.video.height_margin, 0, 200, 5)

        form.addRow(_styled_label("Codec"), codec_input)
        form.addRow(_styled_label("Resolution"), resolution_input)
        form.addRow(_styled_label("Frame rate"), frame_rate_input)
        form.addRow(_styled_label("Density"), density_input)
        form.addRow(_styled_label("Width margin"), width_margin_input)
        form.addRow(_styled_label("Height margin"), height_margin_input)

        self._widgets["video"] = {
            "codec": codec_input,
            "resolution": resolution_input,
            "frame_rate": frame_rate_input,
            "density": density_input,
            "width_margin": width_margin_input,
            "height_margin": height_margin_input,
        }

        return widget

    def _build_services_tab(self) -> QWidget:
        widget = QWidget()
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(12)

        config = get_config()
        self._widgets["services"] = {}

        enabled = set(config.service_discovery.enabled_channels or [])

        # Show checkboxes for all known channels
        for ch_id, name in sorted(KNOWN_CHANNELS.items()):
            cb = QCheckBox(f"{name} (ID: {ch_id})")
            cb.setChecked(ch_id in enabled)
            layout.addWidget(cb)
            self._widgets["services"][str(ch_id)] = cb

        layout.addStretch(1)
        return widget

    def _build_logging_tab(self) -> QWidget:
        widget = QWidget()
        form = QFormLayout(widget)
        config = get_config()
        self._widgets["logging"] = {}

        core_level_input = _create_enum_combobox(LogLevel)
        core_level_input.setCurrentText(config.logging.core_level)
        core_level_input.setObjectName("ConfigInput")
        form.addRow(_styled_label("Core log level"), core_level_input)
        self._widgets["logging"]["core_level"] = core_level_input

        aasdk_level_input = _create_enum_combobox(LogLevel)
        aasdk_level_input.setCurrentText(config.logging.aasdk_level)
        aasdk_level_input.setObjectName("ConfigInput")
        form.addRow(_styled_label("AASDK log level"), aasdk_level_input)
        self._widgets["logging"]["aasdk_level"] = aasdk_level_input

        return widget

    def save(self):
        cfg = get_config()

        # General
        gen = self._widgets.get("general", {})
        if gen:
            cfg.ui.instance_name = gen["instance_name"].text()
            cfg.ui.mode = gen["mode"].currentText()
            cfg.ui.bt_address = gen["bt_address"].text()

        # Audio
        audio = self._widgets.get("audio", {})
        for ch_id, widgets in audio.items():
            try:
                cfg.service_discovery.audio_streams[int(ch_id)] = AudioStreamConfig(
                    codec=proto_to_audio_codec(widgets["codec"].currentText()),
                    sample_rate=int(widgets["sample_rate"].value()),
                    bits=int(widgets["bits"].value()),
                    channels=int(widgets["channels"].value()),
                )
            except Exception:
                pass

        audio_out = self._widgets.get("audio_output", {})
        if audio_out:
            cfg.audio_output.sink = audio_out["sink"].currentText()
            cfg.audio_output.buffer_ms = int(audio_out["buffer_ms"].value())
            cfg.audio_output.max_queue_frames = int(audio_out["max_queue_frames"].value())
            cfg.audio_output.mic_frame_ms = int(audio_out["mic_frame_ms"].value())
            cfg.audio_output.mic_batch_ms = int(audio_out["mic_batch_ms"].value())
            cfg.audio_output.volume_backend = audio_out["volume_backend"].currentText()
            cfg.audio_output.volume_step = int(audio_out["volume_step"].value())
            cfg.audio_output.volume_device = audio_out["volume_device"].text()
            cfg.audio_output.volume_percent = int(audio_out["volume"].value())

        # Video
        video = self._widgets.get("video", {})
        if video:
            cfg.service_discovery.video = VideoConfig(
                codec=proto_to_video_codec(video["codec"].currentText()),
                resolution=proto_to_video_resolution(video["resolution"].currentText()),
                frame_rate=proto_to_video_frame_rate(video["frame_rate"].currentText()),
                density=int(video["density"].value()),
                width_margin=int(video["width_margin"].value()),
                height_margin=int(video["height_margin"].value()),
            )

        # Services (enabled channels)
        services = self._widgets.get("services", {})
        if services:
            enabled = [int(k) for k, cb in services.items() if cb.isChecked()]
            cfg.service_discovery.enabled_channels = enabled

        # Logging
        log_widgets = self._widgets.get("logging", {})
        if log_widgets:
            cfg.logging.core_level = log_widgets["core_level"].currentText()
            cfg.logging.aasdk_level = log_widgets["aasdk_level"].currentText()

        save_config()

    def reload(self):
        # Refresh widgets from config
        cfg = get_config()

        gen = self._widgets.get("general", {})
        if gen:
            gen["instance_name"].setText(cfg.ui.instance_name)
            gen["mode"].setCurrentText(cfg.ui.mode)
            gen["bt_address"].setText(cfg.ui.bt_address)

        # Rebuild tabs to reflect any structure changes
        # (simpler to just rebuild the module on reload)
        # For now we only update values in-place.
        audio = self._widgets.get("audio", {})
        for ch_id, widgets in audio.items():
            stream = cfg.service_discovery.audio_streams.get(int(ch_id))
            if not stream:
                continue
            widgets["codec"].setCurrentText(audio_codec_to_proto(stream.codec))
            widgets["sample_rate"].set_value(int(stream.sample_rate))
            widgets["bits"].set_value(int(stream.bits))
            widgets["channels"].set_value(int(stream.channels))

        audio_out = self._widgets.get("audio_output", {})
        if audio_out:
            audio_out["sink"].setCurrentText(cfg.audio_output.sink)
            audio_out["buffer_ms"].set_value(int(cfg.audio_output.buffer_ms))
            audio_out["max_queue_frames"].set_value(int(cfg.audio_output.max_queue_frames))
            audio_out["mic_frame_ms"].set_value(int(cfg.audio_output.mic_frame_ms))
            audio_out["mic_batch_ms"].set_value(int(cfg.audio_output.mic_batch_ms))
            audio_out["volume_backend"].setCurrentText(cfg.audio_output.volume_backend)
            audio_out["volume_step"].set_value(int(cfg.audio_output.volume_step))
            audio_out["volume_device"].setText(cfg.audio_output.volume_device)
            vol_current, _ = system_volume_get()
            if vol_current is not None:
                audio_out["volume"].set_value(int(vol_current))
            else:
                audio_out["volume"].set_value(int(cfg.audio_output.volume_percent))

        video = self._widgets.get("video", {})
        if video:
            video["codec"].setCurrentText(video_codec_to_proto(cfg.service_discovery.video.codec))
            video["resolution"].setCurrentText(video_resolution_to_proto(cfg.service_discovery.video.resolution))
            video["frame_rate"].setCurrentText(video_frame_rate_to_proto(cfg.service_discovery.video.frame_rate))
            video["density"].set_value(int(cfg.service_discovery.video.density))
            video["width_margin"].set_value(int(cfg.service_discovery.video.width_margin))
            video["height_margin"].set_value(int(cfg.service_discovery.video.height_margin))

        services = self._widgets.get("services", {})
        for ch_id, cb in services.items():
            cb.setChecked(int(ch_id) in (cfg.service_discovery.enabled_channels or []))

        log_widgets = self._widgets.get("logging", {})
        if log_widgets:
            log_widgets["core_level"].setCurrentText(cfg.logging.core_level)
            log_widgets["aasdk_level"].setCurrentText(cfg.logging.aasdk_level)
