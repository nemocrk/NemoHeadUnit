import time

from .utils import log_and_send
from app.core.py_logging import get_logger
from . import proto

_logger = get_logger("app.control.focus")

def on_ping_request(payload: bytes) -> bytes:
    _logger.debug("PingRequest ricevuta -> PingResponse inviata")
    msg = proto.PingResponse_pb2.PingResponse()
    msg.timestamp = int(time.time() * 1000)
    return msg.SerializeToString()


def on_audio_focus_request(payload: bytes) -> bytes:
    _logger.debug("AudioFocusRequest ricevuta -> AudioFocusNotification inviata")
    req = proto.AudioFocusRequest_pb2.AudioFocusRequest()
    req.ParseFromString(payload)
    is_release = (
        req.audio_focus_type ==
        proto.AudioFocusRequestType_pb2.AudioFocusRequestType.Value("AUDIO_FOCUS_RELEASE")
    )
    state = (
        proto.AudioFocusStateType_pb2.AudioFocusStateType.Value("AUDIO_FOCUS_STATE_LOSS")
        if is_release else
        proto.AudioFocusStateType_pb2.AudioFocusStateType.Value("AUDIO_FOCUS_STATE_GAIN")
    )
    msg = proto.AudioFocusNotification_pb2.AudioFocusNotification()
    msg.focus_state = state
    return log_and_send("Invia AudioFocusNotification", msg.SerializeToString())


def on_navigation_focus_request(payload: bytes) -> bytes:
    _logger.debug("NavigationFocusRequest ricevuta -> NavFocusNotification(PROJECTED) inviata")
    msg = proto.NavFocusNotification_pb2.NavFocusNotification()
    msg.focus_type = proto.NavFocusType_pb2.NavFocusType.Value("NAV_FOCUS_PROJECTED")
    return log_and_send("Invia NavFocusNotification", msg.SerializeToString())


def on_voice_session_request(payload: bytes) -> bytes:
    _logger.debug("VoiceSessionRequest ricevuta -> (sink silente)")
    return b""


def on_battery_status_notification(payload: bytes) -> bytes:
    _logger.debug("BatteryStatusNotification ricevuta -> (sink silente)")
    return b""
