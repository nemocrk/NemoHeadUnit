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

    # ── Media Sink (video + audio) ────────────────────────────────────────────
    # MediaSinkService_pb2.py è in media/sink/ (NON in media/sink/message/)
    from aasdk_proto.aap_protobuf.service.media.sink import MediaSinkService_pb2
    from aasdk_proto.aap_protobuf.service.media.sink.MediaSinkService_pb2 import MediaSinkService
    from aasdk_proto.aap_protobuf.service.media.sink.message.VideoCodecResolutionType_pb2 import VideoCodecResolutionType
    from aasdk_proto.aap_protobuf.service.media.sink.message.VideoFrameRateType_pb2 import VideoFrameRateType
    from aasdk_proto.aap_protobuf.service.media.sink.message.AudioStreamType_pb2 import AudioStreamType
    # MediaCodecType e AudioConfiguration sono in media/shared/message/ (condivisi sink+source)
    from aasdk_proto.aap_protobuf.service.media.shared.message.MediaCodecType_pb2 import MediaCodecType
    from aasdk_proto.aap_protobuf.service.media.shared.message.AudioConfiguration_pb2 import AudioConfiguration

    # ── Media Source (microfono) ──────────────────────────────────────────────
    # MediaSourceService_pb2.py è in media/source/ (NON in media/source/message/)
    # audio_config è un singolo AudioConfiguration (shared) — NON repeated
    from aasdk_proto.aap_protobuf.service.media.source.MediaSourceService_pb2 import MediaSourceService

    # ── Sensor Source ─────────────────────────────────────────────────────────
    from aasdk_proto.aap_protobuf.service.sensorsource.SensorSourceService_pb2 import SensorSourceService
    from aasdk_proto.aap_protobuf.service.sensorsource.message.Sensor_pb2 import Sensor
    from aasdk_proto.aap_protobuf.service.sensorsource.message.SensorType_pb2 import SensorType

    # ── Input Source ──────────────────────────────────────────────────────────
    # campo proto: keycodes_supported (packed=true, field 1)
    from aasdk_proto.aap_protobuf.service.inputsource.InputSourceService_pb2 import InputSourceService

    # ── Navigation Status ─────────────────────────────────────────────────────
    # ATTENZIONE proto2: minimum_interval_ms e type sono REQUIRED
    # InstrumentClusterType è enum ANNIDATO in NavigationStatusService (non modulo separato)
    # ImageOptions è message ANNIDATO in NavigationStatusService (non modulo separato)
    from aasdk_proto.aap_protobuf.service.navigationstatus.NavigationStatusService_pb2 import NavigationStatusService

    # ── Bluetooth ─────────────────────────────────────────────────────────────
    # car_address è required nel proto
    from aasdk_proto.aap_protobuf.service.bluetooth.BluetoothService_pb2 import BluetoothService
    from aasdk_proto.aap_protobuf.service.bluetooth.message.BluetoothPairingMethod_pb2 import BluetoothPairingMethod

    PROTOBUF_AVAILABLE = True
except ImportError as e:
    print(f"\n[ERRORE CRITICO] Moduli Protobuf non trovati: {e}")
    print("1) pip install protobuf")
    print("2) ./scripts/generate_protos.sh")
    sys.exit(1)


