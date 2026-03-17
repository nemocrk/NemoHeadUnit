"""Application configuration and persistence.

This module provides a single source of truth for runtime settings and service
configuration. It exposes a simple, JSON-backed `ConfigStore` that can be used
across the codebase, and is intended to be the only place where configuration
values are read/written.

The configuration model is intentionally minimal and focused on the needs of the
app (service discovery, audio/video parameters, UI settings, etc.).
"""

from __future__ import annotations

import json
import os
from dataclasses import asdict, dataclass, field
from enum import Enum
from pathlib import Path
from typing import Dict, List, Optional, Any


DEFAULT_CONFIG_DIR = Path.home() / ".nemoheadunit"
DEFAULT_CONFIG_PATH = DEFAULT_CONFIG_DIR / "config.json"

try:
    from app.core.py_logging import get_logger
    _logger = get_logger("app.config")
except Exception:
    _logger = None


class VideoCodec(str, Enum):
    H264 = "H264"
    H265 = "H265"


class VideoResolution(str, Enum):
    R_800x480 = "800x480"
    R_1280x720 = "1280x720"
    R_1920x1080 = "1920x1080"


class VideoFrameRate(str, Enum):
    FPS_30 = "30"
    FPS_60 = "60"


class AudioCodec(str, Enum):
    PCM = "PCM"
    AAC_LC = "AAC_LC"
    OPUS = "OPUS"


# Mapping between our internal enum values and the protobuf enum names.
# These are used to serialize config to the protobuf enum names, while still
# keeping our internal model in the compact names.
_VIDEO_CODEC_TO_PROTO = {
    VideoCodec.H264: "MEDIA_CODEC_VIDEO_H264_BP",
    VideoCodec.H265: "MEDIA_CODEC_VIDEO_H265",
}

_AUDIO_CODEC_TO_PROTO = {
    AudioCodec.PCM: "MEDIA_CODEC_AUDIO_PCM",
    AudioCodec.AAC_LC: "MEDIA_CODEC_AUDIO_AAC_LC",
    AudioCodec.OPUS: "MEDIA_CODEC_AUDIO_OPUS",
}

_VIDEO_RESOLUTION_TO_PROTO = {
    VideoResolution.R_800x480: "VIDEO_800x480",
    VideoResolution.R_1280x720: "VIDEO_1280x720",
    VideoResolution.R_1920x1080: "VIDEO_1920x1080",
}

_VIDEO_FRAMERATE_TO_PROTO = {
    VideoFrameRate.FPS_30: "VIDEO_FPS_30",
    VideoFrameRate.FPS_60: "VIDEO_FPS_60",
}


def _find_matching_key(value: str, options: list[str]) -> str:
    """Find the best match among options for value.

    This supports backward-compatible values like "H264" or "30" being mapped
    to their protobuf enum equivalents.
    """

    if not value:
        return ""

    normalized = value.strip().upper().replace(" ", "_").replace("-", "_")
    if normalized in options:
        return normalized

    for opt in options:
        if normalized in opt.upper():
            return opt

    # Try stripping common prefixes
    stripped = normalized
    for prefix in ["MEDIA_CODEC_VIDEO_", "MEDIA_CODEC_AUDIO_", "VIDEO_", "AUDIO_"]:
        if stripped.startswith(prefix):
            stripped = stripped[len(prefix):]
    for opt in options:
        if stripped in opt.upper():
            return opt

    return ""


def video_codec_to_proto(codec: VideoCodec) -> str:
    return _VIDEO_CODEC_TO_PROTO.get(codec, "MEDIA_CODEC_VIDEO_H264_BP")


def audio_codec_to_proto(codec: AudioCodec) -> str:
    return _AUDIO_CODEC_TO_PROTO.get(codec, "MEDIA_CODEC_AUDIO_PCM")


def video_resolution_to_proto(res: VideoResolution) -> str:
    return _VIDEO_RESOLUTION_TO_PROTO.get(res, "VIDEO_800x480")


