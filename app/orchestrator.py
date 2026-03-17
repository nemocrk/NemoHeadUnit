"""
Orchestrator.
Owns runtime wiring: runner, USB discovery, transport stack, channel manager.
"""

from __future__ import annotations

import time
import threading

from app.nemo_core import init_logging, _LOG_ENV, _LOG_ENV_AASDK, io_context
from app.core.py_logging import get_logger
from app.core import UsbHubManager, TransportStack, ChannelManager
from handlers.control.control_event_handler import ControlEventHandlerLogic
from handlers.audio.audio_event_handler import AudioEventHandlerLogic
from handlers.video.video_event_handler import VideoEventHandlerLogic, VideoOrchestrator
from handlers.sensor.sensor_event_handler import SensorEventHandlerLogic
from handlers.input.input_event_handler import InputEventHandlerLogic, InputOrchestrator, default_supported_keycodes
from handlers.navigation.navigation_event_handler import NavigationEventHandlerLogic
from handlers.bluetooth.bluetooth_event_handler import BluetoothEventHandlerLogic
from handlers.generic_notification.generic_notification_event_handler import GenericNotificationEventHandlerLogic
from handlers.media_browser.media_browser_event_handler import MediaBrowserEventHandlerLogic
from handlers.media_playback_status.media_playback_status_event_handler import MediaPlaybackStatusEventHandlerLogic
from handlers.media_source.media_source_event_handler import MediaSourceEventHandlerLogic
from handlers.phone_status.phone_status_event_handler import PhoneStatusEventHandlerLogic
from handlers.radio.radio_event_handler import RadioEventHandlerLogic
from handlers.vendor_extension.vendor_extension_event_handler import VendorExtensionEventHandlerLogic
from handlers.wifi_projection.wifi_projection_event_handler import WifiProjectionEventHandlerLogic

_logger = get_logger("app.orchestrator")

