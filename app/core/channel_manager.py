"""
Channel manager: constructs channels and wires C++ handlers to Python logic.
Channels are created only for handlers provided by the orchestrator.
"""

try:
    import channels as _channels
    import event as _event
    import control_event as _control_event
    import audio_event as _audio_event
    import video_event as _video_event
    import sensor_event as _sensor_event
    import input_event as _input_event
    import navigation_event as _navigation_event
    import bluetooth_event as _bluetooth_event
    import generic_notification_event as _generic_notification_event
    import media_browser_event as _media_browser_event
    import media_playback_status_event as _media_playback_status_event
    import media_source_event as _media_source_event
    import phone_status_event as _phone_status_event
    import radio_event as _radio_event
    import vendor_extension_event as _vendor_extension_event
    import wifi_projection_event as _wifi_projection_event
except Exception as e:
    raise ImportError("Native modules for channels/event handlers not available.") from e

from app.core.py_logging import get_logger

_logger = get_logger("app.channel_manager")



class ChannelManager:
    def __init__(
        self,
        io_context_ptr: int,
        messenger,
        cryptor=None,
        control_logic=None,
        handlers_by_channel=None,
        channel_strands=None,
        av_core=None,
    ):
        self._io_context_ptr = io_context_ptr
        self._messenger = messenger
        self._cryptor = cryptor

        self._default_strand = _event.Strand(self._io_context_ptr)
        self._strand_name_by_channel = channel_strands or {}
        self._strand_by_name = {}
        self._bindings = []
        self._handlers = []
        self._channels = []
        self._control_channel = None
        self._control_strand = None

        self._control_logic = control_logic
        if self._control_logic is None:
            raise RuntimeError("ChannelManager requires a control handler.")

        self._handlers_by_channel = handlers_by_channel or {}
        self._av_core = av_core
        self._av_core_ptr = 0
        if self._av_core is not None and hasattr(self._av_core, "ptr"):
            try:
                self._av_core_ptr = int(self._av_core.ptr())
            except Exception:
                self._av_core_ptr = 0

        if self._cryptor is not None and hasattr(self._control_logic, "set_cryptor"):
            self._control_logic.set_cryptor(self._cryptor)

        self._enabled_channels = {int(ch) for ch in self._handlers_by_channel.keys()}
        if hasattr(self._control_logic, "set_enabled_channels"):
            self._control_logic.set_enabled_channels(self._enabled_channels)

    @property
    def strand(self):
        return self._default_strand

    def _bind(self, logic):
        binding = _event.EventBinding(logic)
        self._bindings.append(binding)
        return binding

    def _keep(self, obj):
        self._handlers.append(obj)
        return obj

    def _get_strand(self, ch_id=None):
        if ch_id is None:
            return self._default_strand
        name = self._strand_name_by_channel.get(int(ch_id))
        if not name:
            return self._default_strand
        strand = self._strand_by_name.get(name)
        if strand is None:
            strand = _event.Strand(self._io_context_ptr)
            self._strand_by_name[name] = strand
        return strand

    def _is_enabled(self, ch_id):
        return int(ch_id) in self._enabled_channels

    def start(self):
        # Control channel
        control_strand = self._get_strand(_channels.ChannelId.CONTROL)
        control_channel = _control_event.ControlChannel(control_strand, self._messenger)
        control_binding = self._bind(self._control_logic)
        control_handler = _control_event.ControlEventHandler(control_strand, control_channel, control_binding)
        control_channel.receive(control_handler)
        control_channel.send_version_request(control_strand)
        self._channels.append(control_channel)
        self._keep(control_handler)
        self._control_channel = control_channel
        self._control_strand = control_strand

        # Video channel
        if self._is_enabled(_channels.ChannelId.MEDIA_SINK_VIDEO):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.MEDIA_SINK_VIDEO))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.MEDIA_SINK_VIDEO)
                video_channel = _video_event.VideoChannel(
                    strand,
                    self._messenger,
                    _channels.ChannelId.MEDIA_SINK_VIDEO,
                )
                video_binding = self._bind(logic)
                video_handler = _video_event.VideoEventHandler(strand, video_channel, video_binding, self._av_core_ptr)
                video_channel.receive(video_handler)
                self._channels.append(video_channel)
                self._keep(video_handler)

        # Audio channels (media, guidance, system, mic)
        audio_ids = [
            _channels.ChannelId.MEDIA_SINK_MEDIA_AUDIO,
            _channels.ChannelId.MEDIA_SINK_GUIDANCE_AUDIO,
            _channels.ChannelId.MEDIA_SINK_SYSTEM_AUDIO,
        ]
        for ch_id in audio_ids:
            if not self._is_enabled(ch_id):
                continue
            logic = self._handlers_by_channel.get(int(ch_id))
            if logic is None:
                continue
            strand = self._get_strand(ch_id)
            ch = _audio_event.AudioChannel(strand, self._messenger, ch_id)
            binding = self._bind(logic)
            handler = _audio_event.AudioEventHandler(strand, ch, binding, self._av_core_ptr)
            ch.receive(handler)
            self._channels.append(ch)
            self._keep(handler)

        # Sensor channel
        if self._is_enabled(_channels.ChannelId.SENSOR):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.SENSOR))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.SENSOR)
                sensor_channel = _sensor_event.SensorChannel(strand, self._messenger)
                sensor_binding = self._bind(logic)
                sensor_handler = _sensor_event.SensorEventHandler(strand, sensor_channel, sensor_binding)
                sensor_channel.receive(sensor_handler)
                self._channels.append(sensor_channel)
                self._keep(sensor_handler)

        # Input channel
        if self._is_enabled(_channels.ChannelId.INPUT_SOURCE):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.INPUT_SOURCE))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.INPUT_SOURCE)
                input_channel = _input_event.InputChannel(strand, self._messenger)
                input_binding = self._bind(logic)
                input_handler = _input_event.InputEventHandler(strand, input_channel, input_binding)
                input_channel.receive(input_handler)
                self._channels.append(input_channel)
                self._keep(input_handler)

        # Navigation channel
        if self._is_enabled(_channels.ChannelId.NAVIGATION_STATUS):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.NAVIGATION_STATUS))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.NAVIGATION_STATUS)
                nav_channel = _navigation_event.NavigationChannel(strand, self._messenger)
                nav_binding = self._bind(logic)
                nav_handler = _navigation_event.NavigationEventHandler(strand, nav_channel, nav_binding)
                nav_channel.receive(nav_handler)
                self._channels.append(nav_channel)
                self._keep(nav_handler)

        # Bluetooth channel
        if self._is_enabled(_channels.ChannelId.BLUETOOTH):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.BLUETOOTH))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.BLUETOOTH)
                bt_channel = _bluetooth_event.BluetoothChannel(strand, self._messenger)
                bt_binding = self._bind(logic)
                bt_handler = _bluetooth_event.BluetoothEventHandler(strand, bt_channel, bt_binding)
                bt_channel.receive(bt_handler)
                self._channels.append(bt_channel)
                self._keep(bt_handler)

        # Generic Notification channel
        if self._is_enabled(_channels.ChannelId.GENERIC_NOTIFICATION):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.GENERIC_NOTIFICATION))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.GENERIC_NOTIFICATION)
                gn_channel = _generic_notification_event.GenericNotificationChannel(strand, self._messenger)
                gn_binding = self._bind(logic)
                gn_handler = _generic_notification_event.GenericNotificationEventHandler(strand, gn_channel, gn_binding)
                gn_channel.receive(gn_handler)
                self._channels.append(gn_channel)
                self._keep(gn_handler)

        # Media Browser channel
        if self._is_enabled(_channels.ChannelId.MEDIA_BROWSER):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.MEDIA_BROWSER))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.MEDIA_BROWSER)
                mb_channel = _media_browser_event.MediaBrowserChannel(strand, self._messenger)
                mb_binding = self._bind(logic)
                mb_handler = _media_browser_event.MediaBrowserEventHandler(strand, mb_channel, mb_binding)
                mb_channel.receive(mb_handler)
                self._channels.append(mb_channel)
                self._keep(mb_handler)

        # Media Playback Status channel
        if self._is_enabled(_channels.ChannelId.MEDIA_PLAYBACK_STATUS):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.MEDIA_PLAYBACK_STATUS))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.MEDIA_PLAYBACK_STATUS)
                mps_channel = _media_playback_status_event.MediaPlaybackStatusChannel(strand, self._messenger)
                mps_binding = self._bind(logic)
                mps_handler = _media_playback_status_event.MediaPlaybackStatusEventHandler(strand, mps_channel, mps_binding)
                mps_channel.receive(mps_handler)
                self._channels.append(mps_channel)
                self._keep(mps_handler)

        # Media Source (microphone) channel
        if self._is_enabled(_channels.ChannelId.MEDIA_SOURCE_MICROPHONE):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.MEDIA_SOURCE_MICROPHONE))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.MEDIA_SOURCE_MICROPHONE)
                ms_channel = _media_source_event.MediaSourceChannel(strand, self._messenger)
                ms_binding = self._bind(logic)
                ms_handler = _media_source_event.MediaSourceEventHandler(strand, ms_channel, ms_binding, self._av_core_ptr)
                ms_channel.receive(ms_handler)
                self._channels.append(ms_channel)
                self._keep(ms_handler)

        # Phone Status channel
        if self._is_enabled(_channels.ChannelId.PHONE_STATUS):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.PHONE_STATUS))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.PHONE_STATUS)
                ps_channel = _phone_status_event.PhoneStatusChannel(strand, self._messenger)
                ps_binding = self._bind(logic)
                ps_handler = _phone_status_event.PhoneStatusEventHandler(strand, ps_channel, ps_binding)
                ps_channel.receive(ps_handler)
                self._channels.append(ps_channel)
                self._keep(ps_handler)

        # Radio channel
        if self._is_enabled(_channels.ChannelId.RADIO):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.RADIO))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.RADIO)
                radio_channel = _radio_event.RadioChannel(strand, self._messenger)
                radio_binding = self._bind(logic)
                radio_handler = _radio_event.RadioEventHandler(strand, radio_channel, radio_binding)
                radio_channel.receive(radio_handler)
                self._channels.append(radio_channel)
                self._keep(radio_handler)

        # Vendor Extension channel
        if self._is_enabled(_channels.ChannelId.VENDOR_EXTENSION):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.VENDOR_EXTENSION))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.VENDOR_EXTENSION)
                ve_channel = _vendor_extension_event.VendorExtensionChannel(strand, self._messenger)
                ve_binding = self._bind(logic)
                ve_handler = _vendor_extension_event.VendorExtensionEventHandler(strand, ve_channel, ve_binding)
                ve_channel.receive(ve_handler)
                self._channels.append(ve_channel)
                self._keep(ve_handler)

        # Wifi Projection channel
        if self._is_enabled(_channels.ChannelId.WIFI_PROJECTION):
            logic = self._handlers_by_channel.get(int(_channels.ChannelId.WIFI_PROJECTION))
            if logic is not None:
                strand = self._get_strand(_channels.ChannelId.WIFI_PROJECTION)
                wp_channel = _wifi_projection_event.WifiProjectionChannel(strand, self._messenger)
                wp_binding = self._bind(logic)
                wp_handler = _wifi_projection_event.WifiProjectionEventHandler(strand, wp_channel, wp_binding)
                wp_channel.receive(wp_handler)
                self._channels.append(wp_channel)
                self._keep(wp_handler)

    def stop(self):
        for ch in list(self._channels):
            if hasattr(ch, "stop"):
                try:
                    ch.stop()
                except Exception:
                    pass
        self._channels.clear()
        self._handlers.clear()
        self._bindings.clear()
        self._control_channel = None
        self._control_strand = None

    def request_shutdown(self):
        if self._control_channel is None or self._control_strand is None:
            _logger.debug("ShutdownRequest skipped: control channel not ready.")
            return False
        try:
            import protobuf as core
        except Exception:
            _logger.debug("ShutdownRequest skipped: protobuf module not available.")
            return False

        try:
            msg = core.GetProtobuf("aap_protobuf.service.control.message.ByeByeRequest")
            payload = _build_bye_bye_request_bytes()
            if payload:
                msg.parse_from_string(payload)
            self._control_channel.send_shutdown_request(msg, self._control_strand)
            _logger.info("ShutdownRequest sent (ByeByeRequest).")
            return True
        except Exception as exc:
            _logger.error("ShutdownRequest failed: %s", exc)
            return False


def _build_bye_bye_request_bytes() -> bytes:
    try:
        from aasdk_proto.aap_protobuf.service.control.message.ByeByeRequest_pb2 import ByeByeRequest
        msg = ByeByeRequest()
        # Optional: set reason if available
        if hasattr(msg, "reason"):
            try:
                from aasdk_proto.aap_protobuf.service.control.message.ByeByeReason_pb2 import ByeByeReason
                if hasattr(ByeByeReason, "REASON_USER_ACTION"):
                    msg.reason = ByeByeReason.REASON_USER_ACTION
                elif hasattr(ByeByeReason, "REASON_USER_REQUEST"):
                    msg.reason = ByeByeReason.REASON_USER_REQUEST
                else:
                    msg.reason = 1
            except Exception:
                pass
        return msg.SerializeToString()
    except Exception:
        return b""