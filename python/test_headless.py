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
    
    # 4. Avvia manager USB
    hub = nemo_head_unit.UsbHubManager(runner)
    hub.start(usb_callback)
    
    # 5. Loop di attesa
    try:
        timeout = 60
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