def video_frame_rate_to_proto(rate: VideoFrameRate) -> str:
    return _VIDEO_FRAMERATE_TO_PROTO.get(rate, "VIDEO_FPS_30")


def proto_to_video_codec(proto_key: str) -> VideoCodec:
    key = _find_matching_key(proto_key, list(_VIDEO_CODEC_TO_PROTO.values()))
    for k, v in _VIDEO_CODEC_TO_PROTO.items():
        if v == key:
            return k
    return VideoCodec.H264


def proto_to_audio_codec(proto_key: str) -> AudioCodec:
    key = _find_matching_key(proto_key, list(_AUDIO_CODEC_TO_PROTO.values()))
    for k, v in _AUDIO_CODEC_TO_PROTO.items():
        if v == key:
            return k
    return AudioCodec.PCM


def proto_to_video_resolution(proto_key: str) -> VideoResolution:
    key = _find_matching_key(proto_key, list(_VIDEO_RESOLUTION_TO_PROTO.values()))
    for k, v in _VIDEO_RESOLUTION_TO_PROTO.items():
        if v == key:
            return k
    return VideoResolution.R_800x480


def proto_to_video_frame_rate(proto_key: str) -> VideoFrameRate:
    key = _find_matching_key(proto_key, list(_VIDEO_FRAMERATE_TO_PROTO.values()))
    for k, v in _VIDEO_FRAMERATE_TO_PROTO.items():
        if v == key:
            return k
    return VideoFrameRate.FPS_30


class LogLevel(str, Enum):
    DEBUG = "DEBUG"
    INFO = "INFO"
    WARNING = "WARNING"
    ERROR = "ERROR"
    CRITICAL = "CRITICAL"


@dataclass
class AudioStreamConfig:
    codec: AudioCodec = AudioCodec.PCM
    sample_rate: int = 16000
    bits: int = 16
    channels: int = 1


@dataclass
class VideoConfig:
    codec: VideoCodec = VideoCodec.H264
    resolution: VideoResolution = VideoResolution.R_800x480
    frame_rate: VideoFrameRate = VideoFrameRate.FPS_30
    density: int = 140
    width_margin: int = 0
    height_margin: int = 0


@dataclass
class UiConfig:
    instance_name: str = "Nemo"
    mode: str = "USB"  # or "WiFi"
    bt_address: str = ""


@dataclass
class ServiceDiscoveryConfig:
    enabled_channels: List[int] = field(default_factory=list)
    # Map channel id to audio configuration. Keys should be strings in JSON.
    audio_streams: Dict[int, AudioStreamConfig] = field(default_factory=dict)
    video: VideoConfig = field(default_factory=VideoConfig)


@dataclass
class LoggingConfig:
    core_level: LogLevel = LogLevel.INFO
    aasdk_level: LogLevel = LogLevel.INFO


@dataclass
class AudioOutputConfig:
    sink: str = "pulsesink"
    buffer_ms: int = 100
    max_queue_frames: int = 10
    mic_frame_ms: int = 20
    mic_batch_ms: int = 100
    volume_backend: str = "pactl"
    volume_step: int = 5
    volume_device: str = ""
    volume_percent: int = 50
    muted: bool = False


@dataclass
class AppConfig:
    ui: UiConfig = field(default_factory=UiConfig)
    service_discovery: ServiceDiscoveryConfig = field(default_factory=ServiceDiscoveryConfig)
    logging: LoggingConfig = field(default_factory=LoggingConfig)
    audio_output: AudioOutputConfig = field(default_factory=AudioOutputConfig)


