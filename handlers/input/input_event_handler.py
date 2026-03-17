"""
Strict Python implementation for InputEventHandler.
"""

try:
    import protobuf as core
except ImportError:
    core = None

try:
    import google.protobuf  # noqa: F401
    from aasdk_proto.aap_protobuf.service.control.message.ChannelOpenResponse_pb2 import ChannelOpenResponse
    from aasdk_proto.aap_protobuf.service.media.sink.message.KeyBindingResponse_pb2 import KeyBindingResponse
    from aasdk_proto.aap_protobuf.service.media.sink.message.KeyBindingRequest_pb2 import KeyBindingRequest
    from aasdk_proto.aap_protobuf.service.media.sink.message import KeyCode_pb2 as KeyCode
    from aasdk_proto.aap_protobuf.service.inputsource.message.InputReport_pb2 import InputReport
    from aasdk_proto.aap_protobuf.service.inputsource.message.TouchEvent_pb2 import TouchEvent
    from aasdk_proto.aap_protobuf.service.inputsource.message.PointerAction_pb2 import PointerAction
    from aasdk_proto.aap_protobuf.service.inputsource.message.KeyEvent_pb2 import KeyEvent
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    PROTOBUF_AVAILABLE = False
    _PROTOBUF_ERROR = e

from app.core.py_logging import get_logger

_logger = get_logger("app.input")


class InputOrchestrator:
    def __init__(self, supported_keycodes=None):
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError(f"Protobuf modules missing: {_PROTOBUF_ERROR}")
        self._supported_keycodes = set(supported_keycodes or [])
        self._bound_keycodes = set()

    def on_channel_open_request(self, request_bytes):
        resp = ChannelOpenResponse()
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        return resp.SerializeToString()

    def on_key_binding_request(self, request_bytes):
        req = KeyBindingRequest()
        req.ParseFromString(request_bytes)

        resp = KeyBindingResponse()
        status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        if self._supported_keycodes:
            requested = set(req.keycodes)
            if requested and not requested.issubset(self._supported_keycodes):
                status = MessageStatus_pb2.MessageStatus.Value("STATUS_KEYCODE_NOT_BOUND")
            else:
                self._bound_keycodes = requested or set(self._supported_keycodes)
        else:
            self._bound_keycodes = set(req.keycodes)
        resp.status = status
        # Optional: return a first InputReport after key binding.
        return resp.SerializeToString(), b""

    def on_channel_error(self, channel_name, error_str):
        return None


def default_supported_keycodes():
    if not PROTOBUF_AVAILABLE:
        return []
    names = [
        "KEYCODE_HOME",
        "KEYCODE_BACK",
        "KEYCODE_CALL",
        "KEYCODE_ENDCALL",
        "KEYCODE_DPAD_UP",
        "KEYCODE_DPAD_DOWN",
        "KEYCODE_DPAD_LEFT",
        "KEYCODE_DPAD_RIGHT",
        "KEYCODE_DPAD_CENTER",
        "KEYCODE_ENTER",
        "KEYCODE_MENU",
        "KEYCODE_MEDIA_PLAY_PAUSE",
        "KEYCODE_MEDIA_NEXT",
        "KEYCODE_MEDIA_PREVIOUS",
        "KEYCODE_VOLUME_UP",
        "KEYCODE_VOLUME_DOWN",
        "KEYCODE_VOLUME_MUTE",
        "KEYCODE_VOICE_ASSIST",
        "KEYCODE_SCROLL_LOCK",
    ]
    result = []
    for name in names:
        try:
            result.append(KeyCode.KeyCode.Value(name))
        except Exception:
            pass
    return result


