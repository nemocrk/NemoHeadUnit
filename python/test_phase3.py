import sys
import os
import unittest
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

class TestPhase3(unittest.TestCase):
    def setUp(self):
        try:
            import nemo_head_unit
            self.nemo_head_unit = nemo_head_unit
        except ImportError as e:
            self.fail(f"Impossibile importare nemo_head_unit: {e}")
            
    def test_usb_discovery_lifecycle(self):
        print("\n--- Test USB Hub Lifecycle ---")
        runner = self.nemo_head_unit.IoContextRunner()
        runner.start()
        
        manager = self.nemo_head_unit.UsbHubManager(runner)
        
        # Funzione callback vuota per il test (non abbiamo un vero device qui)
        def on_device(success, msg):
            pass
            
        started = manager.start(on_device)
        self.assertTrue(started, "Inizializzazione manager libusb fallita (permessi errati?)")
        
        # Lasciamo girare il loop USB per un decimo di secondo
        time.sleep(0.1)
        
        # Test di clean teardown, per scongiurare Segfault in Boost.Asio
        manager.stop()
        runner.stop()
        self.assertTrue(True)

if __name__ == '__main__':
    unittest.main()