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
    from aasdk_proto.aap_protobuf.service.control.message import AuthResponse_pb2
    from aasdk_proto.aap_protobuf.service.control.message import ServiceDiscoveryResponse_pb2
    from aasdk_proto.aap_protobuf.service.control.message import HeadUnitInfo_pb2
    from aasdk_proto.aap_protobuf.service.control.message import DriverPosition_pb2
    from aasdk_proto.aap_protobuf.service.control.message import PingResponse_pb2
    from aasdk_proto.aap_protobuf.service.control.message import AudioFocusNotification_pb2
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    from aasdk_proto.aap_protobuf.service.Service_pb2 import ServiceConfiguration
    # Media sink (video + audio output)
    from aasdk_proto.aap_protobuf.service.media.sink.MediaSinkService_pb2 import MediaSinkService
    from aasdk_proto.aap_protobuf.service.media.sink.message.VideoCodecResolutionType_pb2 import VideoCodecResolutionType
    from aasdk_proto.aap_protobuf.service.media.sink.message.VideoFrameRateType_pb2 import VideoFrameRateType
    from aasdk_proto.aap_protobuf.service.media.sink.message.DisplayType_pb2 import DisplayType
    from aasdk_proto.aap_protobuf.service.media.sink.message.AudioType_pb2 import AudioType        # [FIX-2]
    # Sensor source
    from aasdk_proto.aap_protobuf.service.sensorsource.SensorSourceService_pb2 import SensorSourceService
    from aasdk_proto.aap_protobuf.service.sensorsource.message.Sensor_pb2 import Sensor
    from aasdk_proto.aap_protobuf.service.sensorsource.message.SensorType_pb2 import SensorType
    # Input source
    from aasdk_proto.aap_protobuf.service.inputsource.InputSourceService_pb2 import InputSourceService
    # Media source (audio input / microfono) — av_input_channel                    [FIX-3]
    from aasdk_proto.aap_protobuf.service.media.source.MediaSourceService_pb2 import MediaSourceService
    from aasdk_proto.aap_protobuf.service.media.source.message.AVInputChannel_pb2 import AVInputChannel  # [FIX-3]
    from aasdk_proto.aap_protobuf.service.media.source.message.AudioConfig_pb2 import AudioConfig        # [FIX-3]
    # Navigation status                                                              [FIX-4]
    from aasdk_proto.aap_protobuf.service.navigation.NavigationStatusService_pb2 import NavigationStatusService
    from aasdk_proto.aap_protobuf.service.navigation.message.NavigationTurnType_pb2 import NavigationTurnType
    from aasdk_proto.aap_protobuf.service.navigation.message.NavigationImageOptions_pb2 import NavigationImageOptions
    # Media info channel (MediaStatusService)                                        [FIX-5]
    from aasdk_proto.aap_protobuf.service.mediainfo.MediaInfoChannel_pb2 import MediaInfoChannel
    # Bluetooth                                                                      [FIX-6]
    from aasdk_proto.aap_protobuf.service.bluetooth.BluetoothService_pb2 import BluetoothService
    from aasdk_proto.aap_protobuf.service.bluetooth.message.BluetoothPairingMethod_pb2 import BluetoothPairingMethod
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    print(f"\n[ERRORE CRITICO] Moduli Protobuf non trovati: {e}")
    print("1) pip install protobuf")
    print("2) ./scripts/generate_protos.sh")
    sys.exit(1)


# ChannelId enum aasdk (da ChannelId.hpp):
#   0=CONTROL, 1=SENSOR_SOURCE, 2=MEDIA_SINK,
#   3=MEDIA_SINK_VIDEO, 4=MEDIA_SINK_MEDIA_AUDIO,
#   5=MEDIA_SINK_GUIDANCE_AUDIO, 6=MEDIA_SINK_SYSTEM_AUDIO,
#   7=MEDIA_SINK_TELEPHONY_AUDIO, 8=INPUT_SOURCE,
#   9=MEDIA_SOURCE_MICROPHONE, 10=BLUETOOTH,
#   13=NAVIGATION, 15=MEDIA_STATUS
CH_SENSOR        = 1
CH_VIDEO         = 3
CH_MEDIA_AUDIO   = 4
CH_SPEECH_AUDIO  = 5
CH_SYSTEM_AUDIO  = 6
CH_INPUT         = 8
CH_MIC           = 9
CH_BLUETOOTH     = 10
CH_NAVIGATION    = 13   # [FIX-4]
CH_MEDIA_STATUS  = 15   # [FIX-5]

