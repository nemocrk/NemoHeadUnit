PROTOBUF_ERROR = None

AuthResponse_pb2 = None
ServiceDiscoveryResponse_pb2 = None
DriverPosition_pb2 = None
PingResponse_pb2 = None
AudioFocusNotification_pb2 = None
AudioFocusRequest_pb2 = None
AudioFocusRequestType_pb2 = None
AudioFocusStateType_pb2 = None
NavFocusNotification_pb2 = None
NavFocusType_pb2 = None
MessageStatus_pb2 = None

MediaSinkService_pb2 = None
VideoCodecResolutionType_pb2 = None
VideoFrameRateType_pb2 = None
AudioStreamType_pb2 = None
MediaCodecType_pb2 = None
AudioConfiguration_pb2 = None

SensorSourceService_pb2 = None
SensorType_pb2 = None
InputSourceService_pb2 = None
TouchScreenType_pb2 = None
NavigationStatusService_pb2 = None
BluetoothService_pb2 = None
BluetoothPairingMethod_pb2 = None
GenericNotificationService_pb2 = None
MediaBrowserService_pb2 = None
MediaPlaybackStatusService_pb2 = None
PhoneStatusService_pb2 = None
RadioService_pb2 = None
VendorExtensionService_pb2 = None
WifiProjectionService_pb2 = None


def _set(name, module_path):
    globals()[name] = __import__(module_path, fromlist=["*"])


def _set_symbol(name, module_path, symbol):
    globals()[name] = getattr(__import__(module_path, fromlist=[symbol]), symbol)


def _try_import(fn):
    global PROTOBUF_ERROR
    try:
        fn()
    except ImportError as e:
        if PROTOBUF_ERROR is None:
            PROTOBUF_ERROR = e


_try_import(lambda: __import__("google.protobuf"))
_try_import(lambda: _set("AuthResponse_pb2", "aasdk_proto.aap_protobuf.service.control.message.AuthResponse_pb2"))
_try_import(lambda: _set("ServiceDiscoveryResponse_pb2", "aasdk_proto.aap_protobuf.service.control.message.ServiceDiscoveryResponse_pb2"))
_try_import(lambda: _set("DriverPosition_pb2", "aasdk_proto.aap_protobuf.service.control.message.DriverPosition_pb2"))
_try_import(lambda: _set("PingResponse_pb2", "aasdk_proto.aap_protobuf.service.control.message.PingResponse_pb2"))
_try_import(lambda: _set("AudioFocusNotification_pb2", "aasdk_proto.aap_protobuf.service.control.message.AudioFocusNotification_pb2"))
_try_import(lambda: _set("AudioFocusRequest_pb2", "aasdk_proto.aap_protobuf.service.control.message.AudioFocusRequest_pb2"))
_try_import(lambda: _set("AudioFocusRequestType_pb2", "aasdk_proto.aap_protobuf.service.control.message.AudioFocusRequestType_pb2"))
_try_import(lambda: _set("AudioFocusStateType_pb2", "aasdk_proto.aap_protobuf.service.control.message.AudioFocusStateType_pb2"))
_try_import(lambda: _set("NavFocusNotification_pb2", "aasdk_proto.aap_protobuf.service.control.message.NavFocusNotification_pb2"))
_try_import(lambda: _set("NavFocusType_pb2", "aasdk_proto.aap_protobuf.service.control.message.NavFocusType_pb2"))
_try_import(lambda: _set("MessageStatus_pb2", "aasdk_proto.aap_protobuf.shared.MessageStatus_pb2"))

_try_import(lambda: _set("MediaSinkService_pb2", "aasdk_proto.aap_protobuf.service.media.sink.MediaSinkService_pb2"))
_try_import(lambda: _set("VideoCodecResolutionType_pb2", "aasdk_proto.aap_protobuf.service.media.sink.message.VideoCodecResolutionType_pb2"))
_try_import(lambda: _set("VideoFrameRateType_pb2", "aasdk_proto.aap_protobuf.service.media.sink.message.VideoFrameRateType_pb2"))
_try_import(lambda: _set("AudioStreamType_pb2", "aasdk_proto.aap_protobuf.service.media.sink.message.AudioStreamType_pb2"))
_try_import(lambda: _set("MediaCodecType_pb2", "aasdk_proto.aap_protobuf.service.media.shared.message.MediaCodecType_pb2"))
_try_import(lambda: _set("AudioConfiguration_pb2", "aasdk_proto.aap_protobuf.service.media.shared.message.AudioConfiguration_pb2"))

_try_import(lambda: _set("SensorSourceService_pb2", "aasdk_proto.aap_protobuf.service.sensorsource.SensorSourceService_pb2"))
_try_import(lambda: _set("SensorType_pb2", "aasdk_proto.aap_protobuf.service.sensorsource.message.SensorType_pb2"))
_try_import(lambda: _set("InputSourceService_pb2", "aasdk_proto.aap_protobuf.service.inputsource.InputSourceService_pb2"))
_try_import(lambda: _set("TouchScreenType_pb2", "aasdk_proto.aap_protobuf.service.inputsource.message.TouchScreenType_pb2"))
_try_import(lambda: _set("NavigationStatusService_pb2", "aasdk_proto.aap_protobuf.service.navigationstatus.NavigationStatusService_pb2"))
_try_import(lambda: _set("BluetoothService_pb2", "aasdk_proto.aap_protobuf.service.bluetooth.BluetoothService_pb2"))
_try_import(lambda: _set("BluetoothPairingMethod_pb2", "aasdk_proto.aap_protobuf.service.bluetooth.message.BluetoothPairingMethod_pb2"))
_try_import(lambda: _set("GenericNotificationService_pb2", "aasdk_proto.aap_protobuf.service.genericnotification.GenericNotificationService_pb2"))
_try_import(lambda: _set("MediaBrowserService_pb2", "aasdk_proto.aap_protobuf.service.mediabrowser.MediaBrowserService_pb2"))
_try_import(lambda: _set("MediaPlaybackStatusService_pb2", "aasdk_proto.aap_protobuf.service.mediaplayback.MediaPlaybackStatusService_pb2"))
_try_import(lambda: _set("PhoneStatusService_pb2", "aasdk_proto.aap_protobuf.service.phonestatus.PhoneStatusService_pb2"))
_try_import(lambda: _set("RadioService_pb2", "aasdk_proto.aap_protobuf.service.radio.RadioService_pb2"))
_try_import(lambda: _set("VendorExtensionService_pb2", "aasdk_proto.aap_protobuf.service.vendorextension.VendorExtensionService_pb2"))
_try_import(lambda: _set("WifiProjectionService_pb2", "aasdk_proto.aap_protobuf.service.wifiprojection.WifiProjectionService_pb2"))


PROTOBUF_AVAILABLE = PROTOBUF_ERROR is None
