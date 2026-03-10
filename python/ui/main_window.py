#!/usr/bin/env python3
"""
python/ui/main_window.py

Finestra principale NemoHeadUnit — Fase 5.

Architettura:
  - VideoWidget   : widget PyQt6 con WA_NativeWindow che fornisce il WId X11
                    a GStreamer. Python non tocca mai i buffer video.
  - PipelineBridge: QObject con un Signal Qt — unico meccanismo thread-safe
                    per postare lavoro nel thread Qt da un thread Boost.Asio.
  - MainWindow    : coordina IoContextRunner, CryptoManager Python,
                    GstVideoSink e UsbHubManager.

Regola GIL:
  I buffer NAL units non attraversano MAI questa UI.
  Il flusso è esclusivamente:
    VideoEventHandler (C++) -> GstVideoSink::pushBuffer() -> xvimagesink -> WId

Perché PipelineBridge invece di QTimer.singleShot():
  on_video_channel_open_request è chiamato dal thread Boost.Asio C++.
  QTimer.singleShot() chiamato da un thread non-Qt NON è thread-safe:
  il timer viene creato nel thread sbagliato e il callback non viene mai
  eseguito nell'event loop Qt principale.
  La soluzione corretta è emettere un Signal Qt da qualsiasi thread:
  Qt garantisce che lo slot connesso venga eseguito nel thread owner
  dell'oggetto ricevente (AutoConnection -> QueuedConnection cross-thread).

Note sui flag Qt (CRITICI):
  - WA_NativeWindow   : crea una finestra X11 nativa con WId valido
  - WA_PaintOnScreen  : NON usare
  - WA_OpaquePaintEvent: NON usare
  - paintEngine()     : deve ritornare None

Avvio:
  DISPLAY=:0 python3 python/ui/main_window.py
"""

import sys
import os

# ── Path setup ──────────────────────────────────────────────────────────────
_THIS_DIR   = os.path.dirname(os.path.abspath(__file__))
_PYTHON_DIR = os.path.dirname(_THIS_DIR)
_ROOT_DIR   = os.path.dirname(_PYTHON_DIR)
for _p in [_ROOT_DIR, _PYTHON_DIR]:
    if _p not in sys.path:
        sys.path.insert(0, _p)

# ── Import core C++ ─────────────────────────────────────────────────────────
try:
    import build.nemo_head_unit as core
except ImportError as e:
    print(f"[ERRORE] Impossibile importare nemo_head_unit: {e}")
    print("Compila prima con: cmake -B build && cmake --build build -j$(nproc)")
    sys.exit(1)

# ── CryptoManager Python (sostituisce core.CryptoManager C++) ───────────────
try:
    from python.app.crypto_manager import CryptoManager
except ImportError:
    sys.path.insert(0, _ROOT_DIR)
    from python.app.crypto_manager import CryptoManager

# ── Import orchestrator ─────────────────────────────────────────────────────
try:
    from test_interactive_phase4 import InteractiveOrchestrator
except ImportError as e:
    print(f"[ERRORE] Impossibile importare InteractiveOrchestrator: {e}")
    sys.exit(1)

# ── Import PyQt6 ───────────────────────────────────────────────────────────
try:
    from PyQt6.QtWidgets import QApplication, QMainWindow, QWidget
    from PyQt6.QtCore    import Qt, QTimer, QObject, pyqtSignal
    from PyQt6.QtGui     import QPalette, QColor
except ImportError:
    print("[ERRORE] PyQt6 non trovato. Installa con: pip install PyQt6")
    sys.exit(1)


# ═══════════════════════════════════════════════════════════════════════════
# PipelineBridge
# ═══════════════════════════════════════════════════════════════════════════

class PipelineBridge(QObject):
    """
    Ponte thread-safe tra il thread Boost.Asio C++ e il thread Qt.

    Emettere pipeline_start_requested da qualsiasi thread è sicuro:
    Qt instraderà automaticamente la chiamata allo slot connesso nel
    thread principale (QueuedConnection) grazie all'AutoConnection.
    """
    pipeline_start_requested = pyqtSignal()


# ═══════════════════════════════════════════════════════════════════════════
# VideoWidget
# ═══════════════════════════════════════════════════════════════════════════

