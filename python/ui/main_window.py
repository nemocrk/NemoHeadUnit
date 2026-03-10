#!/usr/bin/env python3
"""
python/ui/main_window.py

Finestra principale NemoHeadUnit — Fase 5.

Architettura:
  - VideoWidget: widget PyQt6 con WA_NativeWindow che fornisce il WId X11
    a GStreamer. Python non tocca mai i buffer video.
  - MainWindow: coordina IoContextRunner, CryptoManager, GstVideoSink e
    UsbHubManager. Avvia la catena C++ 200 ms dopo show() per garantire
    che il WId nativo sia valido.
  - InteractiveOrchestrator: riuso completo dalla Fase 4. L'unico override
    è on_video_channel_open_request che trigga start_pipeline() sul sink C++.

Regola GIL:
  I buffer NAL units non attraversano MAI questa UI.
  Il flusso è esclusivamente:
    VideoEventHandler (C++) -> GstVideoSink::pushBuffer() -> xvimagesink -> WId

Avvio:
  python3 python/ui/main_window.py
  oppure con display virtuale:
  Xvfb :99 -screen 0 800x480x24 & DISPLAY=:99 python3 python/ui/main_window.py
"""

import sys
import os
import time

# ── Path setup ─────────────────────────────────────────────────────────────
_THIS_DIR   = os.path.dirname(os.path.abspath(__file__))
_PYTHON_DIR = os.path.dirname(_THIS_DIR)
_ROOT_DIR   = os.path.dirname(_PYTHON_DIR)
for _p in [_ROOT_DIR, _PYTHON_DIR]:
    if _p not in sys.path:
        sys.path.insert(0, _p)

# ── Import core C++ ────────────────────────────────────────────────────────
try:
    import build.nemo_head_unit as core
except ImportError as e:
    print(f"[ERRORE] Impossibile importare nemo_head_unit: {e}")
    print("Compila prima con: cmake -B build && cmake --build build -j$(nproc)")
    sys.exit(1)

# ── Import orchestrator Phase 4 (riuso completo) ───────────────────────────
try:
    from test_interactive_phase4 import InteractiveOrchestrator
except ImportError as e:
    print(f"[ERRORE] Impossibile importare InteractiveOrchestrator: {e}")
    sys.exit(1)

# ── Import PyQt6 ───────────────────────────────────────────────────────────
try:
    from PyQt6.QtWidgets import QApplication, QMainWindow, QWidget
    from PyQt6.QtCore    import Qt, QTimer
    from PyQt6.QtGui     import QPalette, QColor
except ImportError:
    print("[ERRORE] PyQt6 non trovato. Installa con: pip install PyQt6")
    sys.exit(1)


# ═══════════════════════════════════════════════════════════════════════════
# VideoWidget
# ═══════════════════════════════════════════════════════════════════════════

class VideoWidget(QWidget):
    """
    Widget con WA_NativeWindow che ospita il rendering GStreamer.

    CRITICO: WA_NativeWindow deve essere impostato PRIMA di show().
    La finestra X11 viene creata solo al momento di show(); dopo di essa
    winId() restituisce un valore valido non-zero.
    """

    def __init__(self, parent=None):
        super().__init__(parent)

        # Questi tre flag sono necessari affinché GStreamer possa scrivere
        # direttamente nell'area del widget bypassando Qt painter.
        self.setAttribute(Qt.WidgetAttribute.WA_NativeWindow,      True)
        self.setAttribute(Qt.WidgetAttribute.WA_PaintOnScreen,     True)
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent,  True)

        # Sfondo nero — visibile finché GStreamer non inizia a renderizzare
        palette = self.palette()
        palette.setColor(QPalette.ColorRole.Window, QColor(0, 0, 0))
        self.setPalette(palette)
        self.setAutoFillBackground(True)

    def get_window_id(self) -> int:
        """
        Restituisce il WId X11 nativo da passare a GstVideoSink.set_window_id().
        Chiamare SOLO dopo che il widget è stato mostrato (show()).
        Un valore 0 indica che la finestra X11 non è ancora stata creata.
        """
        wid = int(self.winId())
        if wid == 0:
            print("[VideoWidget] WARN: winId() == 0. "
                  "Assicurarsi che show() sia stato chiamato prima.")
        return wid


# ═══════════════════════════════════════════════════════════════════════════
# MainWindow
# ═══════════════════════════════════════════════════════════════════════════

