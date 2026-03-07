import sys
import os
import time

root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
possible_build_dirs = [
    os.path.join(root_dir, 'build'),
    os.path.join(root_dir, 'cmake-build-debug'),
    os.path.join(root_dir, 'cmake-build-release')
]

for d in possible_build_dirs:
    if os.path.isdir(d):
        sys.path.insert(0, d)

try:
    import nemo_head_unit
except ImportError as e:
    print(f"Impossibile importare nemo_head_unit: {e}")
    sys.exit(1)

print("\n--- Test Interattivo USB Hub ---")
print("Collega uno smartphone Android alla porta USB.")
print("Assicurati che l'app Android Auto sia installata e attiva.")
print("Il test attenderà 30 secondi il rilevamento del dispositivo.\n")

runner = nemo_head_unit.IoContextRunner()
runner.start()

manager = nemo_head_unit.UsbHubManager(runner)
device_found = False

def on_device(success, msg):
    global device_found
    device_found = True
    if success:
        print(f"\n[SUCCESS] -> {msg}")
        print("Il telefono è entrato in modalità Android Open Accessory (AOAP)!")
    else:
        print(f"\n[ERROR] -> {msg}")

started = manager.start(on_device)
if not started:
    print("Errore inizializzazione USB. Esegui lo script con i privilegi di root (sudo) o imposta le regole udev.")
    manager.stop()
    runner.stop()
    sys.exit(1)

# Attesa bloccante fino al rilevamento o timeout
timeout = 30
while timeout > 0 and not device_found:
    time.sleep(1)
    timeout -= 1
    if timeout % 5 == 0:
        print(f"... in attesa ({timeout}s rimanenti)")

if not device_found:
    print("\n[TIMEOUT] Nessun dispositivo compatibile rilevato.")

print("\nChiusura dei servizi...")
manager.stop()
runner.stop()
print("Test terminato.")
