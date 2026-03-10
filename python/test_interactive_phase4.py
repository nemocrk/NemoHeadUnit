import sys
import os
import binascii
import time

current_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.dirname(current_dir))
sys.path.append(current_dir)

try:
    import build.nemo_head_unit as core
except ImportError as e:
    print(f"Errore import: {e}. Assicurati di aver compilato con cmake -B build && cmake --build build")
    sys.exit(1)

try:
    import google.protobuf
    # ── Control channel ──────────────────────────────────────────────────────────
    from aasdk_proto.aap_protobuf.service.control.message import AuthResponse_pb2
    from aasdk_proto.aap_protobuf.service.control.message import ServiceDiscoveryResponse_pb2
    from aasdk_proto.aap_protobuf.service.control.message import HeadUnitInfo_pb2
    from aasdk_proto.aap_protobuf.service.control.message import DriverPosition_pb2
    from aasdk_proto.aap_protobuf.service.control.message import PingResponse_pb2
    from aasdk_proto.aap_protobuf.service.control.message import AudioFocusNotification_pb2
    from aasdk_proto.aap_protobuf.service.control.message import AudioFocusRequest_pb2
    from aasdk_proto.aap_protobuf.service.control.message import AudioFocusRequestType_pb2
    from aasdk_proto.aap_protobuf.service.control.message import AudioFocusStateType_pb2
    from aasdk_proto.aap_protobuf.service.control.message import ChannelOpenResponse_pb2
    from aasdk_proto.aap_protobuf.service.control.message import NavFocusNotification_pb2
    from aasdk_proto.aap_protobuf.service.control.message import NavFocusType_pb2
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2

    # ── Media Sink (video + audio) ───────────────────────────────────────────
    from aasdk_proto.aap_protobuf.service.media.sink import MediaSinkService_pb2
    from aasdk_proto.aap_protobuf.service.media.sink.MediaSinkService_pb2 import MediaSinkService
    from aasdk_proto.aap_protobuf.service.media.sink.message.VideoCodecResolutionType_pb2 import VideoCodecResolutionType
    from aasdk_proto.aap_protobuf.service.media.sink.message.VideoFrameRateType_pb2 import VideoFrameRateType
    from aasdk_proto.aap_protobuf.service.media.sink.message.AudioStreamType_pb2 import AudioStreamType
    from aasdk_proto.aap_protobuf.service.media.shared.message.MediaCodecType_pb2 import MediaCodecType
    from aasdk_proto.aap_protobuf.service.media.shared.message.AudioConfiguration_pb2 import AudioConfiguration
    from aasdk_proto.aap_protobuf.service.media.shared.message.Config_pb2 import Config as AVChannelConfig

    # ── Video focus ─────────────────────────────────────────────────────────────
    from aasdk_proto.aap_protobuf.service.media.video.message.VideoFocusNotification_pb2 import VideoFocusNotification
    from aasdk_proto.aap_protobuf.service.media.video.message.VideoFocusMode_pb2 import VideoFocusMode

    # ── Media Source (microfono) ──────────────────────────────────────────────
    from aasdk_proto.aap_protobuf.service.media.source.MediaSourceService_pb2 import MediaSourceService

    # ── Sensor Source ─────────────────────────────────────────────────────────
    from aasdk_proto.aap_protobuf.service.sensorsource.SensorSourceService_pb2 import SensorSourceService
    from aasdk_proto.aap_protobuf.service.sensorsource.message.Sensor_pb2 import Sensor
    from aasdk_proto.aap_protobuf.service.sensorsource.message.SensorType_pb2 import SensorType
    # Aggiunto: risposta al SensorStartRequest e indicazione DrivingStatus/NightMode
    from aasdk_proto.aap_protobuf.service.sensorsource.message.SensorStartResponseMessage_pb2 import SensorStartResponseMessage
    from aasdk_proto.aap_protobuf.service.sensorsource.message.SensorBatch_pb2 import SensorBatch
    from aasdk_proto.aap_protobuf.service.sensorsource.message.DrivingStatus_pb2 import DrivingStatus

    # ── Input Source ──────────────────────────────────────────────────────────
    from aasdk_proto.aap_protobuf.service.inputsource.InputSourceService_pb2 import InputSourceService

    # ── Navigation Status ─────────────────────────────────────────────────────
    from aasdk_proto.aap_protobuf.service.navigationstatus.NavigationStatusService_pb2 import NavigationStatusService

    # ── Bluetooth ─────────────────────────────────────────────────────────────
    from aasdk_proto.aap_protobuf.service.bluetooth.BluetoothService_pb2 import BluetoothService
    from aasdk_proto.aap_protobuf.service.bluetooth.message.BluetoothPairingMethod_pb2 import BluetoothPairingMethod

    PROTOBUF_AVAILABLE = True
