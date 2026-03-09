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
    # Correggiamo gli import in base alla reale struttura dei Protobuf generata
    from aasdk_proto.aap_protobuf.service.control.message import AuthResponse_pb2
    from aasdk_proto.aap_protobuf.service.control.message import ServiceDiscoveryResponse_pb2
    from aasdk_proto.aap_protobuf.service.control.message import PingResponse_pb2
    from aasdk_proto.aap_protobuf.service.control.message import AudioFocusNotification_pb2
    from aasdk_proto.aap_protobuf.service.control.message import ChannelOpenResponse_pb2
    from aasdk_proto.aap_protobuf.shared import MessageStatus_pb2
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    print(f"[ATTENZIONE] Impossibile importare moduli Protobuf: {e}")
    print("[ATTENZIONE] Assicurati di aver eseguito ./scripts/generate_protos.sh")
    PROTOBUF_AVAILABLE = False


class InteractiveOrchestrator:
    def __init__(self):
        self.cryptor = None
        self.handshake_done = False

    def set_cryptor(self, cryptor):
        print("[InteractiveOrchestrator] Cryptor inizializzato dal C++.")
        self.cryptor = cryptor

    def _prompt_send(self, label: str, data: bytes) -> bytes:
        print(f"\n{'='*50}")
        print(f"[Azione Richiesta: {label}]")
        print(f"Dimensione: {len(data)} bytes")
        if len(data) > 0:
            print(f"Preview Hex: {binascii.hexlify(data[:32]).decode()}...")
        print(f"{'='*50}")
        
        while True:
            cmd = input("Inviare questo chunk? [Invio]=Si, 'q'=Quit, 'd'=Dump Hex: ").strip().lower()
            if cmd == "q":
                print("Chiusura su richiesta dell'utente.")
                sys.exit(0)
            elif cmd == "d":
                print(binascii.hexlify(data).decode())
            else:
                return data

    def on_version_status(self, major: int, minor: int, status: int) -> bytes:
        print(f"\n[InteractiveOrchestrator] on_version_status: {major}.{minor} (status={status})")
        if status != 0:
            raise RuntimeError(f"Version negotiation fallita (status={status})")
        
        self.cryptor.do_handshake()
        out_chunk = self.cryptor.read_handshake_buffer()
        return self._prompt_send("Invia Primo Chunk TLS (Flight 1)", out_chunk)

    def on_handshake(self, payload: bytes) -> bytes:
        print(f"\n[InteractiveOrchestrator] Ricevuto Handshake payload ({len(payload)} bytes)")
        if payload:
            self.cryptor.write_handshake_buffer(payload)

        done = self.cryptor.do_handshake()
        out_chunk = self.cryptor.read_handshake_buffer()

        if not done:
            return self._prompt_send("Invia Chunk TLS Intermedio", out_chunk)

        print("\n[InteractiveOrchestrator] *** HANDSHAKE TLS COMPLETATO ***")
        self.handshake_done = True
        return b""

    def get_auth_complete_response(self) -> bytes:
        print("\n[InteractiveOrchestrator] Costruzione AuthResponse...")
        if not PROTOBUF_AVAILABLE: raise RuntimeError("Protobuf mancanti")
        
        msg = AuthResponse_pb2.AuthResponse()
        msg.status = 0 # status_success
        return self._prompt_send("Invia AuthCompleteResponse", msg.SerializeToString())

    def on_service_discovery_request(self, payload: bytes) -> bytes:
        print(f"\n[InteractiveOrchestrator] Service Discovery Request ricevuta!")
        if not PROTOBUF_AVAILABLE: raise RuntimeError("Protobuf mancanti")
            
        msg = ServiceDiscoveryResponse_pb2.ServiceDiscoveryResponse()
        # Ritorna vuota per ora (verranno aggiunti i display info più avanti)
        return self._prompt_send("Invia ServiceDiscoveryResponse (vuota)", msg.SerializeToString())

    def on_ping_request(self, payload: bytes) -> bytes:
        print("\n[InteractiveOrchestrator] Ricevuto Ping. Rispondo con Pong.")
        if not PROTOBUF_AVAILABLE: raise RuntimeError("Protobuf mancanti")
            
        msg = PingResponse_pb2.PingResponse()
        msg.timestamp = int(time.time() * 1000)
        return msg.SerializeToString()

    def on_audio_focus_request(self, payload: bytes) -> bytes:
        print("\n[InteractiveOrchestrator] AudioFocusRequest ricevuta.")
        if not PROTOBUF_AVAILABLE: raise RuntimeError("Protobuf mancanti")
        
        msg = AudioFocusNotification_pb2.AudioFocusNotification()
        # 1 = AUDIO_FOCUS_STATE_GAIN
        msg.audio_focus_state = 1 
        return self._prompt_send("Invia AudioFocusNotification", msg.SerializeToString())

    def on_video_channel_open_request(self, payload: bytes) -> bytes:
        print("\n[InteractiveOrchestrator] VideoChannelOpenRequest ricevuta.")
        if not PROTOBUF_AVAILABLE: raise RuntimeError("Protobuf mancanti")
        
        msg = ChannelOpenResponse_pb2.ChannelOpenResponse()
        msg.status = 0 # status success
        return self._prompt_send("Invia ChannelOpenResponse", msg.SerializeToString())


def main():
    runner = core.IoContextRunner()
    crypto = core.CryptoManager()
    crypto.initialize()
    usb = core.UsbHubManager(runner)
    orchestrator = InteractiveOrchestrator()
    usb.set_orchestrator(orchestrator)
    
    def on_connect(success, msg):
        if success:
            print("\n[Python Callback] AOAP Avviato:", msg)
        else:
            print("\n[Python Callback] Errore connessione:", msg)
            runner.stop()

    print("\n" + "*"*60)
    print("* TEST HEADLESS INTERATTIVO (ZERO FALLBACK)       *")
    print("* Collega un dispositivo Android via USB          *")
    print("*"*60 + "\n")

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
