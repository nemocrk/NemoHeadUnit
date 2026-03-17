"""
Entrypoint.
Delegates orchestration to Orchestrator.
"""

import os
import sys
import signal

_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from app.orchestrator import Orchestrator

try:
    from PyQt6.QtWidgets import QApplication
    from PyQt6.QtCore import QTimer
    PYQT_AVAILABLE = True
except Exception:
    PYQT_AVAILABLE = False


def main():
    import channels
    av = None
    try:
        from app.config import (
            get_config,
            AudioStreamConfig,
            AudioCodec,
            apply_audio_env,
            system_volume_change,
            system_volume_toggle_mute,
            sync_system_volume_to_config,
            system_volume_get,
            system_volume_set,
        )
        from app.core.py_logging import get_logger
        _logger = get_logger("app.av_core")
        apply_audio_env()
        import av_core
        cfg_data = get_config()
        cfg = av_core.AvCoreConfig()
        cfg.jitter_buffer_ms = 120
        cfg.max_queue_frames = int(getattr(cfg_data.audio_output, "max_queue_frames", 10) or 10)
        cfg.audio_frame_ms = 20
        cfg.max_av_lead_ms = 80
        cfg.mic_frame_ms = int(getattr(cfg_data.audio_output, "mic_frame_ms", 20))
        cfg.mic_batch_ms = int(getattr(cfg_data.audio_output, "mic_batch_ms", 100))
        cfg.audio_prebuffer_ms = int(getattr(cfg_data.audio_output, "buffer_ms", 100))
        cfg.overrun_policy = av_core.OverrunPolicy.DROP_OLD
        cfg.underrun_policy = av_core.UnderrunPolicy.SILENCE
        _logger.info(
            "AvCoreConfig: jitter_buffer_ms=%d max_queue_frames=%d audio_frame_ms=%d max_av_lead_ms=%d "
            "audio_prebuffer_ms=%d mic_frame_ms=%d mic_batch_ms=%d overrun_policy=%s underrun_policy=%s",
            cfg.jitter_buffer_ms,
            cfg.max_queue_frames,
            cfg.audio_frame_ms,
            cfg.max_av_lead_ms,
            cfg.audio_prebuffer_ms,
            cfg.mic_frame_ms,
            cfg.mic_batch_ms,
            str(cfg.overrun_policy),
            str(cfg.underrun_policy),
        )

        av = av_core.AvCore(cfg)
        # Use media audio stream config to choose a default output format.
        media_audio_cfg = cfg_data.service_discovery.audio_streams.get(
            int(channels.ChannelId.MEDIA_SINK_MEDIA_AUDIO),
            AudioStreamConfig(codec=AudioCodec.AAC_LC, sample_rate=48000, bits=16, channels=2),
        )
        guidance_audio_cfg = cfg_data.service_discovery.audio_streams.get(
            int(channels.ChannelId.MEDIA_SINK_GUIDANCE_AUDIO),
            media_audio_cfg,
        )
        system_audio_cfg = cfg_data.service_discovery.audio_streams.get(
            int(channels.ChannelId.MEDIA_SINK_SYSTEM_AUDIO),
            media_audio_cfg,
        )

        def _codec_value(codec):
            return codec.value if hasattr(codec, "value") else str(codec)

        av.configure_video(800, 480)
        av.configure_audio(
            media_audio_cfg.sample_rate,
            media_audio_cfg.channels,
            media_audio_cfg.bits,
            _codec_value(media_audio_cfg.codec),
        )
        av.configure_audio_stream(
            int(channels.ChannelId.MEDIA_SINK_MEDIA_AUDIO),
            media_audio_cfg.sample_rate,
            media_audio_cfg.channels,
            media_audio_cfg.bits,
            _codec_value(media_audio_cfg.codec),
        )
        av.configure_audio_stream(
            int(channels.ChannelId.MEDIA_SINK_GUIDANCE_AUDIO),
            guidance_audio_cfg.sample_rate,
            guidance_audio_cfg.channels,
            guidance_audio_cfg.bits,
            _codec_value(guidance_audio_cfg.codec),
        )
        av.configure_audio_stream(
            int(channels.ChannelId.MEDIA_SINK_SYSTEM_AUDIO),
            system_audio_cfg.sample_rate,
            system_audio_cfg.channels,
            system_audio_cfg.bits,
            _codec_value(system_audio_cfg.codec),
        )

        mic_audio_cfg = cfg_data.service_discovery.audio_streams.get(
            int(channels.ChannelId.MEDIA_SOURCE_MICROPHONE),
            AudioStreamConfig(codec=AudioCodec.PCM, sample_rate=16000, bits=16, channels=1),
        )
        av.configure_mic(
            mic_audio_cfg.sample_rate,
            mic_audio_cfg.channels,
            mic_audio_cfg.bits,
        )

        av.set_audio_priority([
            ([int(channels.ChannelId.MEDIA_SINK_GUIDANCE_AUDIO), int(channels.ChannelId.MEDIA_SINK_SYSTEM_AUDIO)], 100, 100, 120),
            ([int(channels.ChannelId.MEDIA_SINK_MEDIA_AUDIO)], 50, 50, 40),
        ])
        # Avvio esplicito: set_window_id prima di start_video.
        av.start_audio()
    except Exception:
        av = None

    channels_list = [
        channels.ChannelId.CONTROL,
        channels.ChannelId.SENSOR,
        channels.ChannelId.INPUT_SOURCE,
        channels.ChannelId.NAVIGATION_STATUS,

        # AV channels get dedicated strands (by name)
        (channels.ChannelId.MEDIA_SINK_VIDEO, "video_strand"),
        (channels.ChannelId.MEDIA_SINK_MEDIA_AUDIO, "media_audio_strand"),
        (channels.ChannelId.MEDIA_SINK_GUIDANCE_AUDIO, "guidance_audio_strand"),
        (channels.ChannelId.MEDIA_SINK_SYSTEM_AUDIO, "system_audio_strand"),
        (channels.ChannelId.MEDIA_SOURCE_MICROPHONE, "microphone_strand"),
    ]

    aa_state = {"active": False}

    def _on_aa_active_changed(active: bool):
        aa_state["active"] = bool(active)

    supported_keycodes = []

    if PYQT_AVAILABLE:
        from PyQt6.QtCore import QEvent, QObject, QPoint
        from app.ui.main_window import MainWindow
        from handlers.input.input_event_handler import InputFrontend, build_pyqt6_keycode_map
        from aasdk_proto.aap_protobuf.service.media.sink.message import KeyCode_pb2 as KeyCode
        from app.core.py_logging import get_logger

        _touch_logger = get_logger("app.ui.touch")

        class _TouchDebugFilter(QObject):
            def __init__(self, parent=None):
                super().__init__(parent)

            def eventFilter(self, obj, event):
                try:
                    et = event.type()
                    if et in (QEvent.Type.TouchBegin, QEvent.Type.TouchUpdate, QEvent.Type.TouchEnd):
                        pts = event.points() if hasattr(event, "points") else []
                        _touch_logger.trace(
                            "AppTouch type=%s points=%d",
                            int(et),
                            len(pts),
                        )
                        for tp in pts:
                            _touch_logger.trace(
                                "AppTouchPoint id=%s state=%s pos=(%d,%d)",
                                getattr(tp, "id", lambda: "?")(),
                                int(tp.state()) if hasattr(tp, "state") else -1,
                                int(tp.position().x()),
                                int(tp.position().y()),
                            )
                        # Forward to overlay to avoid X11 native window routing issues.
                        try:
                            if isinstance(obj, MainWindow):
                                overlay = obj.get_android_auto_input_overlay()
                            else:
                                overlay = None
                                if hasattr(obj, "window"):
                                    win = obj.window()
                                    if isinstance(win, MainWindow):
                                        overlay = win.get_android_auto_input_overlay()
                            if overlay is not None:
                                proxy_points = []

                                class _TP:
                                    def __init__(self, pos, state, pid):
                                        self._pos = pos
                                        self._state = state
                                        self._pid = pid

                                    def position(self):
                                        return self._pos

                                    def state(self):
                                        return self._state

                                    def id(self):
                                        return self._pid

                                for tp in pts:
                                    try:
                                        gp = tp.globalPosition()
                                        gp_x, gp_y = int(gp.x()), int(gp.y())
                                    except Exception:
                                        try:
                                            gp = obj.mapToGlobal(tp.position().toPoint())
                                            gp_x, gp_y = int(gp.x()), int(gp.y())
                                        except Exception:
                                            continue
                                    local = overlay.mapFromGlobal(QPoint(gp_x, gp_y))
                                    proxy_points.append(_TP(local, tp.state(), tp.id()))
                                overlay._handle_touch_points(proxy_points)
                                event.accept()
                                return True
                        except Exception:
                            pass
                except Exception:
                    pass
                return False

        _touch_filter = _TouchDebugFilter()

        def _action_to_keycode(action_id: str):
            mapping = {
                "volume_up": KeyCode.KeyCode.Value("KEYCODE_VOLUME_UP"),
                "volume_down": KeyCode.KeyCode.Value("KEYCODE_VOLUME_DOWN"),
                "assistant": KeyCode.KeyCode.Value("KEYCODE_ASSIST"),
                "mute": KeyCode.KeyCode.Value("KEYCODE_VOLUME_MUTE"),
            }
            return mapping.get(action_id)

        app = QApplication(sys.argv)
        app.installEventFilter(_touch_filter)
        window = MainWindow(width=800, height=480)

        qt_map = build_pyqt6_keycode_map()
        for qt_key in window.get_supported_qt_keys():
            kc = qt_map.get(int(qt_key))
            if kc is not None:
                supported_keycodes.append(int(kc))
        for action_id in window.get_supported_action_ids():
            kc = _action_to_keycode(action_id)
            if kc is not None:
                supported_keycodes.append(int(kc))
        supported_keycodes = sorted(set(supported_keycodes))
    else:
        from handlers.input.input_event_handler import default_supported_keycodes
        supported_keycodes = default_supported_keycodes()

    orch = Orchestrator(
        channels=channels_list,
        av_core=av,
        on_aa_active_changed=_on_aa_active_changed,
        supported_keycodes=supported_keycodes,
    )

    orch.start_runner()
    orch.start_usb()

    if not PYQT_AVAILABLE:
        orch.run_forever()
        return

    input_frontend_holder = {"frontend": None}

    def _on_footer_action(action_id: str):
        cfg_data = get_config()
        step = int(getattr(cfg_data.audio_output, "volume_step", 5) or 5)
        if action_id == "volume_up":
            system_volume_change(step)
            vol, muted = system_volume_get()
            if vol is not None:
                window.show_volume_toast(int(vol), muted=bool(muted))
            return
        if action_id == "volume_down":
            system_volume_change(-step)
            vol, muted = system_volume_get()
            if vol is not None:
                window.show_volume_toast(int(vol), muted=bool(muted))
            return
        if action_id == "mute":
            system_volume_toggle_mute()
            vol, muted = system_volume_get()
            if vol is not None:
                window.show_volume_toast(int(vol), muted=bool(muted))
            return

        if not aa_state["active"]:
            return
        if input_frontend_holder.get("frontend") is None:
            return
        from aasdk_proto.aap_protobuf.service.media.sink.message import KeyCode_pb2 as KeyCode
        keycode = {
            "assistant": KeyCode.KeyCode.Value("KEYCODE_ASSIST"),
        }.get(action_id)
        if keycode is None:
            return
        input_frontend_holder["frontend"].send_key(keycode, True)
        input_frontend_holder["frontend"].send_key(keycode, False)

    window.set_action_handler(_on_footer_action)
    window.set_volume_handler(
        lambda: (system_volume_get()[0] or get_config().audio_output.volume_percent),
        lambda v: system_volume_set(v),
    )
    window.showMaximized()

    last_volume = {"value": None, "muted": None}

    def _poll_volume():
        sync_system_volume_to_config()
        vol, muted = system_volume_get()
        if vol is None:
            return
        if last_volume["value"] != vol or last_volume["muted"] != muted:
            last_volume["value"] = vol
            last_volume["muted"] = muted
            window.show_volume_toast(int(vol), muted=bool(muted))

    vol_timer = QTimer()
    vol_timer.timeout.connect(_poll_volume)
    vol_timer.start(2000)

    def _try_bind_input_frontend():
        if input_frontend_holder["frontend"] is not None:
            return
        logic = orch.get_input_logic()
        if logic is None:
            return
        input_frontend_holder["frontend"] = InputFrontend(logic)
        window.set_input_frontend(input_frontend_holder["frontend"])

    def _try_bind_window_id():
        if av is None:
            return
        video_widget = window.get_android_auto_video_widget()
        if video_widget is None:
            return
        wid = video_widget.get_window_id()
        if wid == 0:
            return
        av.set_window_id(wid)
        av.start_video()

    last_state = {"active": None}

    def _sync_ui_state():
        active = aa_state["active"]
        if last_state["active"] is active:
            return
        last_state["active"] = active
        window.set_input_enabled(active)
        if active:
            window.show_android_auto()
        else:
            window.show_disconnected()

    def _sync_mic_state():
        if av is None:
            return
        try:
            window.set_mic_active(av.is_mic_active())
        except Exception:
            pass

    bind_timer = QTimer()
    bind_timer.setInterval(300)
    bind_timer.timeout.connect(_try_bind_input_frontend)
    bind_timer.timeout.connect(_try_bind_window_id)
    bind_timer.timeout.connect(_sync_ui_state)
    bind_timer.timeout.connect(_sync_mic_state)
    bind_timer.start()

    _sigint_state = {"handled": False}

    def _handle_sigint(_signum, _frame):
        if _sigint_state["handled"]:
            return
        _sigint_state["handled"] = True
        QTimer.singleShot(0, app.quit)

    signal.signal(signal.SIGINT, _handle_sigint)
    signal.signal(signal.SIGTERM, _handle_sigint)

    app.aboutToQuit.connect(orch.stop)
    exit_code = app.exec()
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
