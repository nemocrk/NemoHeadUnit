#!/usr/bin/env python3
import sys
import os

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

try:
    import nemo_head_unit
    print("[Python] Modulo nemo_head_unit importato con successo.")
except ImportError as e:
    print(f"[Errore] Impossibile importare nemo_head_unit.\nAssicurati di aver compilato il progetto con CMake.\nDettagli: {e}")
    sys.exit(1)

def main():
    print("[Python] Test invocazione funzione C++ in corso...")
    # La funzione in C++ stampa sullo standard output
    nemo_head_unit.hello_world()
    print("[Python] Test completato.")

if __name__ == "__main__":
    main()
