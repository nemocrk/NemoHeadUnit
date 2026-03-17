"""
Strict Python implementation for ControlEventHandler.
Logic is split into small modules under control/.
"""

try:
    import protobuf as core
except ImportError:
    core = None

from handlers.control.control_modules.proto import PROTOBUF_AVAILABLE, PROTOBUF_ERROR
from handlers.control.control_modules.handshake import HandshakeState
from handlers.control.control_modules.service_discovery import build_service_discovery_response
from handlers.control.control_modules import focus
from app.core.py_logging import get_logger

_logger = get_logger("app.control")


class ControlOrchestrator:
    def __init__(self,
                 screen_width: int = 800,
                 screen_height: int = 480,
                 bluetooth_available: bool = False,
                 bt_address: str = "",
                 enabled_channels: set[int] | None = None,
                 supported_keycodes: list[int] | None = None):
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError(f"Protobuf modules missing: {PROTOBUF_ERROR}")

        self.handshake = HandshakeState()
        self.screen_width = screen_width
        self.screen_height = screen_height
        self.bluetooth_available = bluetooth_available
        self.bt_address = bt_address
        self.enabled_channels = enabled_channels
        self.supported_keycodes = supported_keycodes

    def set_cryptor(self, cryptor):
        self.handshake.set_cryptor(cryptor)

    def set_enabled_channels(self, enabled_channels: set[int] | None):
        self.enabled_channels = enabled_channels

    # ─────────────────────────────────────────────────────────────────────────
    # Handshake TLS
    # ─────────────────────────────────────────────────────────────────────────
    def on_version_response(self, major: int, minor: int, status: int) -> bytes:
        return self.handshake.on_version_response(major, minor, status)

    def on_handshake(self, payload: bytes) -> bytes:
        return self.handshake.on_handshake(payload)

    def get_auth_complete_response(self) -> bytes:
        return self.handshake.get_auth_complete_response()

    # ─────────────────────────────────────────────────────────────────────────
    # Service Discovery
    # ─────────────────────────────────────────────────────────────────────────
    def on_service_discovery_request(self, payload: bytes) -> bytes:
        return build_service_discovery_response(
            self.screen_width,
            self.screen_height,
            self.bluetooth_available,
            self.bt_address,
            enabled_channels=self.enabled_channels,
            supported_keycodes=self.supported_keycodes,
        )

    # ─────────────────────────────────────────────────────────────────────────
    # Control channel — messaggi ausiliari
    # ─────────────────────────────────────────────────────────────────────────
    def on_ping_request(self, payload: bytes) -> bytes:
        return focus.on_ping_request(payload)

    def on_audio_focus_request(self, payload: bytes) -> bytes:
        return focus.on_audio_focus_request(payload)

    def on_navigation_focus_request(self, payload: bytes) -> bytes:
        return focus.on_navigation_focus_request(payload)

    def on_voice_session_request(self, payload: bytes) -> bytes:
        return focus.on_voice_session_request(payload)

    def on_battery_status_notification(self, payload: bytes) -> bytes:
        return focus.on_battery_status_notification(payload)

    def on_channel_error(self, channel_name, error_str):
        return None


