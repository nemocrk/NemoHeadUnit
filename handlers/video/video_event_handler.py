"""
Strict Python implementation for VideoEventHandler.
"""

try:
    import protobuf as core
except ImportError:
    core = None

try:
    import google.protobuf  # noqa: F401
    from aasdk_proto.aap_protobuf.service.media.shared.message.Config_pb2 import Config as AVChannelConfig
    from aasdk_proto.aap_protobuf.service.control.message.ChannelOpenResponse_pb2 import ChannelOpenResponse
    from aasdk_proto.aap_protobuf.service.media.video.message.VideoFocusNotification_pb2 import VideoFocusNotification
    from aasdk_proto.aap_protobuf.service.media.video.message.VideoFocusMode_pb2 import VideoFocusMode
    from aasdk_proto.aap_protobuf.service.media.source.message.Ack_pb2 import Ack as MediaAck
    from aasdk_proto.aap_protobuf.service.media.shared.message.Start_pb2 import Start as MediaStart
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    PROTOBUF_AVAILABLE = False
    _PROTOBUF_ERROR = e

from app.core.py_logging import get_logger

_logger = get_logger("app.video")


class VideoOrchestrator:
    """
    Minimal in-file orchestrator (no external modules).
    Implement response generation here. By default returns empty bytes.
    """

    def __init__(self, on_active_changed=None):
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError(f"Protobuf modules missing: {_PROTOBUF_ERROR}")
        self._on_active_changed = on_active_changed
        self._video_session_id = None

    def set_video_session_id(self, session_id):
        self._video_session_id = session_id

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

    def on_video_focus_request(self, channel_id, request_bytes):
        vf = VideoFocusNotification()
        vf.focus = VideoFocusMode.Value("VIDEO_FOCUS_PROJECTED")
        vf.unsolicited = False
        return vf.SerializeToString()

    def on_video_focus_post_setup(self, channel_id):
        vf = VideoFocusNotification()
        vf.focus = VideoFocusMode.Value("VIDEO_FOCUS_PROJECTED")
        vf.unsolicited = False
        return vf.SerializeToString()

    def on_media_channel_start(self, channel_id, request_bytes):
        if self._on_active_changed is not None:
            self._on_active_changed(True)
        return b""

    def on_media_channel_stop(self, channel_id, request_bytes):
        if self._on_active_changed is not None:
            self._on_active_changed(False)
        self._video_session_id = None
        return b""

    def on_media_with_timestamp(self, channel_id, request_bytes):
        # Return Ack bytes if you want to acknowledge media packets.
        if self._video_session_id is None:
            return b""
        ack = MediaAck()
        ack.session_id = int(self._video_session_id)
        ack.ack = 1
        return ack.SerializeToString()

    def on_channel_error(self, channel_name, error_str):
        # Hook for logging/telemetry
        return None


