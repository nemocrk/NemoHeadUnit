"""
Strict Python implementation for SensorEventHandler.
"""

try:
    import protobuf as core
except ImportError:
    core = None

try:
    import google.protobuf  # noqa: F401
    from aasdk_proto.aap_protobuf.service.control.message.ChannelOpenResponse_pb2 import ChannelOpenResponse
    from aasdk_proto.aap_protobuf.service.sensorsource.message.SensorRequest_pb2 import SensorRequest
    from aasdk_proto.aap_protobuf.service.sensorsource.message.SensorStartResponseMessage_pb2 import SensorStartResponseMessage
    from aasdk_proto.aap_protobuf.service.sensorsource.message.SensorBatch_pb2 import SensorBatch
    from aasdk_proto.aap_protobuf.service.sensorsource.message.SensorType_pb2 import SensorType
    from aasdk_proto.aap_protobuf.service.sensorsource.message.DrivingStatus_pb2 import DrivingStatus
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    PROTOBUF_AVAILABLE = False
    _PROTOBUF_ERROR = e

from app.core.py_logging import get_logger

_logger = get_logger("app.sensor")


class SensorOrchestrator:
    def __init__(self):
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError(f"Protobuf modules missing: {_PROTOBUF_ERROR}")

    def on_channel_open_request(self, request_bytes):
        resp = ChannelOpenResponse()
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        return resp.SerializeToString()

    def on_sensor_start_request(self, request_bytes):
        req = SensorRequest()
        req.ParseFromString(request_bytes)

        resp = SensorStartResponseMessage()
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")

        batch = None
        if req.type == SensorType.Value("SENSOR_DRIVING_STATUS_DATA"):
            batch = SensorBatch()
            batch.driving_status_data.add().status = DrivingStatus.Value("DRIVE_STATUS_UNRESTRICTED")
        elif req.type == SensorType.Value("SENSOR_NIGHT_MODE"):
            batch = SensorBatch()
            batch.night_mode_data.add().night_mode = False

        batch_bytes = batch.SerializeToString() if batch else b""
        return resp.SerializeToString(), batch_bytes

    def on_channel_error(self, channel_name, error_str):
        return None


class SensorEventHandlerLogic:
    def __init__(self, orchestrator=None):
        if core is None:
            raise RuntimeError("Missing 'protobuf' module. Build and import theprotobuf module.")
        self._orchestrator = orchestrator or SensorOrchestrator()

    def on_channel_open_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand

        _logger.debug("Sensor: ChannelOpenRequest ricevuta")
        res_bytes = self._orchestrator.on_channel_open_request(payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.control.message.ChannelOpenResponse")
            resp.parse_from_string(res_bytes)
            channel.send_channel_open_response(resp, strand, "Sensor/ChannelOpenResponse")
            _logger.info("Sensor: ChannelOpenResponse inviata (canale aperto)")
        channel.receive(handler)

    def on_sensor_start_request(self, handler, payload):
        channel = handler.channel
        strand = handler.strand

        _logger.debug("Sensor: SensorStartRequest ricevuta")
        res_bytes, batch_bytes = self._orchestrator.on_sensor_start_request(payload)
        if res_bytes:
            resp = core.GetProtobuf("aap_protobuf.service.sensorsource.message.SensorStartResponseMessage")
            resp.parse_from_string(res_bytes)
            channel.send_sensor_start_response(resp, strand, "Sensor/SensorStartResponse")
            _logger.debug("Sensor: SensorStartResponse inviata")
        if batch_bytes:
            batch = core.GetProtobuf("aap_protobuf.service.sensorsource.message.SensorBatch")
            batch.parse_from_string(batch_bytes)
            channel.send_sensor_event_indication(batch, strand, "Sensor/SensorEventIndication")
            _logger.debug("Sensor: SensorEventIndication inviata")
        channel.receive(handler)

    def on_channel_error(self, handler, payload):
        _logger.error("Sensor: ChannelError %s", payload)
        self._orchestrator.on_channel_error("Sensor", payload)
        handler.channel.receive(handler)
