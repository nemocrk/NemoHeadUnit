import os

from .utils import log_and_send
from . import proto
import channels as _channels
from app.core.py_logging import get_logger
from app.config import get_config, AudioStreamConfig, VideoConfig, AudioCodec

_logger = get_logger("app.control.discovery")


def _marshal_enum(enum_type, value: str, default: str):
    """Safely marshal an enum value string to the protobuf enum."""

    try:
        return enum_type.Value(value)
    except Exception:
        return enum_type.Value(default)


def _normalize_enum_key(key: str) -> str:
    return key.strip().upper().replace(" ", "_").replace("-", "_")


def _select_enum_value(enum_type, candidate: str, default: str) -> str:
    """Return a valid enum key from a candidate string, falling back to default."""

    if not candidate:
        return default

    norm = _normalize_enum_key(candidate)

    keys = list(enum_type.keys())
    if norm in keys:
        return norm

    # Direct substring match (e.g. H264 matches MEDIA_CODEC_VIDEO_H264_BP)
    for k in keys:
        if norm in _normalize_enum_key(k):
            return k

    # Allow matching by stripping common prefixes
    stripped = norm
    for prefix in ["MEDIA_CODEC_VIDEO_", "MEDIA_CODEC_AUDIO_", "VIDEO_", "AUDIO_"]:
        if stripped.startswith(prefix):
            stripped = stripped[len(prefix):]
    for k in keys:
        if stripped in _normalize_enum_key(k):
            return k

    return default


def _env_truthy(value: str | None) -> bool:
    if not value:
        return False
    return value.strip().lower() in {"1", "true", "yes", "on"}


