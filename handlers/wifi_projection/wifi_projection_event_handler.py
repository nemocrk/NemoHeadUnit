"""
Strict Python implementation for WifiProjectionEventHandler.
"""

try:
    import protobuf as core
except ImportError:
    core = None

try:
    import google.protobuf  # noqa: F401
    from aasdk_proto.aap_protobuf.service.control.message.ChannelOpenResponse_pb2 import ChannelOpenResponse
    from aasdk_proto.aap_protobuf.service.wifiprojection.message.WifiCredentialsResponse_pb2 import WifiCredentialsResponse
    from aasdk_proto.aap_protobuf.service.wifiprojection.message.AccessPointType_pb2 import AccessPointType
    from aasdk_proto.aap_protobuf.service.wifiprojection.message.WifiSecurityMode_pb2 import WifiSecurityMode
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    PROTOBUF_AVAILABLE = False
    _PROTOBUF_ERROR = e

from app.core.py_logging import get_logger

_logger = get_logger("app.wifi_projection")


class WifiProjectionOrchestrator:
    def __init__(self, ssid: str = "NemoHeadUnit", password: str = "12345678", security_mode: int | None = None):
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError(f"Protobuf modules missing: {_PROTOBUF_ERROR}")
        self._ssid = ssid
        self._password = password
        self._security_mode = security_mode or WifiSecurityMode.WPA2_PERSONAL

    def on_channel_open_request(self, channel_id, request_bytes):
        resp = ChannelOpenResponse()
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        return resp.SerializeToString()

    def on_wifi_credentials_request(self, channel_id, request_bytes):
        resp = WifiCredentialsResponse()
        resp.access_point_type = AccessPointType.STATIC
        resp.car_wifi_ssid = self._ssid
        resp.car_wifi_password = self._password
        resp.car_wifi_security_mode = self._security_mode
        return resp.SerializeToString()

    def on_channel_error(self, channel_name, error_str):
        return None


class WifiProjectionEventHandlerLogic:
    def __init__(self, orchestrator=None, **orch_kwargs):
        if core is None:
            raise RuntimeError("Missing 'protobuf' module. Build and import theprotobuf module.")
        self._orchestrator = orchestrator or WifiProjectionOrchestrator(**orch_kwargs)

    def on_channel_open_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand

        _logger.debug("WifiProjection: ChannelOpenRequest ricevuta")
        res_bytes = self._orchestrator.on_channel_open_request(None, payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.control.message.ChannelOpenResponse")
            resp.parse_from_string(res_bytes)
            channel.send_channel_open_response(resp, strand, "WifiProjection/ChannelOpenResponse")
            _logger.info("WifiProjection: ChannelOpenResponse inviata (canale aperto)")
        channel.receive(handler)

    def on_wifi_credentials_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand

        _logger.debug("WifiProjection: WifiCredentialsRequest ricevuta")
        res_bytes = self._orchestrator.on_wifi_credentials_request(None, payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.wifiprojection.message.WifiCredentialsResponse")
            resp.parse_from_string(res_bytes)
            channel.send_wifi_credentials_response(resp, strand, "WifiProjection/WifiCredentialsResponse")
            _logger.debug("WifiProjection: WifiCredentialsResponse inviata")
        channel.receive(handler)

    def on_channel_error(self, handler, payload):
        _logger.error("WifiProjection: ChannelError %s", payload)
        self._orchestrator.on_channel_error("WifiProjection", payload)
        handler.channel.receive(handler)