class ConfigStore:
    """Persistent config store.

    Loads defaults if no file exists. Saves updates to disk when requested.
    """

    def __init__(self, path: Optional[Path] = None):
        self.path = Path(path or DEFAULT_CONFIG_PATH)
        self.config = AppConfig()
        if _logger:
            _logger.info("ConfigStore path: %s", self.path)
        self.load()

    def _ensure_dir(self):
        if not self.path.parent.exists():
            self.path.parent.mkdir(parents=True, exist_ok=True)

    def load(self) -> None:
        if not self.path.exists():
            if _logger:
                _logger.warning("ConfigStore load skipped (missing): %s", self.path)
            return
        try:
            with self.path.open("r", encoding="utf-8") as f:
                data = json.load(f)
            self.config = self._from_dict(data)
            if _logger:
                _logger.info(
                    "ConfigStore loaded: audio_output.buffer_ms=%s max_queue_frames=%s mic_frame_ms=%s mic_batch_ms=%s",
                    getattr(self.config.audio_output, "buffer_ms", None),
                    getattr(self.config.audio_output, "max_queue_frames", None),
                    getattr(self.config.audio_output, "mic_frame_ms", None),
                    getattr(self.config.audio_output, "mic_batch_ms", None),
                )
        except Exception:
            # Ignore failures and keep defaults, but do not crash.
            if _logger:
                _logger.exception("ConfigStore load failed: %s", self.path)
            pass

    def save(self) -> None:
        try:
            self._ensure_dir()
            with self.path.open("w", encoding="utf-8") as f:
                json.dump(self._to_dict(), f, indent=2, sort_keys=True)
        except Exception:
            pass

    def _to_dict(self) -> Dict[str, Any]:
        d = asdict(self.config)

        # Convert service discovery codec values into protobuf enum names
        if isinstance(d.get("service_discovery"), dict):
            sd = d["service_discovery"]
            # Video config
            video = sd.get("video")
            if isinstance(video, dict):
                video_codec = video.get("codec")
                if isinstance(video_codec, str):
                    video["codec"] = video_codec_to_proto(proto_to_video_codec(video_codec))
                res = video.get("resolution")
                if isinstance(res, str):
                    video["resolution"] = video_resolution_to_proto(proto_to_video_resolution(res))
                fr = video.get("frame_rate")
                if isinstance(fr, str):
                    video["frame_rate"] = video_frame_rate_to_proto(proto_to_video_frame_rate(fr))

            # Audio stream codecs
            streams = sd.get("audio_streams")
            if isinstance(streams, dict):
                for k, v in streams.items():
                    if isinstance(v, dict) and isinstance(v.get("codec"), str):
                        try:
                            codec_enum = AudioCodec(v["codec"])
                        except Exception:
                            codec_enum = proto_to_audio_codec(v["codec"])
                        v["codec"] = audio_codec_to_proto(codec_enum)

            # Convert audio_streams keys to str for JSON serialization
            if isinstance(streams, dict):
                sd["audio_streams"] = {str(k): v for k, v in streams.items()}

        return d

    def _from_dict(self, data: Dict[str, Any]) -> AppConfig:
        # Map JSON dict back to AppConfig/data classes.
        def _safe_enum(enum_cls, value, default):
            try:
                return enum_cls(value)
            except (ValueError, TypeError):
                return default

        cfg = AppConfig()
        try:
            ui = data.get("ui") or {}
            cfg.ui = UiConfig(
                instance_name=ui.get("instance_name", cfg.ui.instance_name),
                mode=ui.get("mode", cfg.ui.mode),
                bt_address=ui.get("bt_address", cfg.ui.bt_address),
            )
        except Exception:
            pass

        try:
            sd = data.get("service_discovery") or {}
            enabled = sd.get("enabled_channels") or []
            vid = sd.get("video") or {}
            cfg.service_discovery.enabled_channels = list(enabled)
            cfg.service_discovery.video = VideoConfig(
                codec=proto_to_video_codec(vid.get("codec", cfg.service_discovery.video.codec.value)),
                resolution=proto_to_video_resolution(vid.get("resolution", cfg.service_discovery.video.resolution.value)),
                frame_rate=proto_to_video_frame_rate(vid.get("frame_rate", cfg.service_discovery.video.frame_rate.value)),
                density=int(vid.get("density", cfg.service_discovery.video.density)),
                width_margin=int(vid.get("width_margin", cfg.service_discovery.video.width_margin)),
                height_margin=int(vid.get("height_margin", cfg.service_discovery.video.height_margin)),
            )
            streams = sd.get("audio_streams") or {}
            for k, v in streams.items():
                try:
                    ch = int(k)
                except Exception:
                    continue
                cfg.service_discovery.audio_streams[ch] = AudioStreamConfig(
                    codec=proto_to_audio_codec(v.get("codec", cfg.service_discovery.audio_streams.get(ch, AudioStreamConfig()).codec.value)),
                    sample_rate=int(v.get("sample_rate", 16000)),
                    bits=int(v.get("bits", 16)),
                    channels=int(v.get("channels", 1)),
                )
        except Exception:
            pass

        try:
            log = data.get("logging") or {}
            cfg.logging = LoggingConfig(
                core_level=_safe_enum(LogLevel, log.get("core_level"), cfg.logging.core_level),
                aasdk_level=_safe_enum(LogLevel, log.get("aasdk_level"), cfg.logging.aasdk_level),
            )
        except Exception:
            pass

        try:
            audio_out = data.get("audio_output") or {}
            cfg.audio_output = AudioOutputConfig(
                sink=str(audio_out.get("sink", cfg.audio_output.sink)),
                buffer_ms=int(audio_out.get("buffer_ms", cfg.audio_output.buffer_ms)),
                max_queue_frames=int(audio_out.get("max_queue_frames", cfg.audio_output.max_queue_frames)),
                mic_frame_ms=int(audio_out.get("mic_frame_ms", cfg.audio_output.mic_frame_ms)),
                mic_batch_ms=int(audio_out.get("mic_batch_ms", cfg.audio_output.mic_batch_ms)),
                volume_backend=str(audio_out.get("volume_backend", cfg.audio_output.volume_backend)),
                volume_step=int(audio_out.get("volume_step", cfg.audio_output.volume_step)),
                volume_device=str(audio_out.get("volume_device", cfg.audio_output.volume_device)),
                volume_percent=int(audio_out.get("volume_percent", cfg.audio_output.volume_percent)),
                muted=bool(audio_out.get("muted", cfg.audio_output.muted)),
            )
        except Exception:
            pass

        return cfg