class ControlEventHandlerLogic:
    def __init__(self, orchestrator=None, **orch_kwargs):
        if core is None:
            raise RuntimeError("Missing 'protobuf' module. Build and import theprotobuf module.")
        self._orchestrator = orchestrator or ControlOrchestrator(**orch_kwargs)

    def set_cryptor(self, cryptor):
        self._orchestrator.set_cryptor(cryptor)

    def set_enabled_channels(self, enabled_channels: set[int] | None):
        if hasattr(self._orchestrator, "set_enabled_channels"):
            self._orchestrator.set_enabled_channels(enabled_channels)

    def on_version_response(self, handler, payload):
        _logger.debug("Control: VersionResponse ricevuta")
        major, minor, status = (int(x) for x in payload.decode().split("|"))
        channel = handler.channel
        strand = handler.strand

        out = self._orchestrator.on_version_response(major, minor, status)
        if out:
            channel.send_handshake(out, strand)
        channel.receive(handler)

    def on_handshake(self, handler, payload):
        _logger.debug("Control: Handshake ricevuto")
        channel = handler.channel
        strand = handler.strand

        out = self._orchestrator.on_handshake(payload)
        if out:
            channel.send_handshake(out, strand)
        else:
            auth_bytes = self._orchestrator.get_auth_complete_response()
            if auth_bytes:
                msg = core.GetProtobuf("aap_protobuf.service.control.message.AuthResponse")
                msg.parse_from_string(auth_bytes)
                channel.send_auth_complete(msg, strand)
        channel.receive(handler)

    def on_service_discovery_request(self, handler, payload):
        _logger.info("Control: ServiceDiscoveryRequest ricevuta")
        channel = handler.channel
        strand = handler.strand

        out = self._orchestrator.on_service_discovery_request(payload)
        if out:
            msg = core.GetProtobuf("aap_protobuf.service.control.message.ServiceDiscoveryResponse")
            msg.parse_from_string(out)
            channel.send_service_discovery_response(msg, strand)
        channel.receive(handler)

    def on_audio_focus_request(self, handler, payload):
        _logger.debug("Control: AudioFocusRequest ricevuta")
        channel = handler.channel
        strand = handler.strand

        out = self._orchestrator.on_audio_focus_request(payload)
        if out:
            msg = core.GetProtobuf("aap_protobuf.service.control.message.AudioFocusNotification")
            msg.parse_from_string(out)
            channel.send_audio_focus_response(msg, strand)
        channel.receive(handler)

    def on_navigation_focus_request(self, handler, payload):
        _logger.debug("Control: NavigationFocusRequest ricevuta")
        channel = handler.channel
        strand = handler.strand

        out = self._orchestrator.on_navigation_focus_request(payload)
        if out:
            msg = core.GetProtobuf("aap_protobuf.service.control.message.NavFocusNotification")
            msg.parse_from_string(out)
            channel.send_navigation_focus_response(msg, strand)
        channel.receive(handler)

    def on_voice_session_request(self, handler, payload):
        _logger.debug("Control: VoiceSessionRequest ricevuta")
        channel = handler.channel
        strand = handler.strand

        out = self._orchestrator.on_voice_session_request(payload)
        if out:
            msg = core.GetProtobuf("aap_protobuf.service.control.message.VoiceSessionNotification")
            msg.parse_from_string(out)
            channel.send_voice_session_focus_response(msg, strand)
        channel.receive(handler)

    def on_ping_request(self, handler, payload):
        _logger.debug("Control: PingRequest ricevuta")
        channel = handler.channel
        strand = handler.strand

        out = self._orchestrator.on_ping_request(payload)
        if out:
            msg = core.GetProtobuf("aap_protobuf.service.control.message.PingResponse")
            msg.parse_from_string(out)
            channel.send_ping_response(msg, strand)
        channel.receive(handler)

    def on_ping_response(self, handler, payload):
        handler.channel.receive(handler)

    def on_battery_status_notification(self, handler, payload):
        _logger.debug("Control: BatteryStatusNotification ricevuta")
        self._orchestrator.on_battery_status_notification(payload)
        handler.channel.receive(handler)

    def on_bye_bye_request(self, handler, payload):
        _logger.info("Control: ByeByeRequest ricevuta")
        channel = handler.channel
        strand = handler.strand
        msg = core.GetProtobuf("aap_protobuf.service.control.message.ByeByeResponse")
        channel.send_shutdown_response(msg, strand)
        handler.channel.receive(handler)

    def on_bye_bye_response(self, handler, payload):
        return None

    def on_channel_error(self, handler, payload):
        channel_id = handler.channel.get_id()
        _logger.error("Control CH%d: ChannelError %s", channel_id, payload)
        self._orchestrator.on_channel_error(f"Control CH{channel_id}", payload)
        handler.channel.receive(handler)