class VideoWidget(QWidget):
    """
    Widget nativo X11 che ospita il rendering diretto di GStreamer.
    WA_NativeWindow garantisce una subwindow X11 con WId proprio.
    paintEngine() ritorna None per delegare completamente il rendering
    a GStreamer/xvimagesink senza interferenze dal painter Qt.
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WidgetAttribute.WA_NativeWindow, True)
        palette = self.palette()
        palette.setColor(QPalette.ColorRole.Window, QColor(0, 0, 0))
        self.setPalette(palette)
        self.setAutoFillBackground(True)

    def paintEngine(self):
        return None

    def get_window_id(self) -> int:
        wid = int(self.winId())
        if wid == 0:
            print("[VideoWidget] WARN: winId() == 0 — show() non ancora chiamato.")
        return wid


# ═══════════════════════════════════════════════════════════════════════════
# MainWindow
# ═══════════════════════════════════════════════════════════════════════════

class MainWindow(QMainWindow):
    """
    Finestra principale NemoHeadUnit.

    Flusso di avvio:
      1. show() + processEvents()         -> WId X11 valido
      2. QTimer(200ms) -> start_headunit()
      3. start_headunit()                 -> CryptoManager Python carica PEM,
                                            crea PipelineBridge + GstVideoSink,
                                            chiama set_certificate_and_key()
      4. UsbHubManager.start()            -> discovery USB
      5. on_video_channel_open_request()  -> chiamato dal thread Boost.Asio
                                            -> bridge.pipeline_start_requested.emit()
      6. Qt consegna il signal allo slot  -> _start_pipeline() nel thread Qt
      7. GstVideoSink.start_pipeline()    -> xvimagesink riceve WId con
                                            dimensioni X11 già valide
      8. pushBuffer() dal thread Asio     -> GStreamer decodifica e renderizza
    """

    def __init__(self, width: int = 800, height: int = 480):
        super().__init__()
        self.setWindowTitle("NemoHeadUnit — Android Auto")
        self.setFixedSize(width, height)

        self._width  = width
        self._height = height

        self._video_widget = VideoWidget(self)
        self._video_widget.setGeometry(0, 0, width, height)
        self.setCentralWidget(self._video_widget)

        self._runner      = None
        self._usb_manager = None
        self._gst_sink    = None
        self._bridge      = None

    # ── Avvio catena C++ ───────────────────────────────────────────────────

    def start_headunit(self):
        QApplication.processEvents()
        wid = self._video_widget.get_window_id()
        print(f"[UI] Window ID nativo: {wid}")
        if wid == 0:
            print("[UI] ERRORE: WId non valido. Riprovo tra 500ms...")
            QTimer.singleShot(500, self.start_headunit)
            return

        self._runner = core.IoContextRunner()

        # ── CryptoManager Python: carica e valida PEM, passa al C++ ──────────
        crypto = CryptoManager()
        if not crypto.initialize():
            print("[UI] ERRORE CRITICO: certificati non trovati o non validi.")
            print("[UI] Assicurati che cert/headunit.crt e cert/headunit.key esistano.")
            return

        self._gst_sink = core.GstVideoSink(self._width, self._height)
        self._gst_sink.set_window_id(wid)
        print(f"[UI] GstVideoSink configurato ({self._width}x{self._height})")

        # ----------------------------------------------------------------
        # PipelineBridge: creato nel thread Qt, connesso nel thread Qt.
        # Il signal pipeline_start_requested può essere emesso da qualsiasi
        # thread — Qt consegna lo slot _start_pipeline nel thread Qt
        # grazie alla QueuedConnection automatica cross-thread.
        # ----------------------------------------------------------------
        self._bridge = PipelineBridge()
        self._bridge.pipeline_start_requested.connect(self._start_pipeline)
        print("[UI] PipelineBridge connesso (thread-safe cross-thread signal).")

        orchestrator = InteractiveOrchestrator(
            screen_width=self._width,
            screen_height=self._height,
        )

        bridge_ref = self._bridge

        def _on_video_channel_open(payload: bytes) -> bytes:
            """
            Chiamata dal thread Boost.Asio C++.
            NON chiamare start_pipeline() qui direttamente.
            Emette il signal Qt — thread-safe per definizione.
            """
            print("[UI] Video channel aperto → emit pipeline_start_requested")
            bridge_ref.pipeline_start_requested.emit()
            return b""

        orchestrator.on_video_channel_open_request = _on_video_channel_open

        self._usb_manager = core.UsbHubManager(self._runner)
        # ── Refactor: set_certificate_and_key invece di set_crypto_manager ───
        self._usb_manager.set_certificate_and_key(
            crypto.get_certificate(),
            crypto.get_private_key()
        )
        self._usb_manager.set_orchestrator(orchestrator)
        self._usb_manager.set_video_sink(self._gst_sink)
        self._usb_manager.start(self._on_connect)

        self._runner.start()
        print("[UI] IoContextRunner avviato. In attesa del telefono via USB...")

    def _start_pipeline(self):
        """
        Slot Qt — eseguito SEMPRE nel thread Qt principale.
        Sicuro chiamare start_pipeline() qui: xvimagesink legge le
        dimensioni X11 che sono già valide dopo show() + processEvents().
        """
        print("[UI] _start_pipeline() — thread Qt — avvio GStreamer...")
        if self._gst_sink:
            self._gst_sink.start_pipeline()
            print(f"[UI] GStreamer running: {self._gst_sink.is_running()}")
        else:
            print("[UI] ERRORE: _gst_sink è None in _start_pipeline()")

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
    QApplication.processEvents()

    QTimer.singleShot(200, window.start_headunit)

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
