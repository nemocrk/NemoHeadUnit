"""
Strict Python implementation for MediaPlaybackStatusEventHandler.
"""

try:
    import protobuf as core
except ImportError:
    core = None

try:
    import google.protobuf  # noqa: F401
    from aasdk_proto.aap_protobuf.service.control.message.ChannelOpenResponse_pb2 import ChannelOpenResponse
    from aasdk_proto.aap_protobuf.service.mediaplayback.message.MediaPlaybackMetadata_pb2 import MediaPlaybackMetadata
    from aasdk_proto.aap_protobuf.service.mediaplayback.message.MediaPlaybackStatus_pb2 import MediaPlaybackStatus
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    PROTOBUF_AVAILABLE = False
    _PROTOBUF_ERROR = e

from app.core.py_logging import get_logger

_logger = get_logger("app.media_playback")


class MediaPlaybackStatusOrchestrator:
    def __init__(self):
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError(f"Protobuf modules missing: {_PROTOBUF_ERROR}")

    def on_channel_open_request(self, channel_id, request_bytes):
        resp = ChannelOpenResponse()
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        return resp.SerializeToString()

    def on_metadata_update(self, channel_id, request_bytes):
        metadata = MediaPlaybackMetadata()
        metadata.ParseFromString(request_bytes)
        return metadata

    def on_playback_update(self, channel_id, request_bytes):
        playback = MediaPlaybackStatus()
        playback.ParseFromString(request_bytes)
        return playback

    def on_channel_error(self, channel_name, error_str):
        return None


class MediaPlaybackStatusEventHandlerLogic:
    def __init__(self, orchestrator=None):
        if core is None:
            raise RuntimeError("Missing 'protobuf' module. Build and import theprotobuf module.")
        self._orchestrator = orchestrator or MediaPlaybackStatusOrchestrator()

    def on_channel_open_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand

        _logger.debug("MediaPlaybackStatus: ChannelOpenRequest ricevuta")
        res_bytes = self._orchestrator.on_channel_open_request(None, payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.control.message.ChannelOpenResponse")
            resp.parse_from_string(res_bytes)
            channel.send_channel_open_response(resp, strand, "MediaPlaybackStatus/ChannelOpenResponse")
            _logger.info("MediaPlaybackStatus: ChannelOpenResponse inviata (canale aperto)")
        channel.receive(handler)

    def on_metadata_update(self, handler, payload):
        _logger.debug("MediaPlaybackStatus: MetadataUpdate ricevuta")
        self._orchestrator.on_metadata_update(None, payload)
        handler.channel.receive(handler)

    def on_playback_update(self, handler, payload):
        _logger.debug("MediaPlaybackStatus: PlaybackUpdate ricevuta")
        self._orchestrator.on_playback_update(None, payload)
        handler.channel.receive(handler)

    def on_channel_error(self, handler, payload):
        _logger.error("MediaPlaybackStatus: ChannelError %s", payload)
        self._orchestrator.on_channel_error("MediaPlaybackStatus", payload)
        handler.channel.receive(handler)
