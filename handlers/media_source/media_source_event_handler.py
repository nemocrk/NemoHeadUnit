"""
Strict Python implementation for MediaSourceEventHandler.
"""

from __future__ import annotations

try:
    import protobuf as core
except ImportError:
    core = None

try:
    import google.protobuf  # noqa: F401
    from aasdk_proto.aap_protobuf.service.control.message.ChannelOpenResponse_pb2 import ChannelOpenResponse
    from aasdk_proto.aap_protobuf.service.media.shared.message.Config_pb2 import Config as AVChannelConfig
    from aasdk_proto.aap_protobuf.service.media.source.message.MicrophoneRequest_pb2 import MicrophoneRequest
    from aasdk_proto.aap_protobuf.service.media.source.message.MicrophoneResponse_pb2 import MicrophoneResponse
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    PROTOBUF_AVAILABLE = False
    _PROTOBUF_ERROR = e

from app.core.py_logging import get_logger

_logger = get_logger("app.media_source")


class MediaSourceOrchestrator:
    def __init__(self, session_id: int = 0):
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError(f"Protobuf modules missing: {_PROTOBUF_ERROR}")
        self._session_id = session_id

    def on_channel_open_request(self, channel_id, request_bytes):
        resp = ChannelOpenResponse()
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        return resp.SerializeToString()

    def on_media_channel_setup_request(self, channel_id, request_bytes):
        resp = AVChannelConfig()
        resp.status = AVChannelConfig.Status.Value("STATUS_READY")
        resp.max_unacked = 1
        resp.configuration_indices.append(0)
        return resp.SerializeToString()

    def on_media_source_open_request(self, channel_id, request_bytes):
        req = MicrophoneRequest()
        req.ParseFromString(request_bytes)

        resp = MicrophoneResponse()
        resp.session_id = self._session_id
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        return resp.SerializeToString()

    def on_media_channel_ack_indication(self, channel_id, request_bytes):
        return None

    def on_channel_error(self, channel_name, error_str):
        return None


class MediaSourceEventHandlerLogic:
    def __init__(self, orchestrator=None, **orch_kwargs):
        if core is None:
            raise RuntimeError("Missing 'protobuf' module. Build and import theprotobuf module.")
        self._orchestrator = orchestrator or MediaSourceOrchestrator(**orch_kwargs)

    def on_channel_open_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.debug("MediaSource CH%d: ChannelOpenRequest ricevuta", channel_id)
        res_bytes = self._orchestrator.on_channel_open_request(channel_id, payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.control.message.ChannelOpenResponse")
            resp.parse_from_string(res_bytes)
            channel.send_channel_open_response(resp, strand, f"MediaSource/ChannelOpenResponse_CH{channel_id}")
            _logger.info("MediaSource CH%d: ChannelOpenResponse inviata (canale aperto)", channel_id)
        channel.receive(handler)

    def on_media_channel_setup_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.debug("MediaSource CH%d: AVChannelSetupRequest ricevuta", channel_id)
        res_bytes = self._orchestrator.on_media_channel_setup_request(channel_id, payload)
        if res_bytes:
            cfg = core.GetProtobuf("aap_protobuf.service.media.shared.message.Config")
            cfg.parse_from_string(res_bytes)
            channel.send_channel_setup_response(cfg, strand, f"MediaSource/ChannelSetupResponse_CH{channel_id}")
            _logger.debug("MediaSource CH%d: ChannelSetupResponse inviata", channel_id)
        channel.receive(handler)

    def on_media_source_open_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.debug("MediaSource CH%d: MicrophoneRequest ricevuta", channel_id)
        res_bytes = self._orchestrator.on_media_source_open_request(channel_id, payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.media.source.message.MicrophoneResponse")
            resp.parse_from_string(res_bytes)
            channel.send_microphone_open_response(resp, strand, f"MediaSource/MicrophoneResponse_CH{channel_id}")
            _logger.info("MediaSource CH%d: MicrophoneResponse inviata (mic aperto)", channel_id)
        channel.receive(handler)

    def on_media_channel_ack_indication(self, handler, payload):
        _logger.debug("MediaSource CH%d: MediaAck indication", handler.channel.get_id())
        self._orchestrator.on_media_channel_ack_indication(handler.channel.get_id(), payload)
        handler.channel.receive(handler)

    def on_channel_error(self, handler, payload):
        channel_id = handler.channel.get_id()
        _logger.error("MediaSource CH%d: ChannelError %s", channel_id, payload)
        self._orchestrator.on_channel_error(f"MediaSource CH{channel_id}", payload)
        handler.channel.receive(handler)