class Orchestrator:
    def __init__(
        self,
        screen_width: int = 800,
        screen_height: int = 480,
        bluetooth_available: bool = False,
        bt_address: str = "",
        channels=None,
        runner=None,
        av_core=None,
        on_aa_active_changed=None,
        supported_keycodes=None,
    ):
        self.screen_width = screen_width
        self.screen_height = screen_height
        self.bluetooth_available = bluetooth_available
        self.bt_address = bt_address

        self.channels = channels
        self._runner = runner
        self._av_core = av_core
        self._on_aa_active_changed = on_aa_active_changed
        self._supported_keycodes = supported_keycodes or []
        self._hub = None
        self._transport = None
        self._channel_manager = None
        self._input_logic = None
        self._stopping = False
        self._restart_timer = None

    # ------------------------------------------------------------------
    # Lifecycle helpers
    # ------------------------------------------------------------------
    def start_runner(self):
        init_logging()
        _logger.info("AASDK loaded. Set %s=trace|debug|info|warn|error to configure logging.", _LOG_ENV_AASDK)
        _logger.info("Core loaded. Set %s=trace|debug|info|warn|error to configure logging.", _LOG_ENV)
        if self._runner is None:
            self._runner = io_context.IoContextRunner()
        self._runner.start()
        return self._runner

    def start_usb(self):
        if self._runner is None:
            raise RuntimeError("start_runner() must be called before start_usb().")
        if self._hub is not None:
            return self._hub
        self._hub = UsbHubManager(self._runner.get_io_context_ptr())
        self._hub.start(self.create_on_device_handler(), self._on_error)
        _logger.info("USB discovery started (aasdk_usb).")
        return self._hub

    def create_on_device_handler(self):
        def on_device(handle):
            try:
                _logger.info("USB AOAP device discovered.")
                aoap = self._hub.create_aoap_device(handle)
                _logger.info("USB AOAP device created. Handoff to transport layer.")

                self._transport = self.create_transport_stack(aoap)
                _logger.info("Transport/Messenger ready.")

                self._channel_manager = self.create_channel_manager(
                    messenger=self._transport.messenger,
                    cryptor=self._transport.cryptor,
                )
                self.start_channels()
                _logger.info("Channels started (Control + Optional).")
            except Exception as exc:
                _logger.error("USB device setup error: %s", exc)
                self._restart_usb(1.0)

        return on_device

    def create_transport_stack(self, aoap_device):
        return TransportStack(self._runner.get_io_context_ptr(), aoap_device)

    def create_channel_manager(self, messenger, cryptor=None):
        control_logic, handlers_by_channel = self._build_handlers()
        cm = ChannelManager(
            self._runner.get_io_context_ptr(),
            messenger,
            cryptor=cryptor,
            control_logic=control_logic,
            handlers_by_channel=handlers_by_channel,
            channel_strands=self._normalize_channel_strands(),
            av_core=self._av_core,
        )
        return cm

    def start_channels(self):
        if self._channel_manager is None:
            raise RuntimeError("ChannelManager not created.")
        self._channel_manager.start()

    def run_forever(self):
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            _logger.info("Stop requested.")
            self.stop()

    def stop(self):
        if self._stopping:
            return
        self._stopping = True
        if self._restart_timer is not None:
            try:
                self._restart_timer.cancel()
            except Exception:
                pass
            self._restart_timer = None
        if self._channel_manager is not None:
            try:
                try:
                    self._channel_manager.request_shutdown()
                    time.sleep(0.2)
                except Exception:
                    pass
                self._channel_manager.stop()
            except Exception:
                pass
        if self._hub is not None:
            try:
                self._hub.stop()
            except Exception:
                pass
        if self._transport is not None:
            self._transport.stop()
            self._transport = None
        if self._av_core is not None:
            try:
                self._av_core.stop()
            except Exception:
                pass
        if self._runner is not None:
            self._runner.stop()
        if self._hub is not None:
            try:
                self._hub.hard_teardown()
            except Exception:
                pass
            self._hub = None
        self._stopping = False

    # ------------------------------------------------------------------
    # Internals
    # ------------------------------------------------------------------
    def _build_handlers(self):
        control = ControlEventHandlerLogic(
            screen_width=self.screen_width,
            screen_height=self.screen_height,
            bluetooth_available=self.bluetooth_available,
            bt_address=self.bt_address,
            supported_keycodes=self._supported_keycodes,
        )

        enabled = self._normalize_enabled_channels()
        handlers_by_channel = {}

        audio_logic = None
        video_logic = None
        sensor_logic = None
        input_logic = None
        navigation_logic = None
        bluetooth_logic = None
        generic_notification_logic = None
        media_browser_logic = None
        media_playback_status_logic = None
        media_source_logic = None
        phone_status_logic = None
        radio_logic = None
        vendor_extension_logic = None
        wifi_projection_logic = None

        try:
            import channels as _channels
            audio_ids = {
                int(_channels.ChannelId.MEDIA_SINK_MEDIA_AUDIO),
                int(_channels.ChannelId.MEDIA_SINK_GUIDANCE_AUDIO),
                int(_channels.ChannelId.MEDIA_SINK_SYSTEM_AUDIO),
            }
            video_id = int(_channels.ChannelId.MEDIA_SINK_VIDEO)
            sensor_id = int(_channels.ChannelId.SENSOR)
            input_id = int(_channels.ChannelId.INPUT_SOURCE)
            nav_id = int(_channels.ChannelId.NAVIGATION_STATUS)
            bluetooth_id = int(_channels.ChannelId.BLUETOOTH)
            generic_notification_id = int(_channels.ChannelId.GENERIC_NOTIFICATION)
            media_browser_id = int(_channels.ChannelId.MEDIA_BROWSER)
            media_playback_status_id = int(_channels.ChannelId.MEDIA_PLAYBACK_STATUS)
            media_source_id = int(_channels.ChannelId.MEDIA_SOURCE_MICROPHONE)
            phone_status_id = int(_channels.ChannelId.PHONE_STATUS)
            radio_id = int(_channels.ChannelId.RADIO)
            vendor_extension_id = int(_channels.ChannelId.VENDOR_EXTENSION)
            wifi_projection_id = int(_channels.ChannelId.WIFI_PROJECTION)
        except Exception:
            audio_ids = set()
            video_id = None
            sensor_id = None
            input_id = None
            nav_id = None
            bluetooth_id = None
            generic_notification_id = None
            media_browser_id = None
            media_playback_status_id = None
            media_source_id = None
            phone_status_id = None
            radio_id = None
            vendor_extension_id = None
            wifi_projection_id = None

        for ch in enabled:
            ch_id = int(ch)
            if ch_id in audio_ids:
                if audio_logic is None:
                    audio_logic = AudioEventHandlerLogic()
                handlers_by_channel[ch_id] = audio_logic
            elif video_id is not None and ch_id == video_id:
                if video_logic is None:
                    video_logic = VideoEventHandlerLogic(
                        orchestrator=VideoOrchestrator(
                            on_active_changed=self._on_aa_active_changed
                        )
                    )
                handlers_by_channel[ch_id] = video_logic
            elif sensor_id is not None and ch_id == sensor_id:
                if sensor_logic is None:
                    sensor_logic = SensorEventHandlerLogic()
                handlers_by_channel[ch_id] = sensor_logic
            elif input_id is not None and ch_id == input_id:
                if input_logic is None:
                    input_logic = InputEventHandlerLogic(
                        orchestrator=InputOrchestrator(
                            supported_keycodes=self._supported_keycodes
                        )
                    )
                handlers_by_channel[ch_id] = input_logic
            elif nav_id is not None and ch_id == nav_id:
                if navigation_logic is None:
                    navigation_logic = NavigationEventHandlerLogic()
                handlers_by_channel[ch_id] = navigation_logic
            elif bluetooth_id is not None and ch_id == bluetooth_id:
                if bluetooth_logic is None:
                    bluetooth_logic = BluetoothEventHandlerLogic()
                handlers_by_channel[ch_id] = bluetooth_logic
            elif generic_notification_id is not None and ch_id == generic_notification_id:
                if generic_notification_logic is None:
                    generic_notification_logic = GenericNotificationEventHandlerLogic()
                handlers_by_channel[ch_id] = generic_notification_logic
            elif media_browser_id is not None and ch_id == media_browser_id:
                if media_browser_logic is None:
                    media_browser_logic = MediaBrowserEventHandlerLogic()
                handlers_by_channel[ch_id] = media_browser_logic
            elif media_playback_status_id is not None and ch_id == media_playback_status_id:
                if media_playback_status_logic is None:
                    media_playback_status_logic = MediaPlaybackStatusEventHandlerLogic()
                handlers_by_channel[ch_id] = media_playback_status_logic
            elif media_source_id is not None and ch_id == media_source_id:
                if media_source_logic is None:
                    media_source_logic = MediaSourceEventHandlerLogic()
                handlers_by_channel[ch_id] = media_source_logic
            elif phone_status_id is not None and ch_id == phone_status_id:
                if phone_status_logic is None:
                    phone_status_logic = PhoneStatusEventHandlerLogic()
                handlers_by_channel[ch_id] = phone_status_logic
            elif radio_id is not None and ch_id == radio_id:
                if radio_logic is None:
                    radio_logic = RadioEventHandlerLogic()
                handlers_by_channel[ch_id] = radio_logic
            elif vendor_extension_id is not None and ch_id == vendor_extension_id:
                if vendor_extension_logic is None:
                    vendor_extension_logic = VendorExtensionEventHandlerLogic()
                handlers_by_channel[ch_id] = vendor_extension_logic
            elif wifi_projection_id is not None and ch_id == wifi_projection_id:
                if wifi_projection_logic is None:
                    wifi_projection_logic = WifiProjectionEventHandlerLogic()
                handlers_by_channel[ch_id] = wifi_projection_logic

        self._input_logic = input_logic
        return control, handlers_by_channel

    def get_input_logic(self):
        return self._input_logic

    def _normalize_enabled_channels(self):
        if self.channels is not None:
            enabled = []
            for item in self.channels:
                if isinstance(item, (list, tuple)):
                    if len(item) == 0:
                        continue
                    enabled.append(item[0])
                else:
                    enabled.append(item)
            return enabled
        return []

    def _normalize_channel_strands(self):
        if self.channels is None:
            return {}
        pairs = list(self.channels)
        normalized = {}
        for item in pairs:
            if isinstance(item, (list, tuple)):
                if len(item) == 1:
                    ch, strand_name = item[0], None
                elif len(item) == 2:
                    ch, strand_name = item
                else:
                    raise ValueError("channels entries must be ChannelId or (ChannelId, strand_name).")
            else:
                ch, strand_name = item, None
            normalized[int(ch)] = strand_name
        return normalized

    def _on_error(self, msg):
        _logger.error("USB discovery error: %s", msg)
        self._restart_usb(1.0)

    def _restart_usb(self, delay_s: float):
        if self._stopping:
            return
        if self._restart_timer is not None:
            return
        if self._channel_manager is not None:
            try:
                self._channel_manager.stop()
            except Exception:
                pass
            self._channel_manager = None
        if self._transport is not None:
            try:
                self._transport.stop()
            except Exception:
                pass
            self._transport = None
        if self._hub is not None:
            try:
                self._hub.hard_teardown()
            except Exception:
                try:
                    self._hub.stop()
                except Exception:
                    pass
            self._hub = None

        def _do_restart():
            self._restart_timer = None
            if self._stopping:
                return
            try:
                self.start_usb()
            except Exception as exc:
                _logger.error("USB restart failed: %s", exc)
                self._restart_usb(delay_s)

        self._restart_timer = threading.Timer(delay_s, _do_restart)
        self._restart_timer.daemon = True
        self._restart_timer.start()
