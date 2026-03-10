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
    # Media sink
    from aasdk_proto.aap_protobuf.service.media.sink.MediaSinkService_pb2 import MediaSinkService
    from aasdk_proto.aap_protobuf.service.media.sink.message.VideoCodecResolutionType_pb2 import VideoCodecResolutionType
    from aasdk_proto.aap_protobuf.service.media.sink.message.VideoFrameRateType_pb2 import VideoFrameRateType
    from aasdk_proto.aap_protobuf.service.media.sink.message.DisplayType_pb2 import DisplayType
    # Sensor source
    from aasdk_proto.aap_protobuf.service.sensorsource.SensorSourceService_pb2 import SensorSourceService
    from aasdk_proto.aap_protobuf.service.sensorsource.message.Sensor_pb2 import Sensor
    from aasdk_proto.aap_protobuf.service.sensorsource.message.SensorType_pb2 import SensorType
    # Input source
    from aasdk_proto.aap_protobuf.service.inputsource.InputSourceService_pb2 import InputSourceService
    # Media source (microfono)
    from aasdk_proto.aap_protobuf.service.media.source.MediaSourceService_pb2 import MediaSourceService
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
#   9=MEDIA_SOURCE_MICROPHONE, 10=BLUETOOTH
CH_SENSOR        = 1
CH_VIDEO         = 3
CH_MEDIA_AUDIO   = 4
CH_SPEECH_AUDIO  = 5
CH_SYSTEM_AUDIO  = 6
CH_INPUT         = 8
CH_MIC           = 9


class InteractiveOrchestrator:
    def __init__(self):
        self.cryptor = None
        self.handshake_done = False

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
        Costruisce la ServiceDiscoveryResponse con TUTTI i canali obbligatori.

        Struttura ServiceConfiguration (Service_pb2.py, field names reali):
          .id                    → int32 (required)
          .sensor_source_service → SensorSourceService  (field 2)
          .media_sink_service    → MediaSinkService      (field 3)
          .input_source_service  → InputSourceService    (field 4)
          .media_source_service  → MediaSourceService    (field 5)

        Canali minimi richiesti da Android Auto:
          CH 1  = SENSOR_SOURCE       → sensor_source_service
          CH 3  = MEDIA_SINK_VIDEO    → media_sink_service (display_type=MAIN)
          CH 4  = MEDIA_SINK_AUDIO    → media_sink_service (display_type=NONE)
          CH 5  = SPEECH_AUDIO        → media_sink_service (display_type=NONE)
          CH 6  = SYSTEM_AUDIO        → media_sink_service (display_type=NONE)
          CH 8  = INPUT_SOURCE        → input_source_service
          CH 9  = MICROPHONE          → media_source_service
        """
        print("\n[Orchestrator] Service Discovery Request ricevuta!")

        msg = ServiceDiscoveryResponse_pb2.ServiceDiscoveryResponse()

        # ── HeadUnitInfo (sub-message moderno, field deprecated ignorati) ──────
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
        ch1 = msg.channels.add()
        ch1.id = CH_SENSOR
        svc_sensor = SensorSourceService()
        s1 = svc_sensor.sensors.add()
        s1.sensor_type = SensorType.Value("SENSOR_DRIVING_STATUS_DATA")
        s2 = svc_sensor.sensors.add()
        s2.sensor_type = SensorType.Value("SENSOR_NIGHT_MODE")
        ch1.sensor_source_service.CopyFrom(svc_sensor)

        # ── CH 3: VIDEO (MEDIA_SINK_VIDEO) ────────────────────────────────────
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

        # ── CH 4: MEDIA AUDIO (MEDIA_SINK_MEDIA_AUDIO) ───────────────────────
        ch4 = msg.channels.add()
        ch4.id = CH_MEDIA_AUDIO
        svc_media_audio = MediaSinkService()
        svc_media_audio.display_type = DisplayType.Value("DISPLAY_TYPE_NONE")
        ch4.media_sink_service.CopyFrom(svc_media_audio)

        # ── CH 5: SPEECH AUDIO (MEDIA_SINK_GUIDANCE_AUDIO) ───────────────────
        ch5 = msg.channels.add()
        ch5.id = CH_SPEECH_AUDIO
        svc_speech = MediaSinkService()
        svc_speech.display_type = DisplayType.Value("DISPLAY_TYPE_NONE")
        ch5.media_sink_service.CopyFrom(svc_speech)

        # ── CH 6: SYSTEM AUDIO (MEDIA_SINK_SYSTEM_AUDIO) ─────────────────────
        ch6 = msg.channels.add()
        ch6.id = CH_SYSTEM_AUDIO
        svc_sys = MediaSinkService()
        svc_sys.display_type = DisplayType.Value("DISPLAY_TYPE_NONE")
        ch6.media_sink_service.CopyFrom(svc_sys)

        # ── CH 8: INPUT SOURCE ────────────────────────────────────────────────
        # TouchScreen è nested message: InputSourceService.TouchScreen
        # field 'touchscreen' è repeated (non touchscreen_configs)
        ch8 = msg.channels.add()
        ch8.id = CH_INPUT
        svc_input = InputSourceService()
        ts = svc_input.touchscreen.add()
        ts.width  = 800
        ts.height = 480
        # ts.type default = TOUCHSCREEN_TYPE_CAPACITIVE (non serve impostarlo)
        ch8.input_source_service.CopyFrom(svc_input)

        # ── CH 9: MICROPHONE (MEDIA_SOURCE_MICROPHONE) ────────────────────────
        ch9 = msg.channels.add()
        ch9.id = CH_MIC
        svc_mic = MediaSourceService()
        ch9.media_source_service.CopyFrom(svc_mic)

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

    orchestrator = InteractiveOrchestrator()
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