# Global default store
config_store = ConfigStore()


def get_config() -> AppConfig:
    return config_store.config


def normalize_enum_value(candidate: str, valid_values: list[str], default: str) -> str:
    """Normalize a candidate value against a set of valid enum keys.

    This is intended to allow backwards-compatible config values (e.g. "H264"
    or "h264") to be mapped to canonical protobuf enum names such as
    "MEDIA_CODEC_VIDEO_H264_BP".

    Args:
        candidate: The incoming value to normalize.
        valid_values: List of allowed values (case-sensitive) to choose from.
        default: The default value to return if no match is found.
    """

    if not candidate:
        return default

    normalized = candidate.strip().upper().replace(" ", "_").replace("-", "_")

    # Direct match
    if normalized in valid_values:
        return normalized

    # Look for substring matches to handle short names (H264 -> MEDIA_CODEC_VIDEO_H264_BP)
    for v in valid_values:
        if normalized in v.upper():
            return v

    # Strip common prefixes and try again
    stripped = normalized
    for prefix in ["MEDIA_CODEC_VIDEO_", "MEDIA_CODEC_AUDIO_", "VIDEO_", "AUDIO_"]:
        if stripped.startswith(prefix):
            stripped = stripped[len(prefix):]
    for v in valid_values:
        if stripped in v.upper():
            return v

    return default


def save_config() -> None:
    config_store.save()