class InputEventHandlerLogic:
    def __init__(self, orchestrator=None):
        if core is None:
            raise RuntimeError("Missing 'protobuf' module. Build and import theprotobuf module.")
        self._orchestrator = orchestrator or InputOrchestrator()
        self._channel = None
        self._strand = None

    def on_channel_open_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        self._channel = channel
        self._strand = strand

        _logger.debug("Input: ChannelOpenRequest ricevuta")
        res_bytes = self._orchestrator.on_channel_open_request(payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.control.message.ChannelOpenResponse")
            resp.parse_from_string(res_bytes)
            channel.send_channel_open_response(resp, strand, "Input/ChannelOpenResponse")
            _logger.info("Input: ChannelOpenResponse inviata (canale aperto)")
        channel.receive(handler)

    def on_key_binding_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        self._channel = channel
        self._strand = strand

        _logger.debug("Input: KeyBindingRequest ricevuta")
        res_bytes, report_bytes = self._orchestrator.on_key_binding_request(payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.media.sink.message.KeyBindingResponse")
            resp.parse_from_string(res_bytes)
            channel.send_key_binding_response(resp, strand, "Input/KeyBindingResponse")
            _logger.debug("Input: KeyBindingResponse inviata")
        if report_bytes:
            report = core.GetProtobuf("aap_protobuf.service.inputsource.message.InputReport")
            report.parse_from_string(report_bytes)
            channel.send_input_report(report, strand, "Input/InputReport")
        channel.receive(handler)

    def on_channel_error(self, handler, payload):
        _logger.error("Input: ChannelError %s", payload)
        self._orchestrator.on_channel_error("Input", payload)
        handler.channel.receive(handler)

    # ------------------------------------------------------------------
    # Frontend API (touch + keyboard)
    # ------------------------------------------------------------------
    def _require_channel(self):
        if self._channel is None or self._strand is None:
            raise RuntimeError("Input channel not ready yet. Wait for channel open.")
        return self._channel, self._strand

    @staticmethod
    def _now_us():
        import time
        return int(time.time_ns() // 1000)

    def _send_input_report_bytes(self, report_bytes, tag):
        channel, strand = self._require_channel()
        msg = core.GetProtobuf("aap_protobuf.service.inputsource.message.InputReport")
        msg.parse_from_string(report_bytes)
        channel.send_input_report(msg, strand, tag)

    def send_touch(self, action, pointers, action_index=0, disp_channel_id=0):
        """
        pointers: list of (x, y, pointer_id)
        action: PointerAction enum value
        """
        try:
            _logger.trace(
                "Input/TouchEvent send action=%d action_index=%d pointers=%s",
                int(action),
                int(action_index),
                [(int(x), int(y), int(pid)) for x, y, pid in pointers],
            )
        except Exception:
            pass
        report = InputReport()
        report.timestamp = self._now_us()
        if disp_channel_id:
            report.disp_channel_id = int(disp_channel_id)
        touch = TouchEvent()
        touch.action = int(action)
        touch.action_index = int(action_index)
        for x, y, pid in pointers:
            p = touch.pointer_data.add()
            p.x = int(x)
            p.y = int(y)
            p.pointer_id = int(pid)
        report.touch_event.CopyFrom(touch)
        self._send_input_report_bytes(report.SerializeToString(), "Input/TouchEvent")

    def send_touch_down(self, x, y, pointer_id=0, disp_channel_id=0):
        self.send_touch(PointerAction.ACTION_DOWN, [(x, y, pointer_id)], 0, disp_channel_id)

    def send_touch_up(self, x, y, pointer_id=0, disp_channel_id=0):
        self.send_touch(PointerAction.ACTION_UP, [(x, y, pointer_id)], 0, disp_channel_id)

    def send_touch_move(self, x, y, pointer_id=0, disp_channel_id=0):
        self.send_touch(PointerAction.ACTION_MOVED, [(x, y, pointer_id)], 0, disp_channel_id)

    def send_key(self, keycode, down=True, metastate=0, longpress=False, disp_channel_id=0):
        report = InputReport()
        report.timestamp = self._now_us()
        if disp_channel_id:
            report.disp_channel_id = int(disp_channel_id)
        key = KeyEvent.Key()
        key.keycode = int(keycode)
        key.down = bool(down)
        key.metastate = int(metastate)
        key.longpress = bool(longpress)
        report.key_event.keys.append(key)
        self._send_input_report_bytes(report.SerializeToString(), "Input/KeyEvent")

    def send_key_down(self, keycode, metastate=0, disp_channel_id=0):
        self.send_key(keycode, True, metastate, False, disp_channel_id)

    def send_key_up(self, keycode, metastate=0, disp_channel_id=0):
        self.send_key(keycode, False, metastate, False, disp_channel_id)


def build_pyqt6_keycode_map():
    try:
        from PyQt6.QtCore import Qt
    except Exception:
        return {}

    mapping = {}

    def add(qt_name, keycode_name):
        qt_key = getattr(Qt.Key, qt_name, None)
        if qt_key is not None:
            mapping[int(qt_key)] = KeyCode.KeyCode.Value(keycode_name)

    # Letters
    for ch in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        qt_key = getattr(Qt.Key, f"Key_{ch}", None)
        if qt_key is not None:
            mapping[int(qt_key)] = KeyCode.KeyCode.Value(f"KEYCODE_{ch}")

    # Digits
    for d in range(10):
        qt_key = getattr(Qt.Key, f"Key_{d}", None)
        if qt_key is not None:
            mapping[int(qt_key)] = KeyCode.KeyCode.Value(f"KEYCODE_{d}")

    # Navigation
    add("Key_Left", "KEYCODE_DPAD_LEFT")
    add("Key_Right", "KEYCODE_DPAD_RIGHT")
    add("Key_Up", "KEYCODE_DPAD_UP")
    add("Key_Down", "KEYCODE_DPAD_DOWN")
    add("Key_Return", "KEYCODE_ENTER")
    add("Key_Enter", "KEYCODE_ENTER")
    add("Key_Backspace", "KEYCODE_DEL")
    add("Key_Delete", "KEYCODE_FORWARD_DEL")
    add("Key_Escape", "KEYCODE_ESCAPE")
    add("Key_Tab", "KEYCODE_TAB")
    add("Key_Space", "KEYCODE_SPACE")
    add("Key_Home", "KEYCODE_HOME")
    add("Key_End", "KEYCODE_MOVE_END")
    add("Key_PageUp", "KEYCODE_PAGE_UP")
    add("Key_PageDown", "KEYCODE_PAGE_DOWN")

    # Media
    add("Key_MediaPlay", "KEYCODE_MEDIA_PLAY")
    add("Key_MediaPause", "KEYCODE_MEDIA_PAUSE")
    add("Key_MediaPlayPause", "KEYCODE_MEDIA_PLAY_PAUSE")
    add("Key_MediaStop", "KEYCODE_MEDIA_STOP")
    add("Key_MediaNext", "KEYCODE_MEDIA_NEXT")
    add("Key_MediaPrevious", "KEYCODE_MEDIA_PREVIOUS")
    add("Key_VolumeUp", "KEYCODE_VOLUME_UP")
    add("Key_VolumeDown", "KEYCODE_VOLUME_DOWN")
    add("Key_VolumeMute", "KEYCODE_VOLUME_MUTE")

    # Back/Home (Qt on Android)
    add("Key_Back", "KEYCODE_BACK")
    add("Key_Menu", "KEYCODE_MENU")

    return mapping


class InputFrontend:
    """
    Thin adapter for frontend code (PyQt6 friendly).
    """

    def __init__(self, input_logic, keycode_map=None):
        self._logic = input_logic
        self._keycode_map = keycode_map or build_pyqt6_keycode_map()

    def send_touch_down(self, x, y, pointer_id=0, disp_channel_id=0):
        self._logic.send_touch_down(x, y, pointer_id, disp_channel_id)

    def send_touch_up(self, x, y, pointer_id=0, disp_channel_id=0):
        self._logic.send_touch_up(x, y, pointer_id, disp_channel_id)

    def send_touch_move(self, x, y, pointer_id=0, disp_channel_id=0):
        self._logic.send_touch_move(x, y, pointer_id, disp_channel_id)

    def send_touch(self, action, pointers, action_index=0, disp_channel_id=0):
        self._logic.send_touch(action, pointers, action_index, disp_channel_id)

    def send_key(self, keycode, down=True, metastate=0, longpress=False, disp_channel_id=0):
        self._logic.send_key(keycode, down, metastate, longpress, disp_channel_id)

    def send_key_down(self, keycode, metastate=0, disp_channel_id=0):
        self._logic.send_key_down(keycode, metastate, disp_channel_id)

    def send_key_up(self, keycode, metastate=0, disp_channel_id=0):
        self._logic.send_key_up(keycode, metastate, disp_channel_id)

    def send_key_qt(self, qt_key, down=True, metastate=0, longpress=False, disp_channel_id=0):
        keycode = self._keycode_map.get(int(qt_key))
        if keycode is None:
            raise ValueError(f"Unsupported Qt key: {qt_key}")
        self._logic.send_key(keycode, down, metastate, longpress, disp_channel_id)

    def send_key_down_qt(self, qt_key, metastate=0, disp_channel_id=0):
        self.send_key_qt(qt_key, True, metastate, False, disp_channel_id)

    def send_key_up_qt(self, qt_key, metastate=0, disp_channel_id=0):
        self.send_key_qt(qt_key, False, metastate, False, disp_channel_id)
