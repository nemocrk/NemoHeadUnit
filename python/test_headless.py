import os
import sys
import time
import subprocess

# Aggiungiamo la cartella build ai percorsi di importazione per trovare il modulo nemo_head_unit.so
current_dir = os.path.dirname(os.path.abspath(__file__))
project_root = os.path.abspath(os.path.join(current_dir, '..'))
build_dir = os.path.join(project_root, 'build')
sys.path.append(build_dir)

try:
    import nemo_head_unit
except ImportError as e:
    print(f"Errore di importazione: {e}")
    print(f"Assicurati di aver compilato il progetto e che il modulo nemo_head_unit.so esista in {build_dir}.")
    sys.exit(1)


class HeadUnitOrchestrator:
    """
    Questa classe gestisce da Python l'handshake e l'orchestrazione dei messaggi Protobuf C++.
    Al momento restituiamo stringhe vuote per attivare il fallback C++ interno, 
    ma la pipeline di passaggio dei byte avanti/indietro e le chiamate di metodo sono fully-working.
    """
    def on_version_status(self, major: int, minor: int, status: int):
        print(f"[Python Orchestrator] Versione negoziata! ({major}.{minor} status: {status})")
        # Dopo questo, il C++ avviera' da solo la richiesta del primo chunk di Handshake.

    def on_handshake(self, payload: bytes) -> bytes:
        print(f"[Python Orchestrator] Handshake chunk in transito, delegando cryptor C++ (size: {len(payload)})")
        # Restituendo vuoto, diamo via libera al ControlEventHandler C++ di usare il Cryptor e 
        # autogestire il loop dei chunk. Quando i Protobuf Python saranno pronti, potremo decriptare 
        # da qua.
        return b""

    def on_service_discovery_request(self, payload: bytes) -> bytes:
        print(f"[Python Orchestrator] Ricevuta ServiceDiscoveryRequest! size: {len(payload)}")
        return b""

    def on_ping_request(self, payload: bytes) -> bytes:
        return b""

    def on_audio_focus_request(self, payload: bytes) -> bytes:
        print(f"[Python Orchestrator] Ricevuta AudioFocusRequest! size: {len(payload)}")
        return b""

    def on_video_channel_open_request(self, payload: bytes) -> bytes:
        print(f"[Python Orchestrator] Ricevuta Video ChannelOpenRequest! size: {len(payload)}")
        return b""


def generate_certificates():
    cert_dir = os.path.join(project_root, 'cert')
    crt_file = os.path.join(cert_dir, 'headunit.crt')
    key_file = os.path.join(cert_dir, 'headunit.key')

    if not os.path.exists(cert_dir):
        os.makedirs(cert_dir, exist_ok=True)

    if not os.path.exists(crt_file) or not os.path.exists(key_file):
        print(f"[CertGenerator] Certificati non trovati in {cert_dir}. Generazione automatica in corso...")
        
        cmd = [
            'openssl', 'req', '-x509', '-newkey', 'rsa:2048',
            '-keyout', key_file, '-out', crt_file,
            '-days', '365', '-nodes',
            '-subj', '/CN=NemoHeadUnit'
        ]
        
        try:
            subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            print("[CertGenerator] Certificati SSL generati con successo!")
        except subprocess.CalledProcessError as e:
            print(f"[CertGenerator] ERRORE durante la generazione dei certificati: {e.stderr.decode()}")
            sys.exit(1)
    else:
        print(f"[CertGenerator] Certificati trovati in {cert_dir}.")

def usb_callback(success: bool, message: str):
    if success:
        print(f"\n>>> ANDROID AUTO SESSION AVVIATA: {message} <<<\n")
    else:
        print(f"\n>>> ERRORE USB: {message} <<<\n")

def main():
    print("=== NemoHeadUnit Headless Integrator (Python) ===")
    
    # 1. Genera certificati se mancanti
    generate_certificates()
    
    # 2. Imposta root del progetto come CWD per far trovare "./cert/..." alla libreria AASDK
    os.chdir(project_root)
    
    print("In attesa di connessione USB Android...")
    
    # 3. Avvia runtime C++ (Event Loop)
    runner = nemo_head_unit.IoContextRunner()
    runner.start()
    
    # 4. Inizializza Orchestrator
    orchestrator = HeadUnitOrchestrator()
    
    # 5. Avvia manager USB agganciando Python Orchestrator
    hub = nemo_head_unit.UsbHubManager(runner)
    hub.set_orchestrator(orchestrator)
    hub.start(usb_callback)
    
    # 6. Loop di attesa (la UI prenderà il posto di questo in futuro)
    try:
        timeout = 600
        while timeout > 0:
            time.sleep(1)
            timeout -= 1
    except KeyboardInterrupt:
        print("\nInterruzione manuale.")
        
    print("\nSpegnimento servizi...")
    hub.stop()
    runner.stop()

if __name__ == '__main__':
    main()
