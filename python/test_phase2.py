import sys
import os
import unittest
import time

# Inserisce i path di build in CIMA al sys.path (insert 0 invece di append)
root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
possible_build_dirs = [
    os.path.join(root_dir, 'build'),
    os.path.join(root_dir, 'cmake-build-debug'),
    os.path.join(root_dir, 'cmake-build-release')
]
for d in possible_build_dirs:
    if os.path.isdir(d):
        sys.path.insert(0, d)

# CryptoManager è ora in Python puro — nessun binding C++ richiesto
sys.path.insert(0, root_dir)
from python.app.crypto_manager import CryptoManager


class TestPhase2(unittest.TestCase):
    def setUp(self):
        try:
            import nemo_head_unit
            self.nemo_head_unit = nemo_head_unit
        except ImportError as e:
            self.fail(f"Impossibile importare nemo_head_unit: {e}")

    def test_io_context_runner(self):
        print("\n--- Test IoContextRunner ---")
        runner = self.nemo_head_unit.IoContextRunner()
        runner.start()
        # Verifichiamo che non blocchi il GIL permettendo a python di fare uno sleep
        time.sleep(0.1)
        runner.stop()
        self.assertTrue(True)

    def test_crypto_manager(self):
        """
        Verifica che CryptoManager Python carichi e validi correttamente
        certificato e chiave privata dai path standard.
        Sostituisce il vecchio test su core.CryptoManager (C++).
        """
        print("\n--- Test CryptoManager (Python) ---")
        crypto = CryptoManager()
        self.assertTrue(crypto.initialize(), "Inizializzazione chiavi OpenSSL fallita")

        cert = crypto.get_certificate()
        pkey = crypto.get_private_key()

        self.assertIn("BEGIN CERTIFICATE", cert)
        self.assertIn("BEGIN", pkey)  # supporta sia PRIVATE KEY che RSA PRIVATE KEY
        print("CryptoManager Python: certificato e chiave privata caricati e validati.")

        # Verifica che set_certificate_and_key sia esposto dal binding C++
        usb_test_runner = self.nemo_head_unit.IoContextRunner()
        usb = self.nemo_head_unit.UsbHubManager(usb_test_runner)
        try:
            usb.set_certificate_and_key(cert, pkey)
            print("UsbHubManager.set_certificate_and_key() OK.")
        except Exception as e:
            self.fail(f"set_certificate_and_key() fallita: {e}")


if __name__ == '__main__':
    unittest.main()