class VideoEventHandlerLogic:
    def __init__(self, orchestrator=None):
        if core is None:
            raise RuntimeError("Missing 'protobuf' module. Build and import theprotobuf module.")
        self._orchestrator = orchestrator or VideoOrchestrator()

    def on_media_channel_setup_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.debug("Video CH%d: AVChannelSetupRequest ricevuta", channel_id)
        res_bytes = self._orchestrator.on_media_channel_setup_request(channel_id, payload)
        if res_bytes:
            cfg = core.GetProtobuf("aap_protobuf.service.media.shared.message.Config")
            cfg.parse_from_string(res_bytes)
            def _after_setup(err):
                if err:
                    _logger.error("Video CH%d: AVChannelSetupResponse FAILED: %s", channel_id, err)
                    return
                vf_bytes = self._orchestrator.on_video_focus_post_setup(channel_id)
                if not vf_bytes:
                    return
                vf = core.GetProtobuf("aap_protobuf.service.media.video.message.VideoFocusNotification")
                vf.parse_from_string(vf_bytes)
                channel.send_video_focus_indication(vf, strand, f"Video/VideoFocusIndication_CH{channel_id}")
                _logger.debug("Video CH%d: VideoFocusIndication inviata (post-setup)", channel_id)

            channel.send_channel_setup_response(
                cfg,
                strand,
                f"Video/AVChannelSetupResponse_CH{channel_id}",
                _after_setup,
            )
            _logger.debug("Video CH%d: AVChannelSetupResponse inviata", channel_id)
        channel.receive(handler)

    def on_channel_open_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.debug("Video CH%d: ChannelOpenRequest ricevuta", channel_id)
        res_bytes = self._orchestrator.on_channel_open_request(channel_id, payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.control.message.ChannelOpenResponse")
            resp.parse_from_string(res_bytes)
            channel.send_channel_open_response(resp, strand, f"Video/ChannelOpenResponse_CH{channel_id}")
            _logger.info("Video CH%d: ChannelOpenResponse inviata (canale aperto)", channel_id)
        channel.receive(handler)

    def on_video_focus_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.debug("Video CH%d: VideoFocusRequest ricevuta", channel_id)
        res_bytes = self._orchestrator.on_video_focus_request(channel_id, payload)
        if res_bytes:
            vf = core.GetProtobuf("aap_protobuf.service.media.video.message.VideoFocusNotification")
            vf.parse_from_string(res_bytes)
            channel.send_video_focus_indication(vf, strand, f"Video/VideoFocusIndication_CH{channel_id}")
            _logger.debug("Video CH%d: VideoFocusIndication inviata", channel_id)
        channel.receive(handler)

    def on_media_channel_start_indication(self, handler, payload):
        channel_id = handler.channel.get_id()
        _logger.debug("Video CH%d: MediaChannelStart indication", channel_id)
        start = MediaStart()
        start.ParseFromString(payload)
        _logger.trace(
            "Video CH%d: MediaChannelStart session_id=%d payload=%d bytes",
            channel_id,
            start.session_id,
            len(payload),
        )
        self._orchestrator.set_video_session_id(start.session_id)
        self._orchestrator.on_media_channel_start(channel_id, payload)
        handler.channel.receive(handler)

    def on_media_channel_stop_indication(self, handler, payload):
        channel_id = handler.channel.get_id()
        _logger.debug("Video CH%d: MediaChannelStop indication", channel_id)
        self._orchestrator.on_media_channel_stop(channel_id, payload)
        handler.channel.receive(handler)

    def on_media_with_timestamp_indication(self, handler, payload):
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.trace("Video CH%d: MediaWithTimestamp payload=%d bytes", channel_id, len(payload))
        ack_bytes = self._orchestrator.on_media_with_timestamp(channel_id, payload)
        if ack_bytes:
            ack = core.GetProtobuf("aap_protobuf.service.media.source.message.Ack")
            ack.parse_from_string(ack_bytes)
            channel.send_media_ack(ack, strand, f"Video/MediaAck_CH{channel_id}")
            _logger.trace("Video CH%d: MediaAck inviata", channel_id)
            try:
                ack_dbg = MediaAck()
                ack_dbg.ParseFromString(ack_bytes)
                _logger.trace(
                    "Video CH%d: MediaAck session_id=%d ack=%d",
                    channel_id,
                    ack_dbg.session_id,
                    ack_dbg.ack,
                )
            except Exception:
                _logger.trace("Video CH%d: MediaAck parse failed (debug only)", channel_id)
        handler.channel.receive(handler)

    def on_media_indication(self, handler, payload):
        # no-op: avoid heavy processing in Python
        channel = handler.channel
        strand = handler.strand
        channel_id = channel.get_id()

        _logger.trace("Video CH%d: Media(no-ts) payload=%d bytes", channel_id, len(payload))
        ack_bytes = self._orchestrator.on_media_with_timestamp(channel_id, payload)
        if ack_bytes:
            ack = core.GetProtobuf("aap_protobuf.service.media.source.message.Ack")
            ack.parse_from_string(ack_bytes)
            channel.send_media_ack(ack, strand, f"Video/MediaAck_CH{channel_id}")
            _logger.debug("Video CH%d: MediaAck inviata (no-ts)", channel_id)
            try:
                ack_dbg = MediaAck()
                ack_dbg.ParseFromString(ack_bytes)
                _logger.trace(
                    "Video CH%d: MediaAck(no-ts) session_id=%d ack=%d",
                    channel_id,
                    ack_dbg.session_id,
                    ack_dbg.ack,
                )
            except Exception:
                _logger.trace("Video CH%d: MediaAck(no-ts) parse failed (debug only)", channel_id)
        handler.channel.receive(handler)

    def on_channel_error(self, handler, payload):
        channel_id = handler.channel.get_id()
        _logger.error("Video CH%d: ChannelError %s", channel_id, payload)
        self._orchestrator.on_channel_error(f"Video CH{channel_id}", payload)
        handler.channel.receive(handler)