except ImportError as e:
    print(f"\n[ERRORE CRITICO] Moduli Protobuf non trovati: {e}")
    print("1) pip install protobuf")
    print("2) ./scripts/generate_protos.sh")
    sys.exit(1)


# ── ChannelId enum aasdk (da ChannelId.hpp) ───────────────────────────────────
CH_SENSOR       = 1
CH_VIDEO        = 3
CH_MEDIA_AUDIO  = 4
CH_SPEECH_AUDIO = 5
CH_SYSTEM_AUDIO = 6
CH_INPUT        = 8
CH_MIC          = 9
CH_BLUETOOTH    = 10
CH_NAVIGATION   = 12

# Canali che richiedono AVChannelSetupRequest/Response
AV_CHANNELS = {CH_VIDEO, CH_MEDIA_AUDIO, CH_SPEECH_AUDIO, CH_SYSTEM_AUDIO, CH_MIC}

# ── Keycodes supportati ───────────────────────────────────────────────────────
SUPPORTED_KEYCODES = [
    1,   # NONE
    4,   # HOME
    5,   # BACK
    6,   # PHONE
    7,   # CALL_END
    23,  # ENTER / OK
    19,  # DPAD_UP
    20,  # DPAD_DOWN
    21,  # DPAD_LEFT
    22,  # DPAD_RIGHT
    82,  # MENU
    164, # MUTE
    85,  # MEDIA_PLAY_PAUSE
    87,  # MEDIA_NEXT
    88,  # MEDIA_PREVIOUS
    24,  # VOLUME_UP
    25,  # VOLUME_DOWN
    115, # SCROLL_WHEEL
]


