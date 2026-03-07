import sys
import os
import unittest

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

class TestCoreBinding(unittest.TestCase):
    def test_import_and_hello_world(self):
        try:
            import nemo_head_unit
        except ImportError as e:
            # Stampa sys.path in caso di errore per debug
            paths = "\n".join(sys.path)
            self.fail(f"Impossibile importare nemo_head_unit.\nCompilazione CMake effettuata?\nSys Path attuali:\n{paths}\nDettagli: {e}")
        
        try:
            # Testiamo l'invocazione della funzione C++
            nemo_head_unit.hello_world()
        except Exception as e:
            self.fail(f"L'invocazione di nemo_head_unit.hello_world() ha generato un'eccezione: {e}")

if __name__ == '__main__':
    unittest.main()