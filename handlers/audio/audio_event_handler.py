"""
Strict Python implementation for AudioEventHandler.

Python receives:
- payload bytes
- channel object (C++ bound)
- strand dispatcher (C++ bound)

Python parses protobuf via C++ ProtobufMessage:
  cfg = core.GetProtobuf("aap_protobuf.service.media.shared.message.Config")
  cfg.parse_from_string(payload)
  channel.send_channel_setup_response(cfg, tag)
"""

try:
    import protobuf as core
except ImportError:
    core = None

try:
    import google.protobuf  # noqa: F401
    from aasdk_proto.aap_protobuf.service.media.shared.message.Config_pb2 import Config as AVChannelConfig
    from aasdk_proto.aap_protobuf.service.control.message.ChannelOpenResponse_pb2 import ChannelOpenResponse
    from aasdk_proto.aap_protobuf.service.media.source.message.Ack_pb2 import Ack as MediaAck
    from aasdk_proto.aap_protobuf.service.media.shared.message.Start_pb2 import Start as MediaStart
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    PROTOBUF_AVAILABLE = False
    _PROTOBUF_ERROR = e

from app.core.py_logging import get_logger

_logger = get_logger("app.audio")


class AudioOrchestrator:
    """
    Minimal in-file orchestrator (no external modules).
    Implement response generation here. By default returns empty bytes.
    """

    def __init__(self):
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError(f"Protobuf modules missing: {_PROTOBUF_ERROR}")
        self._audio_session_id = None

    def set_audio_session_id(self, session_id):
        self._audio_session_id = session_id

    def on_media_channel_setup_request(self, channel_id, request_bytes):
        resp = AVChannelConfig()
        resp.status = AVChannelConfig.Status.Value("STATUS_READY")
        resp.max_unacked = 1
        resp.configuration_indices.append(0)
        return resp.SerializeToString()

    def on_channel_open_request(self, channel_id, request_bytes):
        resp = ChannelOpenResponse()
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        return resp.SerializeToString()

    def on_media_channel_start(self, channel_id, request_bytes):
        return b""

    def on_media_channel_stop(self, channel_id, request_bytes):
        self._audio_session_id = None
        return b""

    def on_media_with_timestamp(self, channel_id, request_bytes):
        # Return Ack bytes if you want to acknowledge media packets.
        if self._audio_session_id is None:
            return b""
        ack = MediaAck()
        ack.session_id = int(self._audio_session_id)
        ack.ack = 1
        return ack.SerializeToString()

    def on_channel_error(self, channel_name, error_str):
        # Hook for logging/telemetry
        return None


class AudioEventHandlerLogic:
    def __init__(self, orchestrator=None):
        if core is None:
            raise RuntimeError("Missing 'protobuf' module. Build and import the protobuf module.")
        self._orchestrator = orchestrator or AudioOrchestrator()

    def on_media_channel_setup_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.debug("Audio CH%d: AVChannelSetupRequest ricevuta", channel_id)
        res_bytes = self._orchestrator.on_media_channel_setup_request(channel_id, payload)
        if res_bytes:
            cfg = core.GetProtobuf("aap_protobuf.service.media.shared.message.Config")
            cfg.parse_from_string(res_bytes)
            channel.send_channel_setup_response(cfg, strand, f"Audio/AVChannelSetupResponse_CH{channel_id}")
            _logger.debug("Audio CH%d: AVChannelSetupResponse inviata", channel_id)
        channel.receive(handler)

    def on_channel_open_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.debug("Audio CH%d: ChannelOpenRequest ricevuta", channel_id)
        res_bytes = self._orchestrator.on_channel_open_request(channel_id, payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.control.message.ChannelOpenResponse")
            resp.parse_from_string(res_bytes)
            channel.send_channel_open_response(resp, strand, f"Audio/ChannelOpenResponse_CH{channel_id}")
            _logger.info("Audio CH%d: ChannelOpenResponse inviata (canale aperto)", channel_id)
        channel.receive(handler)

    def on_media_channel_start_indication(self, handler, payload):
        channel_id = handler.channel.get_id()
        _logger.debug("Audio CH%d: MediaChannelStart indication", channel_id)
        start = MediaStart()
        start.ParseFromString(payload)
        _logger.trace(
            "Audio CH%d: MediaChannelStart session_id=%d payload=%d bytes",
            channel_id,
            start.session_id,
            len(payload),
        )
        self._orchestrator.set_audio_session_id(start.session_id)
        self._orchestrator.on_media_channel_start(channel_id, payload)
        handler.channel.receive(handler)

    def on_media_channel_stop_indication(self, handler, payload):
        channel_id = handler.channel.get_id()
        _logger.debug("Audio CH%d: MediaChannelStop indication", channel_id)
        self._orchestrator.on_media_channel_stop(channel_id, payload)
        handler.channel.receive(handler)

    def on_media_with_timestamp_indication(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.trace("Audio CH%d: MediaWithTimestamp payload=%d bytes", channel_id, len(payload))
        ack_bytes = self._orchestrator.on_media_with_timestamp(channel_id, payload)
        if ack_bytes:
            ack = core.GetProtobuf("aap_protobuf.service.media.source.message.Ack")
            ack.parse_from_string(ack_bytes)
            channel.send_media_ack(ack, strand, f"Audio/MediaAck_CH{channel_id}")
            _logger.trace("Audio CH%d: MediaAck inviata", channel_id)
            try:
                ack_dbg = MediaAck()
                ack_dbg.ParseFromString(ack_bytes)
                _logger.trace(
                    "Audio CH%d: MediaAck session_id=%d ack=%d",
                    channel_id,
                    ack_dbg.session_id,
                    ack_dbg.ack,
                )
            except Exception:
                _logger.trace("Audio CH%d: MediaAck parse failed (debug only)", channel_id)
        handler.channel.receive(handler)

    def on_media_indication(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.trace("Audio CH%d: Media(no-ts) payload=%d bytes", channel_id, len(payload))
        ack_bytes = self._orchestrator.on_media_with_timestamp(channel_id, payload)
        if ack_bytes:
            ack = core.GetProtobuf("aap_protobuf.service.media.source.message.Ack")
            ack.parse_from_string(ack_bytes)
            channel.send_media_ack(ack, strand, f"Audio/MediaAck_CH{channel_id}")
            _logger.debug("Audio CH%d: MediaAck inviata (no-ts)", channel_id)
            try:
                ack_dbg = MediaAck()
                ack_dbg.ParseFromString(ack_bytes)
                _logger.trace(
                    "Audio CH%d: MediaAck(no-ts) session_id=%d ack=%d",
                    channel_id,
                    ack_dbg.session_id,
                    ack_dbg.ack,
                )
            except Exception:
                _logger.trace("Audio CH%d: MediaAck(no-ts) parse failed (debug only)", channel_id)
        handler.channel.receive(handler)

    def on_channel_error(self, handler, payload):
        # payload = error string
        channel_id = handler.channel.get_id()
        _logger.error("Audio CH%d: ChannelError %s", channel_id, payload)
        self._orchestrator.on_channel_error(f"Audio CH{channel_id}", payload)
        handler.channel.receive(handler)
