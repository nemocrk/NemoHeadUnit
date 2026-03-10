#!/usr/bin/env python3
"""
python/test_phase5_dump.py

Test Fase 5a — Dump NAL Units H.264

Obiettivo:
  Verificare che il canale video sia attivo e che Android Auto invii
  effettivamente NAL units H.264 validi. Il risultato e' un file
  `video_dump.h264` apribile con VLC per conferma visiva.

Funzionamento:
  Il dump e' scritto direttamente da VideoEventHandler in C++ (NO GIL).
  Python chiama usb.enable_video_dump(path) dopo la connessione,
  il C++ scrive i NAL units raw su file fino a 5 MB poi chiude.

Prerequisiti:
  - Fase 4 funzionante
  - Telefono Android connesso via USB con Android Auto abilitato
  - cmake -B build && cmake --build build -j$(nproc)

Verifica output:
  vlc --demux h264 video_dump.h264
  ffprobe -v quiet -show_streams -select_streams v video_dump.h264

NOTA: per il rendering visivo completo usare python/ui/main_window.py
"""

import sys
import os
import time

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_ROOT_DIR = os.path.dirname(_THIS_DIR)
for _p in [_ROOT_DIR, _THIS_DIR]:
    if _p not in sys.path:
        sys.path.insert(0, _p)

try:
    import build.nemo_head_unit as core
except ImportError as e:
    print(f"[ERRORE] {e}")
    print("Compila con: cmake -B build && cmake --build build -j$(nproc)")
    sys.exit(1)

try:
    from test_interactive_phase4 import InteractiveOrchestrator
except ImportError as e:
    print(f"[ERRORE] {e}")
    sys.exit(1)

DUMP_PATH  = os.path.join(_ROOT_DIR, "video_dump.h264")
DUMP_LIMIT = 5 * 1024 * 1024  # 5 MB — stesso limite di VideoEventHandler


def main():
    print("=" * 60)
    print(" TEST PHASE 5a — Dump NAL Units H.264")
    print(f" Output: {DUMP_PATH}")
    print(f" Limite: {DUMP_LIMIT // 1024} KB")
    print("=" * 60 + "\n")

    if hasattr(core, "enable_aasdk_logging"):
        core.enable_aasdk_logging()

    runner = core.IoContextRunner()
    crypto = core.CryptoManager()
    crypto.initialize()

    orchestrator = InteractiveOrchestrator(
        screen_width=800,
        screen_height=480,
    )

    usb = core.UsbHubManager(runner)
    usb.set_crypto_manager(crypto)
    usb.set_orchestrator(orchestrator)
    # Nessun GstVideoSink: modalita' dump puro (fakesink implicito in C++)

    dump_enabled = False

    def on_connect(ok: bool, msg: str):
        nonlocal dump_enabled
        status = "\u2713" if ok else "\u2717"
        print(f"[USB] {status} {msg}")
        if ok and not dump_enabled:
            print(f"[Dump] Abilitazione dump -> {DUMP_PATH}")
            usb.enable_video_dump(DUMP_PATH)
            dump_enabled = True
        elif not ok:
            runner.stop()

    usb.start(on_connect)
    runner.start()

    print("[INFO] Collega il telefono via USB. CTRL+C per uscire.")
    print("[INFO] Il dump si chiude automaticamente a 5 MB.\n")

    try:
        while True:
            time.sleep(2)
            if os.path.exists(DUMP_PATH):
                size_kb = os.path.getsize(DUMP_PATH) / 1024
                print(f"[Dump] {size_kb:7.1f} KB / {DUMP_LIMIT // 1024} KB", end="\r")
                if os.path.getsize(DUMP_PATH) >= DUMP_LIMIT:
                    print(f"\n[Dump] \u2713 Completato! {DUMP_PATH}")
                    print("[Dump] Verifica con:")
                    print(f"         vlc --demux h264 {DUMP_PATH}")
                    print(f"         ffprobe -show_streams -select_streams v {DUMP_PATH}")
                    break
    except KeyboardInterrupt:
        print("\n[Info] Interruzione manuale.")
    finally:
        usb.stop()
        runner.stop()
        print("[Info] Cleanup completato.")
        if os.path.exists(DUMP_PATH):
            size_kb = os.path.getsize(DUMP_PATH) / 1024
            print(f"[Info] Dump salvato: {size_kb:.1f} KB -> {DUMP_PATH}")


if __name__ == "__main__":
    main()
