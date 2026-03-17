"""
Strict Python implementation for BluetoothEventHandler.
"""

try:
    import protobuf as core
except ImportError:
    core = None

try:
    import google.protobuf  # noqa: F401
    from aasdk_proto.aap_protobuf.service.control.message.ChannelOpenResponse_pb2 import ChannelOpenResponse
    from aasdk_proto.aap_protobuf.service.bluetooth.message.BluetoothPairingResponse_pb2 import BluetoothPairingResponse
    from aasdk_proto.aap_protobuf.service.bluetooth.message.BluetoothAuthenticationData_pb2 import BluetoothAuthenticationData
    from aasdk_proto.aap_protobuf.service.bluetooth.message.BluetoothPairingMethod_pb2 import BluetoothPairingMethod
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    PROTOBUF_AVAILABLE = False
    _PROTOBUF_ERROR = e

from app.core.py_logging import get_logger

_logger = get_logger("app.bluetooth")


class BluetoothOrchestrator:
    def __init__(self, auth_data: str = "123456", already_paired: bool = False):
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError(f"Protobuf modules missing: {_PROTOBUF_ERROR}")
        self._auth_data = auth_data
        self._already_paired = already_paired

    def on_channel_open_request(self, channel_id, request_bytes):
        resp = ChannelOpenResponse()
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        return resp.SerializeToString()

    def on_bluetooth_pairing_request(self, channel_id, request_bytes):
        pairing = BluetoothPairingResponse()
        pairing.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        pairing.already_paired = bool(self._already_paired)

        auth = BluetoothAuthenticationData()
        auth.auth_data = self._auth_data
        auth.pairing_method = BluetoothPairingMethod.BLUETOOTH_PAIRING_PIN

        return pairing.SerializeToString(), auth.SerializeToString()

    def on_bluetooth_authentication_result(self, channel_id, request_bytes):
        return None

    def on_channel_error(self, channel_name, error_str):
        return None


class BluetoothEventHandlerLogic:
    def __init__(self, orchestrator=None, **orch_kwargs):
        if core is None:
            raise RuntimeError("Missing 'protobuf' module. Build and import the protobuf module.")
        self._orchestrator = orchestrator or BluetoothOrchestrator(**orch_kwargs)

    def on_channel_open_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand

        _logger.debug("Bluetooth: ChannelOpenRequest ricevuta")
        res_bytes = self._orchestrator.on_channel_open_request(None, payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.control.message.ChannelOpenResponse")
            resp.parse_from_string(res_bytes)
            channel.send_channel_open_response(resp, strand, "Bluetooth/ChannelOpenResponse")
            _logger.info("Bluetooth: ChannelOpenResponse inviata (canale aperto)")
        channel.receive(handler)

    def on_bluetooth_pairing_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand

        _logger.debug("Bluetooth: PairingRequest ricevuta")
        res = self._orchestrator.on_bluetooth_pairing_request(None, payload)
        if res:
            pairing_bytes, auth_bytes = res
            if pairing_bytes:
                pairing = core.GetProtobuf(
                    "aap_protobuf.service.bluetooth.message.BluetoothPairingResponse"
                )
                pairing.parse_from_string(pairing_bytes)
                channel.send_bluetooth_pairing_response(pairing, strand, "Bluetooth/PairingResponse")
                _logger.debug("Bluetooth: PairingResponse inviata")
            if auth_bytes:
                auth = core.GetProtobuf(
                    "aap_protobuf.service.bluetooth.message.BluetoothAuthenticationData"
                )
                auth.parse_from_string(auth_bytes)
                channel.send_bluetooth_authentication_data(auth, strand, "Bluetooth/AuthData")
                _logger.debug("Bluetooth: AuthData inviata")
        channel.receive(handler)

    def on_bluetooth_authentication_result(self, handler, payload):
        _logger.debug("Bluetooth: AuthenticationResult ricevuta")
        self._orchestrator.on_bluetooth_authentication_result(None, payload)
        handler.channel.receive(handler)

    def on_channel_error(self, handler, payload):
        _logger.error("Bluetooth: ChannelError %s", payload)
        self._orchestrator.on_channel_error("Bluetooth", payload)
        handler.channel.receive(handler)