def build_service_discovery_response(
    screen_width: int,
    screen_height: int,
    bluetooth_available: bool,
    bt_address: str,
    enabled_channels: set[int] | None = None,
    supported_keycodes: list[int] | None = None,
) -> bytes:
    _logger.info("ServiceDiscoveryRequest ricevuta -> ServiceDiscoveryResponse inviata")

    config = get_config()
    sd_cfg = config.service_discovery

    force_pcm = _env_truthy(os.getenv("NEMO_AUDIO_FORCE_PCM"))
    force_uniform = _env_truthy(os.getenv("NEMO_AUDIO_FORCE_UNIFORM_FORMAT"))
    base_audio_cfg = sd_cfg.audio_streams.get(
        int(_channels.ChannelId.MEDIA_SINK_MEDIA_AUDIO),
        AudioStreamConfig(),
    )

    def _apply_audio_overrides(cfg: AudioStreamConfig, base_cfg: AudioStreamConfig | None = None) -> AudioStreamConfig:
        out = cfg
        if force_uniform and base_cfg is not None:
            out = AudioStreamConfig(
                codec=out.codec,
                sample_rate=base_cfg.sample_rate,
                bits=base_cfg.bits,
                channels=base_cfg.channels,
            )
        if force_pcm:
            out = AudioStreamConfig(
                codec=AudioCodec.PCM,
                sample_rate=out.sample_rate,
                bits=out.bits,
                channels=out.channels,
            )
        return out

    if force_pcm or force_uniform:
        _logger.info(
            "ServiceDiscovery audio overrides: force_pcm=%s force_uniform=%s",
            force_pcm,
            force_uniform,
        )

    if enabled_channels is None:
        enabled = set(sd_cfg.enabled_channels or [])
    else:
        enabled = set(enabled_channels) if enabled_channels is not None else None

    def is_enabled(ch_id: int) -> bool:
        return enabled is None or ch_id in enabled

    msg = proto.ServiceDiscoveryResponse_pb2.ServiceDiscoveryResponse()

    msg.headunit_info.make = "NemoDev"
    msg.headunit_info.model = "NemoHU"
    msg.headunit_info.year = "2025"
    msg.headunit_info.vehicle_id = "NEMO0001"
    msg.headunit_info.head_unit_make = "NemoDev"
    msg.headunit_info.head_unit_model = "NemoHeadUnit"
    msg.headunit_info.head_unit_software_build = "1"
    msg.headunit_info.head_unit_software_version = "0.1.0"
    msg.driver_position = proto.DriverPosition_pb2.DRIVER_POSITION_LEFT
    msg.can_play_native_media_during_vr = False
    msg.display_name = "NemoHeadUnit"
    msg.probe_for_support = False

    conn_cfg = msg.connection_configuration
    ping_cfg = conn_cfg.ping_configuration
    ping_cfg.tracked_ping_count = 5
    ping_cfg.timeout_ms = 3000
    ping_cfg.interval_ms = 1000
    ping_cfg.high_latency_threshold_ms = 200

    supported_keycodes = supported_keycodes if supported_keycodes is not None else sd_cfg.supported_keycodes

    _logger.info(
        "ServiceDiscovery config: screen=%dx%d bt_available=%s enabled_channels=%s keycodes=%d",
        screen_width,
        screen_height,
        bluetooth_available,
        sorted(enabled) if enabled is not None else "ALL",
        len(supported_keycodes or []),
    )

    # CH 1: SENSOR SOURCE
    if is_enabled(int(_channels.ChannelId.SENSOR)):
        ch1 = msg.channels.add()
        ch1.id = int(_channels.ChannelId.SENSOR)
        svc_sensor = proto.SensorSourceService_pb2.SensorSourceService()
        s1 = svc_sensor.sensors.add(); s1.sensor_type = proto.SensorType_pb2.SensorType.Value("SENSOR_DRIVING_STATUS_DATA")
        s2 = svc_sensor.sensors.add(); s2.sensor_type = proto.SensorType_pb2.SensorType.Value("SENSOR_LOCATION")
        s3 = svc_sensor.sensors.add(); s3.sensor_type = proto.SensorType_pb2.SensorType.Value("SENSOR_NIGHT_MODE")
        ch1.sensor_source_service.CopyFrom(svc_sensor)

    # CH 3: VIDEO
    if is_enabled(int(_channels.ChannelId.MEDIA_SINK_VIDEO)):
        ch3 = msg.channels.add()
        ch3.id = int(_channels.ChannelId.MEDIA_SINK_VIDEO)
        svc_video = proto.MediaSinkService_pb2.MediaSinkService()
        svc_video.available_type = _marshal_enum(
            proto.MediaCodecType_pb2.MediaCodecType,
            _select_enum_value(
                proto.MediaCodecType_pb2.MediaCodecType,
                sd_cfg.video.codec,
                "MEDIA_CODEC_VIDEO_H264_BP",
            ),
            "MEDIA_CODEC_VIDEO_H264_BP",
        )
        svc_video.available_while_in_call = True
        vcfg = svc_video.video_configs.add()
        vcfg.codec_resolution = _marshal_enum(
            proto.VideoCodecResolutionType_pb2.VideoCodecResolutionType,
            _select_enum_value(
                proto.VideoCodecResolutionType_pb2.VideoCodecResolutionType,
                f"VIDEO_{sd_cfg.video.resolution}",
                "VIDEO_800x480",
            ),
            "VIDEO_800x480",
        )
        vcfg.frame_rate = _marshal_enum(
            proto.VideoFrameRateType_pb2.VideoFrameRateType,
            _select_enum_value(
                proto.VideoFrameRateType_pb2.VideoFrameRateType,
                f"VIDEO_FPS_{sd_cfg.video.frame_rate}",
                "VIDEO_FPS_30",
            ),
            "VIDEO_FPS_30",
        )
        vcfg.density = sd_cfg.video.density
        vcfg.width_margin = sd_cfg.video.width_margin
        vcfg.height_margin = sd_cfg.video.height_margin
        ch3.media_sink_service.CopyFrom(svc_video)
        _logger.info(
            "ServiceDiscovery video: id=%d codec=%s res=%s fps=%s density=%d margins=%dx%d",
            ch3.id,
            sd_cfg.video.codec,
            sd_cfg.video.resolution,
            sd_cfg.video.frame_rate,
            vcfg.density,
            vcfg.width_margin,
            vcfg.height_margin,
        )

    # CH 4: MEDIA AUDIO
    if is_enabled(int(_channels.ChannelId.MEDIA_SINK_MEDIA_AUDIO)):
        ch_id = int(_channels.ChannelId.MEDIA_SINK_MEDIA_AUDIO)
        stream_cfg = sd_cfg.audio_streams.get(ch_id, AudioStreamConfig())
        stream_cfg = _apply_audio_overrides(stream_cfg, base_audio_cfg)
        ch4 = msg.channels.add()
        ch4.id = ch_id
        svc_ma = proto.MediaSinkService_pb2.MediaSinkService()
        svc_ma.available_type = _marshal_enum(
            proto.MediaCodecType_pb2.MediaCodecType,
            _select_enum_value(
                proto.MediaCodecType_pb2.MediaCodecType,
                stream_cfg.codec,
                "MEDIA_CODEC_AUDIO_PCM",
            ),
            "MEDIA_CODEC_AUDIO_PCM",
        )
        svc_ma.audio_type = proto.AudioStreamType_pb2.AudioStreamType.Value("AUDIO_STREAM_MEDIA")
        svc_ma.available_while_in_call = True
        ac = svc_ma.audio_configs.add()
        ac.sampling_rate = stream_cfg.sample_rate
        ac.number_of_bits = stream_cfg.bits
        ac.number_of_channels = stream_cfg.channels
        ch4.media_sink_service.CopyFrom(svc_ma)
        _logger.info(
            "ServiceDiscovery audio(media): id=%d %s %dkHz/%dbit/%dch",
            ch4.id,
            stream_cfg.codec,
            stream_cfg.sample_rate,
            stream_cfg.bits,
            stream_cfg.channels,
        )

    # CH 5: GUIDANCE AUDIO
    if is_enabled(int(_channels.ChannelId.MEDIA_SINK_GUIDANCE_AUDIO)):
        ch_id = int(_channels.ChannelId.MEDIA_SINK_GUIDANCE_AUDIO)
        stream_cfg = sd_cfg.audio_streams.get(ch_id, AudioStreamConfig())
        stream_cfg = _apply_audio_overrides(stream_cfg, base_audio_cfg)
        ch5 = msg.channels.add()
        ch5.id = ch_id
        svc_ga = proto.MediaSinkService_pb2.MediaSinkService()
        svc_ga.available_type = _marshal_enum(
            proto.MediaCodecType_pb2.MediaCodecType,
            _select_enum_value(
                proto.MediaCodecType_pb2.MediaCodecType,
                stream_cfg.codec,
                "MEDIA_CODEC_AUDIO_PCM",
            ),
            "MEDIA_CODEC_AUDIO_PCM",
        )
        svc_ga.audio_type = proto.AudioStreamType_pb2.AudioStreamType.Value("AUDIO_STREAM_GUIDANCE")
        svc_ga.available_while_in_call = True
        ac2 = svc_ga.audio_configs.add()
        ac2.sampling_rate = stream_cfg.sample_rate
        ac2.number_of_bits = stream_cfg.bits
        ac2.number_of_channels = stream_cfg.channels
        ch5.media_sink_service.CopyFrom(svc_ga)
        _logger.info(
            "ServiceDiscovery audio(guidance): id=%d %s %dkHz/%dbit/%dch",
            ch5.id,
            stream_cfg.codec,
            stream_cfg.sample_rate,
            stream_cfg.bits,
            stream_cfg.channels,
        )

    # CH 6: SYSTEM AUDIO
    if is_enabled(int(_channels.ChannelId.MEDIA_SINK_SYSTEM_AUDIO)):
        ch_id = int(_channels.ChannelId.MEDIA_SINK_SYSTEM_AUDIO)
        stream_cfg = sd_cfg.audio_streams.get(ch_id, AudioStreamConfig())
        stream_cfg = _apply_audio_overrides(stream_cfg, base_audio_cfg)
        ch6 = msg.channels.add()
        ch6.id = ch_id
        svc_sa = proto.MediaSinkService_pb2.MediaSinkService()
        svc_sa.available_type = _marshal_enum(
            proto.MediaCodecType_pb2.MediaCodecType,
            _select_enum_value(
                proto.MediaCodecType_pb2.MediaCodecType,
                stream_cfg.codec,
                "MEDIA_CODEC_AUDIO_PCM",
            ),
            "MEDIA_CODEC_AUDIO_PCM",
        )
        svc_sa.audio_type = proto.AudioStreamType_pb2.AudioStreamType.Value("AUDIO_STREAM_SYSTEM_AUDIO")
        svc_sa.available_while_in_call = True
        ac3 = svc_sa.audio_configs.add()
        ac3.sampling_rate = stream_cfg.sample_rate
        ac3.number_of_bits = stream_cfg.bits
        ac3.number_of_channels = stream_cfg.channels
        ch6.media_sink_service.CopyFrom(svc_sa)
        _logger.info(
            "ServiceDiscovery audio(system): id=%d %s %dkHz/%dbit/%dch",
            ch6.id,
            stream_cfg.codec,
            stream_cfg.sample_rate,
            stream_cfg.bits,
            stream_cfg.channels,
        )

    # CH 9: MIC (MEDIA SOURCE)
    if is_enabled(int(_channels.ChannelId.MEDIA_SOURCE_MICROPHONE)):
        ch_id = int(_channels.ChannelId.MEDIA_SOURCE_MICROPHONE)
        stream_cfg = sd_cfg.audio_streams.get(ch_id, AudioStreamConfig())
        stream_cfg = _apply_audio_overrides(stream_cfg, None)
        ch9 = msg.channels.add()
        ch9.id = ch_id
        ch9.media_source_service.available_type = _marshal_enum(
            proto.MediaCodecType_pb2.MediaCodecType,
            _select_enum_value(
                proto.MediaCodecType_pb2.MediaCodecType,
                stream_cfg.codec,
                "MEDIA_CODEC_AUDIO_PCM",
            ),
            "MEDIA_CODEC_AUDIO_PCM",
        )
        ch9.media_source_service.audio_config.sampling_rate = stream_cfg.sample_rate
        ch9.media_source_service.audio_config.number_of_bits = stream_cfg.bits
        ch9.media_source_service.audio_config.number_of_channels = stream_cfg.channels
        _logger.info(
            "ServiceDiscovery mic: id=%d %s %dkHz/%dbit/%dch",
            ch9.id,
            stream_cfg.codec,
            stream_cfg.sample_rate,
            stream_cfg.bits,
            stream_cfg.channels,
        )

    # CH 8: INPUT SOURCE
    if is_enabled(int(_channels.ChannelId.INPUT_SOURCE)):
        ch8 = msg.channels.add()
        ch8.id = int(_channels.ChannelId.INPUT_SOURCE)
        input_svc = proto.InputSourceService_pb2.InputSourceService()
        keycodes = supported_keycodes or []
        for kc in keycodes:
            input_svc.keycodes_supported.append(int(kc))
        # Touchscreen config (use display size for now)
        ts = input_svc.touchscreen.add()
        ts.width = screen_width
        ts.height = screen_height
        if proto.TouchScreenType_pb2 is not None:
            ts.type = proto.TouchScreenType_pb2.TouchScreenType.Value("CAPACITIVE")
        ch8.input_source_service.CopyFrom(input_svc)
        _logger.info(
            "ServiceDiscovery input: id=%d touchscreen=%dx%d keycodes=%d",
            ch8.id,
            ts.width,
            ts.height,
            len(keycodes),
        )

    # CH 12: NAVIGATION
    if is_enabled(int(_channels.ChannelId.NAVIGATION_STATUS)):
        ch12 = msg.channels.add()
        ch12.id = int(_channels.ChannelId.NAVIGATION_STATUS)
        nav_svc = proto.NavigationStatusService_pb2.NavigationStatusService()
        nav_svc.minimum_interval_ms = 1000
        nav_svc.type = proto.NavigationStatusService_pb2.NavigationStatusService.InstrumentClusterType.Value("IMAGE")
        nav_svc.image_options.width = 256
        nav_svc.image_options.height = 256
        nav_svc.image_options.colour_depth_bits = 16
        ch12.navigation_status_service.CopyFrom(nav_svc)

    # CH 10: BLUETOOTH (condizionato)
    if bluetooth_available and is_enabled(int(_channels.ChannelId.BLUETOOTH)):
        ch_bt = msg.channels.add()
        ch_bt.id = int(_channels.ChannelId.BLUETOOTH)
        bt_svc = proto.BluetoothService_pb2.BluetoothService()
        bt_svc.car_address = bt_address
        bt_svc.supported_pairing_methods.append(
            proto.BluetoothPairingMethod_pb2.BluetoothPairingMethod.Value("BLUETOOTH_PAIRING_PIN")
        )
        bt_svc.supported_pairing_methods.append(
            proto.BluetoothPairingMethod_pb2.BluetoothPairingMethod.Value("BLUETOOTH_PAIRING_NUMERIC_COMPARISON")
        )
        ch_bt.bluetooth_service.CopyFrom(bt_svc)

    # CH 11: PHONE STATUS
    if is_enabled(int(_channels.ChannelId.PHONE_STATUS)):
        ch_phone = msg.channels.add()
        ch_phone.id = int(_channels.ChannelId.PHONE_STATUS)
        ch_phone.phone_status_service.CopyFrom(proto.PhoneStatusService_pb2.PhoneStatusService())

    # CH 13: MEDIA PLAYBACK STATUS
    if is_enabled(int(_channels.ChannelId.MEDIA_PLAYBACK_STATUS)):
        ch_mps = msg.channels.add()
        ch_mps.id = int(_channels.ChannelId.MEDIA_PLAYBACK_STATUS)
        ch_mps.media_playback_service.CopyFrom(proto.MediaPlaybackStatusService_pb2.MediaPlaybackStatusService())

    # CH 14: MEDIA BROWSER
    if is_enabled(int(_channels.ChannelId.MEDIA_BROWSER)):
        ch_mb = msg.channels.add()
        ch_mb.id = int(_channels.ChannelId.MEDIA_BROWSER)
        ch_mb.media_browser_service.CopyFrom(proto.MediaBrowserService_pb2.MediaBrowserService())

    # CH 15: VENDOR EXTENSION
    if is_enabled(int(_channels.ChannelId.VENDOR_EXTENSION)):
        ch_ve = msg.channels.add()
        ch_ve.id = int(_channels.ChannelId.VENDOR_EXTENSION)
        ch_ve.vendor_extension_service.CopyFrom(proto.VendorExtensionService_pb2.VendorExtensionService())

    # CH 16: GENERIC NOTIFICATION
    if is_enabled(int(_channels.ChannelId.GENERIC_NOTIFICATION)):
        ch_gn = msg.channels.add()
        ch_gn.id = int(_channels.ChannelId.GENERIC_NOTIFICATION)
        ch_gn.generic_notification_service.CopyFrom(proto.GenericNotificationService_pb2.GenericNotificationService())

    # CH 17: WIFI PROJECTION
    if is_enabled(int(_channels.ChannelId.WIFI_PROJECTION)):
        ch_wifi = msg.channels.add()
        ch_wifi.id = int(_channels.ChannelId.WIFI_PROJECTION)
        wifi_svc = proto.WifiProjectionService_pb2.WifiProjectionService()
        wifi_svc.car_wifi_bssid = ""
        ch_wifi.wifi_projection_service.CopyFrom(wifi_svc)

    # CH 18: RADIO
    if is_enabled(int(_channels.ChannelId.RADIO)):
        ch_radio = msg.channels.add()
        ch_radio.id = int(_channels.ChannelId.RADIO)
        ch_radio.radio_service.CopyFrom(proto.RadioService_pb2.RadioService())

    serialized = msg.SerializeToString()
    return log_and_send(
        f"Invia ServiceDiscoveryResponse ({len(msg.channels)} canali)", serialized
    )
