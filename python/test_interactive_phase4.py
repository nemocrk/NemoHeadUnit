import sys
import os
import binascii
import time

# Aggiungi il percorso corrente e la cartella parent per trovare i moduli generati
current_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.dirname(current_dir))
sys.path.append(current_dir)

try:
    import build.nemo_head_unit as core
except ImportError as e:
    print(f"Errore import: {e}. Assicurati di aver compilato con cmake -B build && cmake --build build")
    sys.exit(1)

# Importa i moduli protobuf (devono essere stati generati con scripts/generate_protos.sh)
try:
    from aasdk_proto import AuthCompleteIndicationMessage_pb2
    from aasdk_proto import StatusEnum_pb2
    from aasdk_proto import ServiceDiscoveryResponseMessage_pb2
    from aasdk_proto import PingResponseMessage_pb2
    PROTOBUF_AVAILABLE = True
except ImportError as e:
    print(f"[ATTENZIONE] Impossibile importare moduli Protobuf: {e}")
    print("[ATTENZIONE] Eseguire prima: ./scripts/generate_protos.sh")
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
        print(f"Dimensione chunk in uscita: {len(data)} bytes")
        if len(data) > 0:
            print(f"Preview Hex: {binascii.hexlify(data[:32]).decode()}...")
        print(f"{'='*50}")
        
        while True:
            cmd = input("Inviare questo chunk? [Invio]=Si, 'q'=Quit, 'd'=Dump Hex completo: ").strip().lower()
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
        if self.cryptor is None:
            raise RuntimeError("Cryptor non disponibile")

        # Avvia l'handshake e produce il flight 1
        self.cryptor.do_handshake()
        out_chunk = self.cryptor.read_handshake_buffer()
        if not out_chunk:
            raise RuntimeError("Errore: start handshake ha prodotto un chunk vuoto.")
            
        return self._prompt_send("Invia Primo Chunk TLS (Flight 1)", out_chunk)

    def on_handshake(self, payload: bytes) -> bytes:
        print(f"\n[InteractiveOrchestrator] Ricevuto Handshake payload ({len(payload)} bytes)")
        if self.cryptor is None:
            raise RuntimeError("Cryptor non disponibile")

        if payload:
            self.cryptor.write_handshake_buffer(payload)

        # Avanza la state machine OpenSSL
        done = self.cryptor.do_handshake()
        out_chunk = self.cryptor.read_handshake_buffer()

        if not done:
            if not out_chunk:
                raise RuntimeError("Errore: handshake non terminato ma OpenSSL non ha prodotto output.")
            return self._prompt_send("Invia Chunk TLS Intermedio", out_chunk)

        print("\n[InteractiveOrchestrator] *** HANDSHAKE TLS COMPLETATO ***")
        self.handshake_done = True
        return b""  # Segnala al C++ che l'handshake è terminato

    def get_auth_complete_response(self) -> bytes:
        print("\n[InteractiveOrchestrator] Costruzione AuthCompleteRequest Protobuf...")
        if not self.handshake_done:
            raise RuntimeError("Richiesto AuthComplete ma handshake non è terminato!")
            
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError("Impossibile creare AuthComplete: moduli protobuf mancanti.")

        # Secondo aasdk AuthComplete in control channel può essere AuthCompleteIndication 
        # (nota: controlla il payload esatto richiesto da aap_protobuf::...::AuthResponse nel tuo repo)
        try:
            # Assumiamo che il C++ si aspetti un message simile a AuthCompleteIndication
            # o quello che hai mappato per AuthResponse.
            msg = AuthCompleteIndicationMessage_pb2.AuthCompleteIndication()
            msg.status = StatusEnum_pb2.Status.STATUS_SUCCESS
            payload = msg.SerializeToString()
            return self._prompt_send("Invia AuthCompleteResponse (Protobuf)", payload)
        except Exception as e:
            print(f"Errore nella serializzazione AuthComplete: {e}")
            raise

    def on_service_discovery_request(self, payload: bytes) -> bytes:
        print(f"\n[InteractiveOrchestrator] Service Discovery Request ricevuta!")
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError("Protobuf mancanti")
            
        msg = ServiceDiscoveryResponseMessage_pb2.ServiceDiscoveryResponse()
        msg.status = StatusEnum_pb2.Status.STATUS_SUCCESS
        # TODO: Aggiungere Services reali
        return self._prompt_send("Invia ServiceDiscoveryResponse (vuota)", msg.SerializeToString())

    def on_ping_request(self, payload: bytes) -> bytes:
        print("\n[InteractiveOrchestrator] Ricevuto Ping. Rispondo con Pong.")
        if not PROTOBUF_AVAILABLE:
            raise RuntimeError("Protobuf mancanti")
            
        msg = PingResponseMessage_pb2.PingResponse()
        msg.timestamp = int(time.time() * 1000)
        return msg.SerializeToString()

    def on_audio_focus_request(self, payload: bytes) -> bytes:
        print("\n[InteractiveOrchestrator] AudioFocusRequest. Generazione mock in corso...")
        raise NotImplementedError("Costruisci AudioFocusNotification protobuf")

    def on_video_channel_open_request(self, payload: bytes) -> bytes:
        print("\n[InteractiveOrchestrator] VideoChannelOpenRequest. Generazione mock in corso...")
        raise NotImplementedError("Costruisci ChannelOpenResponse protobuf")


def main():
    runner = core.IoContextRunner()
    
    # Ignoriamo i warning crypto dummy per il test
    crypto = core.CryptoManager()
    crypto.initialize()

    usb = core.UsbHubManager(runner)
    
    orchestrator = InteractiveOrchestrator()
    usb.set_orchestrator(orchestrator)
    
    def on_connect(success, msg):
        if success:
            print("[Python Callback] AOAP Avviato:", msg)
        else:
            print("[Python Callback] Errore connessione:", msg)
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
