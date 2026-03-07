import sys
import os
import unittest

# Aggiunge i path di build comuni (build, cmake-build-debug, etc.) al sys.path
current_dir = os.path.dirname(os.path.abspath(__file__))
possible_build_dirs = [
    os.path.join(current_dir, '..', 'build'),
    os.path.join(current_dir, '..', 'cmake-build-debug'),
    os.path.join(current_dir, '..', 'cmake-build-release')
]

for d in possible_build_dirs:
    if os.path.isdir(d):
        sys.path.append(d)

class TestCoreBinding(unittest.TestCase):
    def test_import_and_hello_world(self):
        try:
            import nemo_head_unit
        except ImportError as e:
            self.fail(f"Impossibile importare nemo_head_unit. Compilazione mancante? Dettagli: {e}")
        
        try:
            # Testiamo l'invocazione della funzione C++
            nemo_head_unit.hello_world()
        except Exception as e:
            self.fail(f"L'invocazione di nemo_head_unit.hello_world() ha generato un'eccezione: {e}")

if __name__ == '__main__':
    unittest.main()