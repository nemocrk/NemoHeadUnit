"""
Strict Python implementation for NavigationEventHandler.
"""

try:
    import protobuf as core
except ImportError:
    core = None

try:
    import google.protobuf  # noqa: F401
    from aasdk_proto.aap_protobuf.service.control.message.ChannelOpenResponse_pb2 import ChannelOpenResponse
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    PROTOBUF_AVAILABLE = False
    _PROTOBUF_ERROR = e

from app.core.py_logging import get_logger

_logger = get_logger("app.navigation")


class NavigationOrchestrator:
    def __init__(self):
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError(f"Protobuf modules missing: {_PROTOBUF_ERROR}")

    def on_channel_open_request(self, request_bytes):
        resp = ChannelOpenResponse()
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        return resp.SerializeToString()

    def on_status_update(self, request_bytes):
        return None

    def on_turn_event(self, request_bytes):
        return None

    def on_distance_event(self, request_bytes):
        return None

    def on_channel_error(self, channel_name, error_str):
        return None


class NavigationEventHandlerLogic:
    def __init__(self, orchestrator=None):
        if core is None:
            raise RuntimeError("Missing 'protobuf' module. Build and import theprotobuf module.")
        self._orchestrator = orchestrator or NavigationOrchestrator()

    def on_channel_open_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand

        _logger.debug("Navigation: ChannelOpenRequest ricevuta")
        res_bytes = self._orchestrator.on_channel_open_request(payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.control.message.ChannelOpenResponse")
            resp.parse_from_string(res_bytes)
            channel.send_channel_open_response(resp, strand, "Navigation/ChannelOpenResponse")
            _logger.info("Navigation: ChannelOpenResponse inviata (canale aperto)")
        channel.receive(handler)

    def on_status_update(self, handler, payload):
        _logger.debug("Navigation: StatusUpdate ricevuta")
        self._orchestrator.on_status_update(payload)
        handler.channel.receive(handler)

    def on_turn_event(self, handler, payload):
        _logger.debug("Navigation: TurnEvent ricevuta")
        self._orchestrator.on_turn_event(payload)
        handler.channel.receive(handler)

    def on_distance_event(self, handler, payload):
        _logger.debug("Navigation: DistanceEvent ricevuta")
        self._orchestrator.on_distance_event(payload)
        handler.channel.receive(handler)

    def on_channel_error(self, handler, payload):
        _logger.error("Navigation: ChannelError %s", payload)
        self._orchestrator.on_channel_error("Navigation", payload)
        handler.channel.receive(handler)
