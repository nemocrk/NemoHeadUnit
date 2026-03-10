#!/usr/bin/env python3
"""
python/test_phase5_dump.py

Test Fase 5a — Dump NAL Units H.264

Obiettivo:
  Verificare che il canale video sia attivo e che Android Auto invii
  effettivamente NAL units H.264. Il risultato è un file `video_dump.h264`
  apribile con VLC per conferma visiva.

Prerequisiti:
  - Fase 4 funzionante (test_interactive_phase4.py passa)
  - Telefono Android connesso via USB con Android Auto abilitato
  - CMake compilato: cmake -B build && cmake --build build -j$(nproc)

Modalità sink (senza finestra grafica):
  Per default usa fakesink — i frame vengono decodificati e scartati.
  Per usare un sink visivo impostare NEMO_VIDEO_SINK prima del lancio:
    NEMO_VIDEO_SINK=autovideosink python3 python/test_phase5_dump.py
    NEMO_VIDEO_SINK=waylandsink   python3 python/test_phase5_dump.py

Verifica output dump:
  vlc --demux h264 video_dump.h264
  oppure:
  ffprobe -v quiet -show_streams -select_streams v video_dump.h264

NOTA: per il rendering visivo completo usare python/ui/main_window.py.
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
DUMP_LIMIT = 5 * 1024 * 1024  # 5 MB


def _ensure_fakesink_if_headless():
    """
    Se WId=0 (test headless) e NEMO_VIDEO_SINK non è impostato,
    forza fakesink per evitare che autovideosink/xvimagesink tenti
    di aprire una finestra senza QApplication event loop.
    """
    env_sink = os.environ.get("NEMO_VIDEO_SINK", "")
    if not env_sink:
        print("[Test5a] Nessun NEMO_VIDEO_SINK impostato → uso fakesink (headless).")
        print("[Test5a] Per video visivo: NEMO_VIDEO_SINK=autovideosink (richiede DISPLAY)")
        os.environ["NEMO_VIDEO_SINK"] = "fakesink"
    else:
        print(f"[Test5a] Sink selezionato: {env_sink}")


def main():
    print("=" * 60)
    print(" TEST PHASE 5a — Dump NAL Units H.264")
    print(f" Output: {DUMP_PATH}")
    print(f" Limite: {DUMP_LIMIT // 1024} KB")
    print("=" * 60 + "\n")

    _ensure_fakesink_if_headless()

    if hasattr(core, "enable_aasdk_logging"):
        core.enable_aasdk_logging()

    runner = core.IoContextRunner()
    crypto = core.CryptoManager()
    crypto.initialize()

    orchestrator = InteractiveOrchestrator(
        screen_width=800,
        screen_height=480,
    )

    gst_sink = None
    if hasattr(core, "GstVideoSink"):
        gst_sink = core.GstVideoSink(800, 480)
        gst_sink.set_window_id(0)  # headless: WId=0, fakesink non usa overlay

    def _on_video_channel_open(payload: bytes) -> bytes:
        print("\n[Test5a] *** Video channel aperto ***")
        if gst_sink:
            print(f"[Test5a] Avvio GStreamer (sink={os.environ.get('NEMO_VIDEO_SINK', 'fakesink')})...")
            gst_sink.start_pipeline()
            print(f"[Test5a] GStreamer running: {gst_sink.is_running()}")
        else:
            print("[Test5a] GstVideoSink non disponibile — solo log NAL units.")
        return b""

    orchestrator.on_video_channel_open_request = _on_video_channel_open

    usb = core.UsbHubManager(runner)
    usb.set_crypto_manager(crypto)
    usb.set_orchestrator(orchestrator)
    if gst_sink:
        usb.set_video_sink(gst_sink)

    def _on_connect(ok: bool, msg: str):
        status = "✓" if ok else "✗"
        print(f"[USB] {status} {msg}")
        if not ok:
            runner.stop()

    usb.start(_on_connect)
    runner.start()

    print("[INFO] Collega il telefono via USB. CTRL+C per uscire.")
    print("[INFO] Monitoraggio avanzamento dump...\n")

    try:
        while True:
            time.sleep(2)
            if os.path.exists(DUMP_PATH):
                size_kb = os.path.getsize(DUMP_PATH) / 1024
                print(f"[Dump] {size_kb:7.1f} KB / {DUMP_LIMIT // 1024} KB", end="\r")
                if size_kb * 1024 >= DUMP_LIMIT:
                    print(f"\n[Dump] ✓ Completato! {DUMP_PATH}")
                    print("[Dump] Verifica con:")
                    print(f"         vlc --demux h264 {DUMP_PATH}")
                    print(f"         ffprobe -v quiet -show_streams -select_streams v {DUMP_PATH}")
                    break
    except KeyboardInterrupt:
        print("\n[Info] Interruzione manuale.")
    finally:
        if gst_sink:
            gst_sink.stop()
        usb.stop()
        runner.stop()
        print("[Info] Cleanup completato.")


if __name__ == "__main__":
    main()