class MainWindow(QMainWindow):
    """
    Finestra principale NemoHeadUnit.

    Flusso di avvio:
      1. show()                     -> crea la finestra X11 e il WId
      2. QTimer(200ms)              -> start_headunit() chiamato
      3. start_headunit()           -> ottiene WId, inizializza C++
      4. UsbHubManager.start()      -> avvia discovery USB
      5. on_video_channel_open_request() -> GstVideoSink.start_pipeline()
      6. GStreamer renderizza dentro VideoWidget
    """

    def __init__(self, width: int = 800, height: int = 480):
        super().__init__()
        self.setWindowTitle("NemoHeadUnit — Android Auto")
        self.setFixedSize(width, height)

        self._width  = width
        self._height = height

        # Widget video a schermo intero
        self._video_widget = VideoWidget(self)
        self._video_widget.setGeometry(0, 0, width, height)
        self.setCentralWidget(self._video_widget)

        # Riferimenti agli oggetti C++ (inizializzati in start_headunit)
        self._runner      = None
        self._usb_manager = None
        self._gst_sink    = None

    # ── Avvio della catena C++ ─────────────────────────────────────────────

    def start_headunit(self):
        """
        Inizializza e avvia l'intera catena C++.
        Chiamato via QTimer 200 ms dopo show() per garantire WId valido.
        """
        # 1. Ottieni WId nativo — deve essere != 0
        QApplication.processEvents()
        wid = self._video_widget.get_window_id()
        print(f"[UI] Window ID nativo: {wid}")
        if wid == 0:
            print("[UI] ERRORE: WId non valido. Riprovo tra 500ms...")
            QTimer.singleShot(500, self.start_headunit)
            return

        # 2. Event loop Boost.Asio (thread C++ separato)
        self._runner = core.IoContextRunner()

        # 3. Crypto
        crypto = core.CryptoManager()
        crypto.initialize()

        # 4. GStreamer Video Sink (C++) — non ancora avviato
        self._gst_sink = core.GstVideoSink(self._width, self._height)
        self._gst_sink.set_window_id(wid)
        print(f"[UI] GstVideoSink configurato ({self._width}x{self._height})")

        # 5. Orchestrator Python (Fase 4) + override Phase 5
        orchestrator = InteractiveOrchestrator(
            screen_width=self._width,
            screen_height=self._height,
        )

        # Override del placeholder Phase 5:
        # on_video_channel_open_request è il trigger per avviare GStreamer.
        gst_ref = self._gst_sink
        def _on_video_channel_open(payload: bytes) -> bytes:
            print("[UI] *** Video channel aperto → avvio GStreamer pipeline ***")
            gst_ref.start_pipeline()
            print(f"[UI] GStreamer running: {gst_ref.is_running()}")
            return b""
        orchestrator.on_video_channel_open_request = _on_video_channel_open

        # 6. UsbHubManager
        self._usb_manager = core.UsbHubManager(self._runner)
        self._usb_manager.set_crypto_manager(crypto)
        self._usb_manager.set_orchestrator(orchestrator)
        self._usb_manager.set_video_sink(self._gst_sink)  # <- Phase 5
        self._usb_manager.start(self._on_connect)

        # 7. Avvia io_context in thread C++ separato (non blocca il GIL)
        self._runner.start()
        print("[UI] IoContextRunner avviato. In attesa del telefono via USB...")

    def _on_connect(self, success: bool, message: str):
        if success:
            print(f"[UI] ✓ Connessione AOAP stabilita: {message}")
        else:
            print(f"[UI] ✗ Errore connessione: {message}")

    # ── Chiusura ───────────────────────────────────────────────────────────

    def closeEvent(self, event):
        print("[UI] Chiusura applicazione...")
        if self._gst_sink:
            self._gst_sink.stop()
        if self._usb_manager:
            self._usb_manager.stop()
        if self._runner:
            self._runner.stop()
        event.accept()


# ═══════════════════════════════════════════════════════════════════════════
# Entry point
# ═══════════════════════════════════════════════════════════════════════════

def main():
    print("\n" + "=" * 60)
    print(" NemoHeadUnit — Fase 5 — Video Rendering E2E")
    print(" Collega il telefono via USB dopo l'avvio della finestra.")
    print("=" * 60 + "\n")

    app = QApplication(sys.argv)

    window = MainWindow(width=800, height=480)
    window.show()

    # Delay per garantire che il WId X11 sia valido prima di inizializzare C++
    QTimer.singleShot(200, window.start_headunit)

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