# [FIX-9] Keycodes supportati dichiarati esplicitamente (da InputService::getSupportedButtonCodes)
# Corrispondono a ButtonCode enum aasdk. Il BindingRequest fallisce se si dichiara un code
# non presente in questa lista.
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
        # [FIX-8] Geometria schermo parametrica — non hardcoded
        self.screen_width  = screen_width
        self.screen_height = screen_height
        # [FIX-6] Bluetooth condizionato
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

    def on_service_discovery_request(self, payload: bytes) -> bytes:
        """
        Costruisce la ServiceDiscoveryResponse allineata al ground truth C++ di openauto.

        Canali registrati (con riferimento al file C++ sorgente):
          CH 1  = SENSOR_SOURCE       → SensorService-3.cpp:fillFeatures()
          CH 3  = MEDIA_SINK_VIDEO    → VideoService-7.cpp:fillFeatures()
          CH 4  = MEDIA_AUDIO         → AudioService-11.cpp (MediaAudioService-14.cpp)
          CH 5  = SPEECH_AUDIO        → AudioService-11.cpp (SpeechAudioService-5.cpp)
          CH 6  = SYSTEM_AUDIO        → AudioService-11.cpp (SystemAudioService-6.cpp)
          CH 8  = INPUT_SOURCE        → InputService-13.cpp:fillFeatures()
          CH 9  = MIC (av_input_ch.)  → AudioInputService-10.cpp:fillFeatures()
          CH 13 = NAVIGATION          → NavigationStatusService.cpp:fillFeatures()
          CH 15 = MEDIA_STATUS        → MediaStatusService-15.cpp:fillFeatures()
          CH 10 = BLUETOOTH           → BluetoothService-12.cpp (condizionato)

        NOTE architetturali:
          - max_unacked=1 / configs(0) vengono inviati nell'AVChannelSetupResponse (lato C++),
            non qui nella ServiceDiscoveryResponse. [FIX-10]
          - I flussi media reali (PCM/H.264) NON transitano mai per Python (GIL constraint).
        """
        print("\n[Orchestrator] Service Discovery Request ricevuta!")

        msg = ServiceDiscoveryResponse_pb2.ServiceDiscoveryResponse()

        # ── HeadUnitInfo ──────────────────────────────────────────────────────
        msg.headunit_info.make                       = "NemoDev"
        msg.headunit_info.model                      = "NemoHU"
        msg.headunit_info.year                       = "2025"
        msg.headunit_info.vehicle_id                 = "NEMO0001"
        msg.headunit_info.head_unit_make             = "NemoDev"
        msg.headunit_info.head_unit_model            = "NemoHeadUnit"
        msg.headunit_info.head_unit_software_build   = "1"
        msg.headunit_info.head_unit_software_version = "0.1.0"

        # Guida a sinistra (Italia/Europa)
        msg.driver_position = DriverPosition_pb2.DRIVER_POSITION_LEFT
        msg.can_play_native_media_during_vr = False

        # ── CH 1: SENSOR SOURCE ───────────────────────────────────────────────
        # Ref: SensorService-3.cpp::fillFeatures()
        # sensorChannel->add_sensors()->set_type(DRIVING_STATUS)
        # sensorChannel->add_sensors()->set_type(NIGHT_DATA)
        ch1 = msg.channels.add()
        ch1.id = CH_SENSOR
        svc_sensor = SensorSourceService()
        s1 = svc_sensor.sensors.add()
        s1.sensor_type = SensorType.Value("SENSOR_DRIVING_STATUS_DATA")
        s2 = svc_sensor.sensors.add()
        s2.sensor_type = SensorType.Value("SENSOR_NIGHT_MODE")
        ch1.sensor_source_service.CopyFrom(svc_sensor)

        # ── CH 3: VIDEO (MEDIA_SINK_VIDEO) ────────────────────────────────────
        # Ref: VideoService-7.cpp::fillFeatures()
        # videoChannel->set_stream_type(VIDEO)
        # videoChannel->set_available_while_in_call(true)
        # videoConfig1->set_video_resolution(...) / set_video_fps(...) / set_dpi(...)
        ch3 = msg.channels.add()
        ch3.id = CH_VIDEO
        svc_video = MediaSinkService()
        svc_video.display_type            = DisplayType.Value("DISPLAY_TYPE_MAIN")
        svc_video.available_while_in_call = True
        vcfg = svc_video.video_configs.add()
        vcfg.codec_resolution = VideoCodecResolutionType.Value("VIDEO_800x480")
        vcfg.frame_rate       = VideoFrameRateType.Value("VIDEO_FPS_30")
        vcfg.density          = 140
        vcfg.width_margin     = 0
        vcfg.height_margin    = 0
        ch3.media_sink_service.CopyFrom(svc_video)

        # ── CH 4: MEDIA AUDIO ─────────────────────────────────────────────────
        # Ref: AudioService-11.cpp::fillFeatures()
        # audioChannel->set_audio_type(MEDIA)            ← [FIX-2]
        # audioChannel->set_available_while_in_call(true)← [FIX-7]
        # audioConfig->set_sample_rate(48000)            ← [FIX-1]
        # audioConfig->set_bit_depth(16)
        # audioConfig->set_channel_count(2)
        ch4 = msg.channels.add()
        ch4.id = CH_MEDIA_AUDIO
        svc_media_audio = MediaSinkService()
        svc_media_audio.display_type            = DisplayType.Value("DISPLAY_TYPE_NONE")
        svc_media_audio.audio_type              = AudioType.Value("AUDIO_MEDIA")   # [FIX-2]
        svc_media_audio.available_while_in_call = True                             # [FIX-7]
        ac_m = svc_media_audio.audio_configs.add()                                 # [FIX-1]
        ac_m.sample_rate    = 48000
        ac_m.bit_depth      = 16
        ac_m.channel_count  = 2
        ch4.media_sink_service.CopyFrom(svc_media_audio)

        # ── CH 5: SPEECH AUDIO ────────────────────────────────────────────────
        # Ref: AudioService-11.cpp::fillFeatures()
        # audioChannel->set_audio_type(SPEECH)           ← [FIX-2]
        # audioConfig: 1ch / 16kHz / 16bit               ← [FIX-1]
        ch5 = msg.channels.add()
        ch5.id = CH_SPEECH_AUDIO
        svc_speech = MediaSinkService()
        svc_speech.display_type            = DisplayType.Value("DISPLAY_TYPE_NONE")
        svc_speech.audio_type              = AudioType.Value("AUDIO_SPEECH")       # [FIX-2]
        svc_speech.available_while_in_call = True                                  # [FIX-7]
        ac_sp = svc_speech.audio_configs.add()                                     # [FIX-1]
        ac_sp.sample_rate   = 16000
        ac_sp.bit_depth     = 16
        ac_sp.channel_count = 1
        ch5.media_sink_service.CopyFrom(svc_speech)

        # ── CH 6: SYSTEM AUDIO ────────────────────────────────────────────────
        # Ref: AudioService-11.cpp::fillFeatures()
        # audioChannel->set_audio_type(SYSTEM)           ← [FIX-2]
        # audioConfig: 1ch / 16kHz / 16bit               ← [FIX-1]
        ch6 = msg.channels.add()
        ch6.id = CH_SYSTEM_AUDIO
        svc_sys = MediaSinkService()
        svc_sys.display_type            = DisplayType.Value("DISPLAY_TYPE_NONE")
        svc_sys.audio_type              = AudioType.Value("AUDIO_SYSTEM")          # [FIX-2]
        svc_sys.available_while_in_call = True                                     # [FIX-7]
        ac_sy = svc_sys.audio_configs.add()                                        # [FIX-1]
        ac_sy.sample_rate   = 16000
        ac_sy.bit_depth     = 16
        ac_sy.channel_count = 1
        ch6.media_sink_service.CopyFrom(svc_sys)

        # ── CH 8: INPUT SOURCE ────────────────────────────────────────────────
        # Ref: InputService-13.cpp::fillFeatures()
        # inputChannel->add_supported_keycodes(...)      ← [FIX-9]
        # touchscreenConfig->set_width/height(...)       ← [FIX-8] parametrico
        ch8 = msg.channels.add()
        ch8.id = CH_INPUT
        svc_input = InputSourceService()
        for kc in SUPPORTED_KEYCODES:                                              # [FIX-9]
            svc_input.supported_keycodes.append(kc)
        ts = svc_input.touchscreen.add()
        ts.width  = self.screen_width                                              # [FIX-8]
        ts.height = self.screen_height
        ch8.input_source_service.CopyFrom(svc_input)

        # ── CH 9: MICROPHONE (av_input_channel, NON media_source_service) ─────
        # Ref: AudioInputService-10.cpp::fillFeatures()
        # avInputChannel->set_stream_type(AUDIO)         ← [FIX-3] tipo CORRETTO
        # audioConfig->set_sample_rate(16000)
        # audioConfig->set_bit_depth(16) / set_channel_count(1)
        ch9 = msg.channels.add()
        ch9.id = CH_MIC
        av_in = AVInputChannel()                                                   # [FIX-3]
        av_in.stream_type = 1  # AVStreamType::AUDIO
        mic_cfg = AudioConfig()
        mic_cfg.sample_rate   = 16000
        mic_cfg.bit_depth     = 16
        mic_cfg.channel_count = 1
        av_in.audio_config.CopyFrom(mic_cfg)
        ch9.av_input_channel.CopyFrom(av_in)

        # ── CH 13: NAVIGATION STATUS ──────────────────────────────────────────
        # Ref: NavigationStatusService.cpp::fillFeatures()            ← [FIX-4]
        # navStatusChannel->set_minimum_interval_ms(1000)
        # navStatusChannel->set_type(NavigationTurnType::IMAGE)
        # imageOptions: 256x256, 16bit colour depth
        ch_nav = msg.channels.add()
        ch_nav.id = CH_NAVIGATION
        nav_svc = NavigationStatusService()
        nav_svc.minimum_interval_ms = 1000
        nav_svc.type = NavigationTurnType.Value("IMAGE")
        img_opts = NavigationImageOptions()
        img_opts.colour_depth_bits = 16
        img_opts.width             = 256
        img_opts.height            = 256
        nav_svc.image_options.CopyFrom(img_opts)
        ch_nav.navigation_channel.CopyFrom(nav_svc)

        # ── CH 15: MEDIA STATUS (MediaInfoChannel) ────────────────────────────
        # Ref: MediaStatusService-15.cpp::fillFeatures()              ← [FIX-5]
        # IMPORTANTE: in C++ mutable_media_infochannel() viene chiamato PRIMA
        # di set_channel_id() → rispettiamo l'ordine di serializzazione proto.
        ch_ms = msg.channels.add()
        _media_info = MediaInfoChannel()                                           # [FIX-5]
        ch_ms.media_infochannel.CopyFrom(_media_info)                             # prima
        ch_ms.id = CH_MEDIA_STATUS                                                # poi

        # ── CH 10: BLUETOOTH (condizionato) ───────────────────────────────────
        # Ref: BluetoothService-12.cpp::fillFeatures()                ← [FIX-6]
        # Registrato SOLO SE bluetoothDevice_->isAvailable() == true
        if self.bluetooth_available:
            ch_bt = msg.channels.add()
            ch_bt.id = CH_BLUETOOTH
            bt_svc = BluetoothService()
            bt_svc.adapter_address = self.bt_address
            bt_svc.supported_pairing_methods.append(
                BluetoothPairingMethod.Value("HFP")
            )
            ch_bt.bluetooth_channel.CopyFrom(bt_svc)

        serialized = msg.SerializeToString()
        return self._log_and_send(
            f"Invia ServiceDiscoveryResponse ({len(msg.channels)} canali)",
            serialized
        )

    def on_ping_request(self, payload: bytes) -> bytes:
        print("\n[Orchestrator] Ricevuto Ping. Rispondo con Pong.")
        msg = PingResponse_pb2.PingResponse()
        msg.timestamp = int(time.time() * 1000)
        return msg.SerializeToString()

    def on_audio_focus_request(self, payload: bytes) -> bytes:
        print("\n[Orchestrator] AudioFocusRequest ricevuta.")
        msg = AudioFocusNotification_pb2.AudioFocusNotification()
        msg.audio_focus_state = 1  # AUDIO_FOCUS_STATE_GAIN
        return self._log_and_send("Invia AudioFocusNotification", msg.SerializeToString())

    def on_video_channel_open_request(self, payload: bytes) -> bytes:
        # Placeholder: in Phase 5 gestito da VideoMediaSinkService C++
        print("\n[Orchestrator] on_video_channel_open_request chiamato (Phase 5).")
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