# ── ChannelId enum aasdk (da ChannelId.hpp) ───────────────────────────────────
#   0=CONTROL, 1=SENSOR_SOURCE,
#   3=MEDIA_SINK_VIDEO, 4=MEDIA_SINK_MEDIA_AUDIO,
#   5=MEDIA_SINK_GUIDANCE_AUDIO, 6=MEDIA_SINK_SYSTEM_AUDIO,
#   8=INPUT_SOURCE, 9=MEDIA_SOURCE_MICROPHONE,
#   10=BLUETOOTH, 13=NAVIGATION_STATUS
CH_SENSOR       = 1
CH_VIDEO        = 3
CH_MEDIA_AUDIO  = 4
CH_SPEECH_AUDIO = 5
CH_SYSTEM_AUDIO = 6
CH_INPUT        = 8
CH_MIC          = 9
CH_BLUETOOTH    = 10
CH_NAVIGATION   = 13

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
        Costruisce la ServiceDiscoveryResponse allineata ai proto reali del repo.

        Canali registrati:
          CH 1  = SENSOR_SOURCE    → SensorService-9.cpp::fillFeatures()
                                     3 sensori: DRIVING_STATUS, LOCATION, NIGHT_MODE
          CH 3  = MEDIA_SINK_VIDEO → VideoMediaSinkService-14.cpp::fillFeatures()
                                     available_type=MEDIA_CODEC_VIDEO_H264_BP
                                     available_while_in_call=true
                                     video_configs[0]: codec_resolution, frame_rate,
                                                       density, width_margin, height_margin
          CH 4  = MEDIA_AUDIO      → AudioMediaSinkService-19.cpp::fillFeatures()
                                     available_type=MEDIA_CODEC_AUDIO_PCM
                                     audio_type=AUDIO_STREAM_MEDIA
                                     available_while_in_call=true
                                     audio_configs[0]: sampling_rate=48000,
                                                       number_of_bits=16,
                                                       number_of_channels=2
          CH 5  = GUIDANCE_AUDIO   → AudioMediaSinkService-19.cpp::fillFeatures()
                                     audio_type=AUDIO_STREAM_GUIDANCE
                                     audio_configs[0]: 1ch / 16kHz / 16bit
          CH 6  = SYSTEM_AUDIO     → AudioMediaSinkService-19.cpp::fillFeatures()
                                     audio_type=AUDIO_STREAM_SYSTEM_AUDIO
                                     audio_configs[0]: 1ch / 16kHz / 16bit
          CH 8  = INPUT_SOURCE     → InputSourceService-23.cpp::fillFeatures()
                                     keycodes_supported (packed int32)
                                     touchscreen[0]: width (required), height (required)
          CH 9  = MIC (source)     → MediaSourceService-3.cpp::fillFeatures()
                                     media_source_service
                                     available_type=MEDIA_CODEC_AUDIO_PCM
                                     audio_config (singolo): sampling_rate=16000,
                                                             number_of_bits=16,
                                                             number_of_channels=1
          CH 13 = NAVIGATION       → NavigationStatusService-5.cpp::fillFeatures()
                                     minimum_interval_ms=1000 (REQUIRED proto2)
                                     type=IMAGE (REQUIRED proto2)
                                     image_options: width=256, height=256,
                                                    colour_depth_bits=16 (tutti required)
          CH 10 = BLUETOOTH        → BluetoothService-20.cpp (condizionato)
                                     car_address (REQUIRED proto2)
                                     pairing_methods: PIN + NUMERIC_COMPARISON

        NOTE:
          - InstrumentClusterType e ImageOptions sono tipi ANNIDATI in
            NavigationStatusService, non moduli separati.
          - AudioConfiguration (shared) è usata sia da MediaSinkService
            (audio_configs repeated) che da MediaSourceService (audio_config singolo).
          - CH 15 (MediaInfoChannel) NON è in ServiceFactory-10.cpp → omesso.
          - I flussi media reali NON transitano per Python (GIL constraint).
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

        msg.driver_position = DriverPosition_pb2.DRIVER_POSITION_LEFT
        msg.can_play_native_media_during_vr = False

        # ── CH 1: SENSOR SOURCE ───────────────────────────────────────────────
        ch1 = msg.channels.add()
        ch1.id = CH_SENSOR
        svc_sensor = SensorSourceService()
        s1 = svc_sensor.sensors.add()
        s1.sensor_type = SensorType.Value("SENSOR_DRIVING_STATUS_DATA")
        s2 = svc_sensor.sensors.add()
        s2.sensor_type = SensorType.Value("SENSOR_LOCATION")
        s3 = svc_sensor.sensors.add()
        s3.sensor_type = SensorType.Value("SENSOR_NIGHT_MODE")
        ch1.sensor_source_service.CopyFrom(svc_sensor)

        # ── CH 3: VIDEO (MEDIA_SINK_VIDEO) ────────────────────────────────────
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

        # ── CH 4: MEDIA AUDIO ─────────────────────────────────────────────────
        ch4 = msg.channels.add()
        ch4.id = CH_MEDIA_AUDIO
        svc_media_audio = MediaSinkService()
        svc_media_audio.available_type          = MediaCodecType.Value("MEDIA_CODEC_AUDIO_PCM")
        svc_media_audio.audio_type              = AudioStreamType.Value("AUDIO_STREAM_MEDIA")
        svc_media_audio.available_while_in_call = True
        ac_m = svc_media_audio.audio_configs.add()
        ac_m.sampling_rate      = 48000
        ac_m.number_of_bits     = 16
        ac_m.number_of_channels = 2
        ch4.media_sink_service.CopyFrom(svc_media_audio)

        # ── CH 5: GUIDANCE AUDIO ─────────────────────────────────────────────
        ch5 = msg.channels.add()
        ch5.id = CH_SPEECH_AUDIO
        svc_guidance = MediaSinkService()
        svc_guidance.available_type          = MediaCodecType.Value("MEDIA_CODEC_AUDIO_PCM")
        svc_guidance.audio_type              = AudioStreamType.Value("AUDIO_STREAM_GUIDANCE")
        svc_guidance.available_while_in_call = True
        ac_g = svc_guidance.audio_configs.add()
        ac_g.sampling_rate      = 16000
        ac_g.number_of_bits     = 16
        ac_g.number_of_channels = 1
        ch5.media_sink_service.CopyFrom(svc_guidance)

        # ── CH 6: SYSTEM AUDIO ────────────────────────────────────────────────
        ch6 = msg.channels.add()
        ch6.id = CH_SYSTEM_AUDIO
        svc_sys = MediaSinkService()
        svc_sys.available_type          = MediaCodecType.Value("MEDIA_CODEC_AUDIO_PCM")
        svc_sys.audio_type              = AudioStreamType.Value("AUDIO_STREAM_SYSTEM_AUDIO")
        svc_sys.available_while_in_call = True
        ac_sy = svc_sys.audio_configs.add()
        ac_sy.sampling_rate      = 16000
        ac_sy.number_of_bits     = 16
        ac_sy.number_of_channels = 1
        ch6.media_sink_service.CopyFrom(svc_sys)

        # ── CH 8: INPUT SOURCE ────────────────────────────────────────────────
        ch8 = msg.channels.add()
        ch8.id = CH_INPUT
        svc_input = InputSourceService()
        for kc in SUPPORTED_KEYCODES:
            svc_input.keycodes_supported.append(kc)
        ts = svc_input.touchscreen.add()
        ts.width  = self.screen_width   # required
        ts.height = self.screen_height  # required
        ch8.input_source_service.CopyFrom(svc_input)

        # ── CH 9: MICROPHONE (media_source_service) ───────────────────────────
        # audio_config è un messaggio singolo (mutable), non repeated
        ch9 = msg.channels.add()
        ch9.id = CH_MIC
        svc_mic = MediaSourceService()
        svc_mic.available_type = MediaCodecType.Value("MEDIA_CODEC_AUDIO_PCM")
        svc_mic.audio_config.sampling_rate      = 16000
        svc_mic.audio_config.number_of_bits     = 16
        svc_mic.audio_config.number_of_channels = 1
        ch9.media_source_service.CopyFrom(svc_mic)

        # ── CH 13: NAVIGATION STATUS ──────────────────────────────────────────
        # Proto2: minimum_interval_ms e type sono REQUIRED — non possono essere omessi.
        # InstrumentClusterType e ImageOptions sono tipi ANNIDATI (non import separati).
        ch13 = msg.channels.add()
        ch13.id = CH_NAVIGATION
        nav_svc = NavigationStatusService()
        nav_svc.minimum_interval_ms = 1000
        # IMAGE = 1 nell'enum InstrumentClusterType annidato
        nav_svc.type = NavigationStatusService.InstrumentClusterType.Value("IMAGE")
        nav_svc.image_options.width             = 256   # required
        nav_svc.image_options.height            = 256   # required
        nav_svc.image_options.colour_depth_bits = 16    # required
        ch13.navigation_status_service.CopyFrom(nav_svc)

        # ── CH 10: BLUETOOTH (condizionato) ───────────────────────────────────
        if self.bluetooth_available:
            ch_bt = msg.channels.add()
            ch_bt.id = CH_BLUETOOTH
            bt_svc = BluetoothService()
            bt_svc.car_address = self.bt_address  # required
            bt_svc.supported_pairing_methods.append(
                BluetoothPairingMethod.Value("BLUETOOTH_PAIRING_PIN")
            )
            bt_svc.supported_pairing_methods.append(
                BluetoothPairingMethod.Value("BLUETOOTH_PAIRING_NUMERIC_COMPARISON")
            )
            ch_bt.bluetooth_service.CopyFrom(bt_svc)

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
