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

Note sui flag Qt (CRITICI):
  - WA_NativeWindow      : crea una finestra X11 nativa con WId valido
  - WA_PaintOnScreen     : NON usare — causa "paintEngine: Should no longer
                           be called" perché Qt tenta il painter su un widget
                           nativo senza backing store.
  - WA_OpaquePaintEvent  : NON usare — stesso problema.
  - paintEngine()        : deve ritornare nullptr per segnalare a Qt che
                           il widget è gestito esternamente (da GStreamer).

Avvio:
  DISPLAY=:0 python3 python/ui/main_window.py
  oppure con display virtuale:
  Xvfb :99 -screen 0 800x480x24 & DISPLAY=:99 python3 python/ui/main_window.py
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

# ── Import orchestrator Phase 4 ────────────────────────────────────────────
try:
    from test_interactive_phase4 import InteractiveOrchestrator
except ImportError as e:
    print(f"[ERRORE] Impossibile importare InteractiveOrchestrator: {e}")
    sys.exit(1)

# ── Import PyQt6 ───────────────────────────────────────────────────────────
try:
    from PyQt6.QtWidgets import QApplication, QMainWindow, QWidget
    from PyQt6.QtCore    import Qt, QTimer
    from PyQt6.QtGui     import QPalette, QColor, QPaintEngine
except ImportError:
    print("[ERRORE] PyQt6 non trovato. Installa con: pip install PyQt6")
    sys.exit(1)


# ═══════════════════════════════════════════════════════════════════════════
# VideoWidget
# ═══════════════════════════════════════════════════════════════════════════

class VideoWidget(QWidget):
    """
    Widget nativo X11 che ospita il rendering diretto di GStreamer.

    Flag usati:
      WA_NativeWindow  -> crea subwindow X11 con WId proprio (obbligatorio
                          per gst_video_overlay_set_window_handle)

    Flag VOLUTAMENTE OMESSI:
      WA_PaintOnScreen    -> causerebbe "paintEngine: Should no longer be
                             called" perché Qt tenta di usare il painter
                             su una finestra gestita da GStreamer.
      WA_OpaquePaintEvent -> stesso problema.

    paintEngine() ritorna None per comunicare a Qt che questo widget
    non ha un paint engine Qt — il rendering è completamente delegato
    a GStreamer/xvimagesink.
    """

    def __init__(self, parent=None):
        super().__init__(parent)
        # Solo WA_NativeWindow: crea la subwindow X11 con WId valido
        self.setAttribute(Qt.WidgetAttribute.WA_NativeWindow, True)
        # Sfondo nero — visibile finché GStreamer non inizia a renderizzare
        palette = self.palette()
        palette.setColor(QPalette.ColorRole.Window, QColor(0, 0, 0))
        self.setPalette(palette)
        self.setAutoFillBackground(True)

    def paintEngine(self):
        """
        Ritorna None per segnalare a Qt che questo widget non ha un
        paint engine interno. Senza questo override Qt chiama il painter
        di default e stampa "Should no longer be called" in loop.
        """
        return None

    def get_window_id(self) -> int:
        """
        Restituisce il WId X11 nativo.
        Chiamare SOLO dopo show() + processEvents().
        Ritorna 0 se la finestra X11 non è ancora stata creata.
        """
        wid = int(self.winId())
        if wid == 0:
            print("[VideoWidget] WARN: winId() == 0 — "
                  "assicurarsi che show() sia stato chiamato prima.")
        return wid


# ═══════════════════════════════════════════════════════════════════════════
# MainWindow
# ═══════════════════════════════════════════════════════════════════════════

class MainWindow(QMainWindow):
    """
    Finestra principale NemoHeadUnit.

    Flusso di avvio:
      1. show() + processEvents()    -> crea finestra X11, WId valido
      2. QTimer(200ms)               -> start_headunit()
      3. start_headunit()            -> configura GstVideoSink con WId
      4. UsbHubManager.start()       -> discovery USB
      5. on_video_channel_open_request() chiamato da thread Boost.Asio
         -> schedula start_pipeline() nel thread Qt via QTimer.singleShot(0)
      6. start_pipeline() eseguito nel thread Qt -> xvimagesink ha già
         ricevuto l'expose event con dimensioni valide (h != 0)
      7. GStreamer renderizza dentro VideoWidget

    CRITICO — perché QTimer.singleShot(0) per start_pipeline():
      on_video_channel_open_request è chiamato dal thread Boost.Asio C++.
      gst_element_set_state(PLAYING) internamente chiama xvimagesink che
      tenta di leggere le dimensioni della finestra X11. Se la chiamata
      avviene prima che Qt abbia processato l'expose event iniziale,
      la finestra ha height=0 e GStreamer abortisce con:
        'gst_video_center_rect: assertion src->h != 0 failed'
      QTimer.singleShot(0) rimanda l'esecuzione al prossimo tick
      dell'event loop Qt, garantendo che il widget sia completamente
      renderizzato con dimensioni valide.
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

    # ── Avvio catena C++ ───────────────────────────────────────────────────

    def start_headunit(self):
        """
        Inizializza la catena C++. Chiamato 200ms dopo show() via QTimer.
        """
        QApplication.processEvents()
        wid = self._video_widget.get_window_id()
        print(f"[UI] Window ID nativo: {wid}")
        if wid == 0:
            print("[UI] ERRORE: WId non valido. Riprovo tra 500ms...")
            QTimer.singleShot(500, self.start_headunit)
            return

        self._runner = core.IoContextRunner()

        crypto = core.CryptoManager()
        crypto.initialize()

        self._gst_sink = core.GstVideoSink(self._width, self._height)
        self._gst_sink.set_window_id(wid)
        print(f"[UI] GstVideoSink configurato ({self._width}x{self._height})")

        orchestrator = InteractiveOrchestrator(
            screen_width=self._width,
            screen_height=self._height,
        )

        # ----------------------------------------------------------------
        # Override on_video_channel_open_request — Phase 5
        #
        # IMPORTANTE: questa callback è chiamata dal thread Boost.Asio C++.
        # NON chiamare start_pipeline() direttamente qui: xvimagesink
        # leggerebbe le dimensioni X11 prima dell'expose event -> h==0.
        #
        # Soluzione: QTimer.singleShot(0) rimanda start_pipeline() al
        # prossimo tick dell'event loop Qt (thread principale), dove le
        # dimensioni del widget sono già valide.
        # ----------------------------------------------------------------
        gst_ref = self._gst_sink

        def _on_video_channel_open(payload: bytes) -> bytes:
            print("[UI] *** Video channel aperto → schedule start_pipeline() nel thread Qt ***")
            QTimer.singleShot(0, _start_pipeline_in_qt_thread)
            return b""

        def _start_pipeline_in_qt_thread():
            print("[UI] start_pipeline() — thread Qt — avvio GStreamer...")
            gst_ref.start_pipeline()
            print(f"[UI] GStreamer running: {gst_ref.is_running()}")

        orchestrator.on_video_channel_open_request = _on_video_channel_open

        self._usb_manager = core.UsbHubManager(self._runner)
        self._usb_manager.set_crypto_manager(crypto)
        self._usb_manager.set_orchestrator(orchestrator)
        self._usb_manager.set_video_sink(self._gst_sink)
        self._usb_manager.start(self._on_connect)

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
    # processEvents() garantisce che la finestra X11 sia completamente
    # creata e il WId sia valido prima di avviare start_headunit()
    QApplication.processEvents()

    QTimer.singleShot(200, window.start_headunit)

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