class InteractiveOrchestrator:
    def __init__(self,
                 screen_width: int = 800,
                 screen_height: int = 480,
                 bluetooth_available: bool = False,
                 bt_address: str = ""):
        self.cryptor = None
        self.handshake_done = False
        self.screen_width  = screen_width
        self.screen_height = screen_height
        self.bluetooth_available = bluetooth_available
        self.bt_address = bt_address

    def set_cryptor(self, cryptor):
        print("[Orchestrator] Cryptor inizializzato dal C++.")
        self.cryptor = cryptor

    def _drain_tls_out(self, max_iters: int = 32) -> bytes:
        out = b""
        for _ in range(max_iters):
            chunk = self.cryptor.read_handshake_buffer()
            if not chunk:
                break
            out += chunk
        return out

    def _step_handshake_and_collect(self, max_steps: int = 32):
        done = False
        out = b""
        for _ in range(max_steps):
            done = bool(self.cryptor.do_handshake())
            drained = self._drain_tls_out()
            if drained:
                out += drained
                continue
            break
        return done, out

    def _log_and_send(self, label: str, data: bytes) -> bytes:
        print(f"\n{'='*50}")
        print(f"[Azione: {label}]")
        print(f"Dimensione: {len(data)} bytes")
        if len(data) > 0:
            print(f"Preview Hex: {binascii.hexlify(data[:32]).decode()}...")
        print(f"{'='*50}")
        return data

    def _make_video_focus_indication(self, unsolicited: bool = False) -> bytes:
        """
        Costruisce VideoFocusNotification(focus=PROJECTED, unsolicited=unsolicited).
        Allineato a VideoMediaSinkService.cpp::sendVideoFocusIndication().
        Usato SOLO per on_video_focus_request (step 3 - gate H.264).
        Il VideoFocus post-Setup e' inviato direttamente dal C++ via promise->then.
        """
        vf = VideoFocusNotification()
        vf.focus       = VideoFocusMode.Value("VIDEO_FOCUS_PROJECTED")
        vf.unsolicited = unsolicited
        return vf.SerializeToString()

    # ─────────────────────────────────────────────────────────────────────────
    # Handshake TLS
    # ─────────────────────────────────────────────────────────────────────────

    def on_version_status(self, major: int, minor: int, status: int) -> bytes:
        print(f"\n[Orchestrator] on_version_status: {major}.{minor} (status={status})")
        if status != 0:
            raise RuntimeError(f"Version negotiation fallita (status={status})")
        done, out = self._step_handshake_and_collect()
        return self._log_and_send("Invia TLS Flight 1 (drain)", out)

    def on_handshake(self, payload: bytes) -> bytes:
        print(f"\n[Orchestrator] Ricevuto Handshake payload ({len(payload)} bytes)")
        if payload:
            self.cryptor.write_handshake_buffer(payload)
        done, out = self._step_handshake_and_collect()
        if not done:
            return self._log_and_send("Invia TLS chunk (drain)", out)
        print("\n[Orchestrator] *** HANDSHAKE TLS COMPLETATO ***")
        self.handshake_done = True
        if out:
            return self._log_and_send("Invia TLS finale (drain)", out)
        return b""

    def get_auth_complete_response(self) -> bytes:
        print("\n[Orchestrator] Costruzione AuthResponse...")
        msg = AuthResponse_pb2.AuthResponse()
        msg.status = 0  # STATUS_SUCCESS
        return self._log_and_send("Invia AuthCompleteResponse", msg.SerializeToString())

    # ─────────────────────────────────────────────────────────────────────────
    # Service Discovery
    # ─────────────────────────────────────────────────────────────────────────

    def on_service_discovery_request(self, payload: bytes) -> bytes:
        """
        Costruisce la ServiceDiscoveryResponse con tutti i canali allineati
        ai file C++ reali del repo.
        Ref: fillFeatures() nei vari *Service-*.cpp
        """
        print("\n[Orchestrator] Service Discovery Request ricevuta!")

        msg = ServiceDiscoveryResponse_pb2.ServiceDiscoveryResponse()

        msg.headunit_info.make                       = "NemoDev"
        msg.headunit_info.model                      = "NemoHU"
        msg.headunit_info.year                       = "2025"
        msg.headunit_info.vehicle_id                 = "NEMO0001"
        msg.headunit_info.head_unit_make             = "NemoDev"
        msg.headunit_info.head_unit_model            = "NemoHeadUnit"
        msg.headunit_info.head_unit_software_build   = "1"
        msg.headunit_info.head_unit_software_version = "0.1.0"
        msg.driver_position = DriverPosition_pb2.DRIVER_POSITION_LEFT
        msg.can_play_native_media_during_vr = False

        # CH 1: SENSOR SOURCE
        ch1 = msg.channels.add()
        ch1.id = CH_SENSOR
        svc_sensor = SensorSourceService()
        s1 = svc_sensor.sensors.add(); s1.sensor_type = SensorType.Value("SENSOR_DRIVING_STATUS_DATA")
        s2 = svc_sensor.sensors.add(); s2.sensor_type = SensorType.Value("SENSOR_LOCATION")
        s3 = svc_sensor.sensors.add(); s3.sensor_type = SensorType.Value("SENSOR_NIGHT_MODE")
        ch1.sensor_source_service.CopyFrom(svc_sensor)

        # CH 3: VIDEO
        ch3 = msg.channels.add()
        ch3.id = CH_VIDEO
        svc_video = MediaSinkService()
        svc_video.available_type          = MediaCodecType.Value("MEDIA_CODEC_VIDEO_H264_BP")
        svc_video.available_while_in_call = True
        vcfg = svc_video.video_configs.add()
        vcfg.codec_resolution = VideoCodecResolutionType.Value("VIDEO_800x480")
        vcfg.frame_rate       = VideoFrameRateType.Value("VIDEO_FPS_30")
        vcfg.density          = 140
        vcfg.width_margin     = 0
        vcfg.height_margin    = 0
        ch3.media_sink_service.CopyFrom(svc_video)

        # CH 4: MEDIA AUDIO
        ch4 = msg.channels.add()
        ch4.id = CH_MEDIA_AUDIO
        svc_ma = MediaSinkService()
        svc_ma.available_type          = MediaCodecType.Value("MEDIA_CODEC_AUDIO_PCM")
        svc_ma.audio_type              = AudioStreamType.Value("AUDIO_STREAM_MEDIA")
        svc_ma.available_while_in_call = True
        ac = svc_ma.audio_configs.add(); ac.sampling_rate = 48000; ac.number_of_bits = 16; ac.number_of_channels = 2
        ch4.media_sink_service.CopyFrom(svc_ma)

        # CH 5: GUIDANCE AUDIO
        ch5 = msg.channels.add()
        ch5.id = CH_SPEECH_AUDIO
        svc_ga = MediaSinkService()
        svc_ga.available_type          = MediaCodecType.Value("MEDIA_CODEC_AUDIO_PCM")
        svc_ga.audio_type              = AudioStreamType.Value("AUDIO_STREAM_GUIDANCE")
        svc_ga.available_while_in_call = True
        ac = svc_ga.audio_configs.add(); ac.sampling_rate = 16000; ac.number_of_bits = 16; ac.number_of_channels = 1
        ch5.media_sink_service.CopyFrom(svc_ga)

        # CH 6: SYSTEM AUDIO
        ch6 = msg.channels.add()
        ch6.id = CH_SYSTEM_AUDIO
        svc_sa = MediaSinkService()
        svc_sa.available_type          = MediaCodecType.Value("MEDIA_CODEC_AUDIO_PCM")
        svc_sa.audio_type              = AudioStreamType.Value("AUDIO_STREAM_SYSTEM_AUDIO")
        svc_sa.available_while_in_call = True
        ac = svc_sa.audio_configs.add(); ac.sampling_rate = 16000; ac.number_of_bits = 16; ac.number_of_channels = 1
        ch6.media_sink_service.CopyFrom(svc_sa)

        # CH 8: INPUT SOURCE
        ch8 = msg.channels.add()
        ch8.id = CH_INPUT
        svc_input = InputSourceService()
        for kc in SUPPORTED_KEYCODES:
            svc_input.keycodes_supported.append(kc)
        ts = svc_input.touchscreen.add()
        ts.width  = self.screen_width
        ts.height = self.screen_height
        ch8.input_source_service.CopyFrom(svc_input)

        # CH 9: MIC
        ch9 = msg.channels.add()
        ch9.id = CH_MIC
        svc_mic = MediaSourceService()
        svc_mic.available_type = MediaCodecType.Value("MEDIA_CODEC_AUDIO_PCM")
        svc_mic.audio_config.sampling_rate      = 16000
        svc_mic.audio_config.number_of_bits     = 16
        svc_mic.audio_config.number_of_channels = 1
        ch9.media_source_service.CopyFrom(svc_mic)

        # CH 13: NAVIGATION
        ch13 = msg.channels.add()
        ch13.id = CH_NAVIGATION
        nav_svc = NavigationStatusService()
        nav_svc.minimum_interval_ms = 1000
        nav_svc.type = NavigationStatusService.InstrumentClusterType.Value("IMAGE")
        nav_svc.image_options.width             = 256
        nav_svc.image_options.height            = 256
        nav_svc.image_options.colour_depth_bits = 16
        ch13.navigation_status_service.CopyFrom(nav_svc)

        # CH 10: BLUETOOTH (condizionato)
        if self.bluetooth_available:
            ch_bt = msg.channels.add()
            ch_bt.id = CH_BLUETOOTH
            bt_svc = BluetoothService()
            bt_svc.car_address = self.bt_address
            bt_svc.supported_pairing_methods.append(BluetoothPairingMethod.Value("BLUETOOTH_PAIRING_PIN"))
            bt_svc.supported_pairing_methods.append(BluetoothPairingMethod.Value("BLUETOOTH_PAIRING_NUMERIC_COMPARISON"))
            ch_bt.bluetooth_service.CopyFrom(bt_svc)

        serialized = msg.SerializeToString()
        return self._log_and_send(
            f"Invia ServiceDiscoveryResponse ({len(msg.channels)} canali)", serialized
        )

    # ─────────────────────────────────────────────────────────────────────────
    # AV Channel Setup (step 1 del 3 per aprire ogni canale media)
    # ─────────────────────────────────────────────────────────────────────────
    def on_av_channel_setup_request(self, channel_id: int, payload: bytes) -> bytes:
        print(f"\n[Orchestrator] AVChannelSetupRequest su CH {channel_id}")
        resp = AVChannelConfig()
        if channel_id == CH_VIDEO:
            resp.status = AVChannelConfig.Status.Value("STATUS_WAIT")  # ← era STATUS_READY
        else:
            resp.status = AVChannelConfig.Status.Value("STATUS_READY")
        resp.max_unacked = 1
        resp.configuration_indices.append(0)
        setup_bytes = resp.SerializeToString()
        self._log_and_send(f"Invia AVChannelSetupResponse CH {channel_id}", setup_bytes)
        return setup_bytes

    # ─────────────────────────────────────────────────────────────────────────
    # Channel Open (step 2 del 3)
    # ─────────────────────────────────────────────────────────────────────────
    def on_channel_open_request(self, channel_id: int, payload: bytes) -> bytes:
        print(f"\n[Orchestrator] ChannelOpenRequest su CH {channel_id}")
        resp = ChannelOpenResponse_pb2.ChannelOpenResponse()
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        open_bytes = resp.SerializeToString()
        self._log_and_send(f"Invia ChannelOpenResponse CH {channel_id}", open_bytes)
        return open_bytes

    # ─────────────────────────────────────────────────────────────────────────
    # Sensor Start Request (CH1 gate)
    # Ref: SensorService.cpp::onSensorStartRequest()
    #
    # Android → HU: SensorRequest (type = SENSOR_DRIVING_STATUS_DATA o SENSOR_NIGHT_MODE)
    # HU → Android: SensorStartResponseMessage(STATUS_SUCCESS)
    #
    # GATE CRITICO: il messaggio SENSOR_DRIVING_STATUS_DATA deve ricevere
    # risposta con SensorStartResponse + SensorBatch(DRIVE_STATUS_UNRESTRICTED).
    # Senza questo Android NON avvia mai lo stream H.264.
    #
    # NOTA ARCHITETTURALE: il C++ (SensorEventHandler) invia
    # SensorStartResponse via channel_->sendSensorStartResponse() e poi
    # nel promise->then chiama sendDrivingStatusUnrestricted()/sendNightData().
    # Questo metodo Python NON invia direttamente sul canale: è chiamato
    # dall'Orchestrator binding SOLO come riferimento logico di test.
    # L'handler reale è SensorEventHandler in sensor_event_handler.hpp.
    # ─────────────────────────────────────────────────────────────────────────
    def on_sensor_start_request(self, payload: bytes) -> bytes:
        from aasdk_proto.aap_protobuf.service.sensorsource.message.SensorRequest_pb2 import SensorRequest

        req = SensorRequest()
        req.ParseFromString(payload)
        sensor_type = req.type
        print(f"\n[Orchestrator] SensorStartRequest tipo={sensor_type}")

        # Step 1: SensorStartResponse(STATUS_SUCCESS)
        resp = SensorStartResponseMessage()
        resp.status = MessageStatus_pb2.MessageStatus.Value("STATUS_SUCCESS")
        resp_bytes = resp.SerializeToString()
        self._log_and_send(f"Invia SensorStartResponse tipo={sensor_type}", resp_bytes)

        if sensor_type == SensorType.Value("SENSOR_DRIVING_STATUS_DATA"):
            # Step 2a: SensorBatch con DRIVE_STATUS_UNRESTRICTED
            # GATE H.264: senza questo Android non invia NAL units.
            # Ref: SensorService.cpp::sendDrivingStatusUnrestricted()
            batch = SensorBatch()
            batch.driving_status_data.add().status = DrivingStatus.Value("DRIVE_STATUS_UNRESTRICTED")
            batch_bytes = batch.SerializeToString()
            self._log_and_send("Invia SensorBatch DRIVE_STATUS_UNRESTRICTED", batch_bytes)
            # Ritorna i due messaggi concatenati: il C++ li spacchetta singolarmente.
            return resp_bytes + batch_bytes

        elif sensor_type == SensorType.Value("SENSOR_NIGHT_MODE"):
            # Step 2b: SensorBatch con night_mode=False (giorno)
            # Ref: SensorService.cpp::sendNightData()
            batch = SensorBatch()
            batch.night_mode_data.add().night_mode = False
            batch_bytes = batch.SerializeToString()
            self._log_and_send("Invia SensorBatch NightMode=False", batch_bytes)
            return resp_bytes + batch_bytes

        # Per altri tipi (es. SENSOR_LOCATION) risponde solo STATUS_SUCCESS
        return resp_bytes

    # ─────────────────────────────────────────────────────────────────────────
    # Video Focus Request (step 3 del 3 per CH_VIDEO)
    # Ref: VideoMediaSinkService.cpp::onVideoFocusRequest()
    # ─────────────────────────────────────────────────────────────────────────
    def on_video_focus_request(self, payload: bytes) -> bytes:
        print("\n[Orchestrator] VideoFocusRequest ricevuta → rispondo PROJECTED")
        vf_bytes = self._make_video_focus_indication(unsolicited=False)
        return self._log_and_send("Invia VideoFocusIndication (gate video)", vf_bytes)

    # ─────────────────────────────────────────────────────────────────────────
    # on_video_channel_open_request — Placeholder Phase 5
    # ─────────────────────────────────────────────────────────────────────────
    def on_video_channel_open_request(self, payload: bytes) -> bytes:
        print("\n[Orchestrator] *** on_video_channel_open_request RAGGIUNTO! (Phase 5) ***")
        return b""

    # ─────────────────────────────────────────────────────────────────────────
    # Control channel — messaggi ausiliari
    # ─────────────────────────────────────────────────────────────────────────

    def on_ping_request(self, payload: bytes) -> bytes:
        print("\n[Orchestrator] Ping → Pong.")
        msg = PingResponse_pb2.PingResponse()
        msg.timestamp = int(time.time() * 1000)
        return msg.SerializeToString()

    def on_audio_focus_request(self, payload: bytes) -> bytes:
        """
        Ref: AndroidAutoEntity.cpp::onAudioFocusRequest()
        RELEASE → AUDIO_FOCUS_STATE_LOSS
        GAIN    → AUDIO_FOCUS_STATE_GAIN
        """
        print("\n[Orchestrator] AudioFocusRequest ricevuta.")
        req = AudioFocusRequest_pb2.AudioFocusRequest()
        req.ParseFromString(payload)
        is_release = (
            req.audio_focus_type ==
            AudioFocusRequestType_pb2.AudioFocusRequestType.Value("AUDIO_FOCUS_RELEASE")
        )
        state = (
            AudioFocusStateType_pb2.AudioFocusStateType.Value("AUDIO_FOCUS_STATE_LOSS")
            if is_release else
            AudioFocusStateType_pb2.AudioFocusStateType.Value("AUDIO_FOCUS_STATE_GAIN")
        )
        msg = AudioFocusNotification_pb2.AudioFocusNotification()
        msg.focus_state = state
        return self._log_and_send("Invia AudioFocusNotification", msg.SerializeToString())

    def on_navigation_focus_request(self, payload: bytes) -> bytes:
        """
        Ref: AndroidAutoEntity.cpp::onNavigationFocusRequest()
        Risponde sempre NAV_FOCUS_PROJECTED (OpenAuto non ha nav locale).
        """
        print("\n[Orchestrator] NavigationFocusRequest ricevuta → PROJECTED")
        msg = NavFocusNotification_pb2.NavFocusNotification()
        msg.focus_type = NavFocusType_pb2.NavFocusType.Value("NAV_FOCUS_PROJECTED")
        return self._log_and_send("Invia NavFocusNotification", msg.SerializeToString())

    def on_voice_session_request(self, payload: bytes) -> bytes:
        """Sink silente: il C++ fa solo channel->receive() senza risposta."""
        print("\n[Orchestrator] VoiceSessionRequest ricevuta (sink silente).")
        return b""

    def on_battery_status_notification(self, payload: bytes) -> bytes:
        """Sink silente: il C++ fa solo channel->receive() senza risposta."""
        print("\n[Orchestrator] BatteryStatusNotification ricevuta (sink silente).")
        return b""


def main():
    print("\n" + "*"*60)
    print("* TEST HEADLESS PHASE 4 - ServiceDiscovery reale          *")
    print("* Collega un dispositivo Android via USB                  *")
    print("*"*60 + "\n")

    if hasattr(core, "enable_aasdk_logging"):
        core.enable_aasdk_logging()

    runner = core.IoContextRunner()

    crypto = core.CryptoManager()
    crypto.initialize()

    usb = core.UsbHubManager(runner)
    usb.set_crypto_manager(crypto)

    orchestrator = InteractiveOrchestrator(
        screen_width=800,
        screen_height=480,
        bluetooth_available=False,
        bt_address=""
    )
    usb.set_orchestrator(orchestrator)

    def on_connect(success, msg):
        if success:
            print("\n[Python Callback] AOAP Avviato:", msg)
        else:
            print("\n[Python Callback] Errore connessione:", msg)
            runner.stop()

    usb.start(on_connect)
    runner.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("Uscita...")
        usb.stop()
        runner.stop()


if __name__ == "__main__":
    main()
