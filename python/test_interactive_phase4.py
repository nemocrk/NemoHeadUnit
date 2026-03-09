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
    from aasdk_proto.aap_protobuf.service.control.message import PingResponse_pb2
    from aasdk_proto.aap_protobuf.service.control.message import AudioFocusNotification_pb2
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    # Service schema reale: ServiceConfiguration (non "Service")
    from aasdk_proto.aap_protobuf.service.Service_pb2 import ServiceConfiguration
    from aasdk_proto.aap_protobuf.service.media.sink.MediaSinkService_pb2 import MediaSinkService
    from aasdk_proto.aap_protobuf.service.media.sink.message.VideoConfiguration_pb2 import VideoConfiguration
    from aasdk_proto.aap_protobuf.service.media.sink.message.VideoCodecResolutionType_pb2 import VideoCodecResolutionType
    from aasdk_proto.aap_protobuf.service.media.sink.message.VideoFrameRateType_pb2 import VideoFrameRateType
    from aasdk_proto.aap_protobuf.service.media.sink.message.DisplayType_pb2 import DisplayType
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    print(f"\n[ERRORE CRITICO] Moduli Protobuf non trovati: {e}")
    print("1) pip install protobuf")
    print("2) ./scripts/generate_protos.sh")
    sys.exit(1)


# Mappa ChannelId enum aasdk (posizionale, da ChannelId.hpp):
#   0=CONTROL, 1=SENSOR, 2=MEDIA_SINK, 3=MEDIA_SINK_VIDEO,
#   4=MEDIA_SINK_MEDIA_AUDIO, 5=MEDIA_SINK_GUIDANCE_AUDIO,
#   6=MEDIA_SINK_SYSTEM_AUDIO, 7=MEDIA_SINK_TELEPHONY_AUDIO,
#   8=INPUT_SOURCE, 9=MEDIA_SOURCE_MICROPHONE, 10=BLUETOOTH, ...
CH_MEDIA_SINK_VIDEO = 3


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
        ServiceConfiguration.id deve corrispondere all'enum ChannelId di aasdk:
          MEDIA_SINK_VIDEO = 3  (posizione nell'enum ChannelId.hpp)
        Il Messenger di aasdk smista i frame in base a questo id numerico.
        """
        print("\n[Orchestrator] Service Discovery Request ricevuta!")

        msg = ServiceDiscoveryResponse_pb2.ServiceDiscoveryResponse()
        msg.make = "NemoDev"
        msg.model = "NemoHU v0.1"
        msg.year = "2024"
        msg.head_unit_make = "NemoDev"
        msg.head_unit_model = "NemoHU"
        msg.head_unit_software_build = "phase4"
        msg.head_unit_software_version = "0.1.0"
        msg.can_play_native_media_during_vr = False

        # Canale video: id=3 = MEDIA_SINK_VIDEO nell'enum aasdk
        ch = msg.channels.add()
        ch.id = CH_MEDIA_SINK_VIDEO  # 3

        sink = MediaSinkService()
        sink.display_type = DisplayType.Value("DISPLAY_TYPE_MAIN")
        sink.available_while_in_call = True

        vid_cfg = sink.video_configs.add()
        vid_cfg.codec_resolution = VideoCodecResolutionType.Value("VIDEO_800x480")
        vid_cfg.frame_rate = VideoFrameRateType.Value("VIDEO_FPS_30")
        vid_cfg.density = 140
        vid_cfg.width_margin = 0
        vid_cfg.height_margin = 0

        ch.media_sink_service.CopyFrom(sink)

        serialized = msg.SerializeToString()
        return self._log_and_send(
            f"Invia ServiceDiscoveryResponse (ch={CH_MEDIA_SINK_VIDEO}=MEDIA_SINK_VIDEO)",
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