def apply_audio_env() -> None:
    cfg = get_config().audio_output
    if cfg.sink:
        os.environ["NEMO_AUDIO_SINK"] = cfg.sink
    if cfg.buffer_ms:
        os.environ["NEMO_AUDIO_BUFFER_MS"] = str(cfg.buffer_ms)


def _run_cmd(args: list[str]) -> None:
    try:
        import subprocess

        subprocess.run(args, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception:
        pass


def _run_cmd_output(args: list[str]) -> str:
    try:
        import subprocess

        out = subprocess.check_output(args, stderr=subprocess.DEVNULL, text=True)
        return out.strip()
    except Exception:
        return ""


def system_volume_get() -> tuple[int | None, bool]:
    cfg = get_config().audio_output
    device = cfg.volume_device.strip() if cfg.volume_device else ""
    backend = (cfg.volume_backend or "pactl").lower()

    if backend == "wpctl":
        target = device or "@DEFAULT_AUDIO_SINK@"
        out = _run_cmd_output(["wpctl", "get-volume", target])
        # Example: "Volume: 0.50 [MUTED]"
        if not out:
            return None, False
        muted = "MUTED" in out.upper()
        parts = out.replace("Volume:", "").strip().split()
        try:
            vol = float(parts[0])
            return int(round(vol * 100)), muted
        except Exception:
            return None, muted

    sink = device or "@DEFAULT_SINK@"
    out = _run_cmd_output(["pactl", "get-sink-volume", sink])
    mute_out = _run_cmd_output(["pactl", "get-sink-mute", sink])
    muted = "yes" in mute_out.lower()
    if not out:
        return None, muted
    import re

    match = re.search(r"(\d+)%", out)
    if not match:
        return None, muted
    return int(match.group(1)), muted


def system_volume_set(percent: int) -> None:
    cfg = get_config().audio_output
    device = cfg.volume_device.strip() if cfg.volume_device else ""
    backend = (cfg.volume_backend or "pactl").lower()
    pct = max(0, min(150, int(percent)))

    if backend == "wpctl":
        target = device or "@DEFAULT_AUDIO_SINK@"
        _run_cmd(["wpctl", "set-volume", target, f"{pct}%"])
    else:
        sink = device or "@DEFAULT_SINK@"
        _run_cmd(["pactl", "set-sink-volume", sink, f"{pct}%"])

    cfg.volume_percent = pct
    save_config()


def sync_system_volume_to_config() -> None:
    cfg = get_config().audio_output
    vol, muted = system_volume_get()
    changed = False
    if vol is not None and vol != cfg.volume_percent:
        cfg.volume_percent = vol
        changed = True
    if muted != cfg.muted:
        cfg.muted = muted
        changed = True
    if changed:
        save_config()


def system_volume_change(delta_percent: int) -> None:
    cfg = get_config().audio_output
    step = delta_percent
    device = cfg.volume_device.strip() if cfg.volume_device else ""
    backend = (cfg.volume_backend or "pactl").lower()

    if backend == "wpctl":
        target = device or "@DEFAULT_AUDIO_SINK@"
        sign = "+" if step >= 0 else "-"
        _run_cmd(["wpctl", "set-volume", target, f"{abs(step)}%{sign}"])
    else:
        sink = device or "@DEFAULT_SINK@"
        sign = "+" if step >= 0 else "-"
        _run_cmd(["pactl", "set-sink-volume", sink, f"{abs(step)}%{sign}"])
    sync_system_volume_to_config()


def system_volume_toggle_mute() -> None:
    cfg = get_config().audio_output
    device = cfg.volume_device.strip() if cfg.volume_device else ""
    backend = (cfg.volume_backend or "pactl").lower()

    if backend == "wpctl":
        target = device or "@DEFAULT_AUDIO_SINK@"
        _run_cmd(["wpctl", "set-mute", target, "toggle"])
    else:
        sink = device or "@DEFAULT_SINK@"
        _run_cmd(["pactl", "set-sink-mute", sink, "toggle"])
    sync_system_volume_to_config()
