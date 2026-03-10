# Fase 5 — Video Channel e Rendering E2E

> **Stato:** Pronto per implementazione  
> **Prerequisito:** Fase 4 completata — tutti i canali base sono negoziati, `on_video_channel_open_request` è raggiunto e restituisce `b""`  
> **Obiettivo:** Ricevere NAL units H.264 da Android Auto, decodificarle tramite GStreamer C++ e renderizzare il video dentro un widget PyQt6, **senza mai attraversare il GIL con i buffer media**.

---

## Indice

1. [Architettura E2E della Fase 5](#1-architettura-e2e)
2. [Step 8 — Estrazione e dump NAL Units H.264](#2-step-8--estrazione-e-dump-nal-units-h264)
3. [Step 9 — Pipeline GStreamer C++](#3-step-9--pipeline-gstreamer-c)
4. [Step 10 — Finestra PyQt6 e Window ID](#4-step-10--finestra-pyqt6-e-window-id)
5. [Modifiche CMakeLists.txt](#5-modifiche-cmakeliststxt)
6. [Binding pybind11 — nuove API](#6-binding-pybind11--nuove-api)
7. [Script di test Phase 5](#7-script-di-test-phase-5)
8. [Criteri di accettazione](#8-criteri-di-accettazione)
9. [Troubleshooting frequente](#9-troubleshooting-frequente)

---

## 1. Architettura E2E

```
[Telefono Android]
      │  USB / AOAP
      ▼
[aasdk Messenger C++]
      │  onMediaWithTimestampIndication(ts, DataConstBuffer)
      ▼
[VideoEventHandler::onMediaWithTimestampIndication]   ← src/session/video_event_handler.hpp
      │  NAL unit (zero-copy, puntatore al buffer aasdk)
      ▼
[GstPipeline::pushBuffer(ts, data, size)]             ← src/gst/gst_pipeline.hpp  (NUOVO)
      │  appsrc → h264parse → avdec_h264
      ▼                            ▼
[GStreamer: decodifica HW/SW]   [autovideosink / xvimagesink / glimagesink]
      │                              │
      │                   Window ID (WId) passato da Python via pybind11
      ▼
[Widget PyQt6]  ← python/ui/main_window.py  (NUOVO)
```

**Regola fondamentale (invariante di architettura):**  
I buffer video non attraversano MAI il confine C++/Python. Python passa solo un intero (`WId`) a C++ all'avvio; da quel momento in poi GStreamer scrive direttamente nella finestra X11/Wayland/OpenGL del widget.

---

## 2. Step 8 — Estrazione e dump NAL Units H.264

### 2.1 Obiettivo

Verificare che `onMediaWithTimestampIndication` riceva dati H.264 validi prima di collegare GStreamer. Scrivere un file `video_dump.h264` apribile con VLC (`vlc --demux h264 video_dump.h264`).

### 2.2 Modifica `video_event_handler.hpp`

La callback esiste già. Rimuovere il `(void)` e aggiungere la scrittura su file:

```cpp
// src/session/video_event_handler.hpp
// Aggiungere in cima alla classe, nella sezione private:
private:
    std::ofstream dump_file_;
    bool          dump_enabled_ = false;
    std::size_t   dump_bytes_   = 0;
    static constexpr std::size_t DUMP_LIMIT = 5 * 1024 * 1024; // 5 MB

public:
    // Chiamare da Python prima di avviare lo stream per attivare il dump
    void enableDump(const std::string &path)
    {
        dump_file_.open(path, std::ios::binary | std::ios::trunc);
        if (dump_file_.is_open())
        {
            dump_enabled_ = true;
            std::cout << "[Video] Dump H.264 abilitato: " << path << std::endl;
        }
    }
```

Modificare `onMediaWithTimestampIndication`:

```cpp
void onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType ts,
                                    const aasdk::common::DataConstBuffer &buffer) override
{
    std::cout << "[Video] NAL unit ts=" << ts
              << " size=" << buffer.size << " bytes" << std::endl;

    // ── Step 8: dump su file per verifica VLC ───────────────────────
    if (dump_enabled_ && dump_file_.is_open() && dump_bytes_ < DUMP_LIMIT)
    {
        dump_file_.write(
            reinterpret_cast<const char *>(buffer.data),
            static_cast<std::streamsize>(buffer.size));
        dump_bytes_ += buffer.size;
        if (dump_bytes_ >= DUMP_LIMIT)
        {
            dump_file_.close();
            dump_enabled_ = false;
            std::cout << "[Video] Dump completato (" << DUMP_LIMIT / 1024
                      << " KB). Apri con: vlc --demux h264 video_dump.h264" << std::endl;
        }
    }

    // ── Step 9: invia a GStreamer (attivato in Phase 5b) ─────────────
    // if (pipeline_) pipeline_->pushBuffer(ts, buffer.data, buffer.size);

    channel_->receive(this->shared_from_this());
}
```

### 2.3 Verifica dump

```bash
# Apri il dump con VLC (richiede codec H.264 installato)
vlc --demux h264 video_dump.h264

# In alternativa con ffprobe
ffprobe -v quiet -show_streams -select_streams v video_dump.h264
```

Se VLC mostra la schermata di Android Auto (anche con artefatti), il canale video è funzionante e si può passare allo Step 9.

---

## 3. Step 9 — Pipeline GStreamer C++

### 3.1 Nuovo file: `src/gst/gst_pipeline.hpp`

Questo file incapsula tutta la logica GStreamer. Il design è **zero-copy**: `pushBuffer` usa `gst_buffer_new_wrapped_full` per wrappare il puntatore `aasdk` senza copiare i byte.

```cpp
// src/gst/gst_pipeline.hpp
#pragma once

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <iostream>
#include <stdexcept>
#include <atomic>

namespace nemo
{

    class GstPipeline
    {
    public:
        using Pointer = std::shared_ptr<GstPipeline>;

        GstPipeline() = default;
        ~GstPipeline() { stop(); }

        // ── init ─────────────────────────────────────────────────────────────
        // window_handle: WId passato da PyQt6 (0 = autovideosink standalone)
        // width, height: risoluzione negoziata con Android (default 800x480)
        void init(guintptr window_handle = 0,
                  int width = 800, int height = 480)
        {
            gst_init(nullptr, nullptr);

            // Pipeline: appsrc → queue → h264parse → avdec_h264 → videoconvert → sink
            // Su Raspberry Pi sostituire avdec_h264 con v4l2h264dec per HW decode
            std::string pipe_desc =
                "appsrc name=src format=time is-live=true "
                "  caps=video/x-h264,stream-format=byte-stream,alignment=au,"
                "framerate=30/1,width=" + std::to_string(width) +
                ",height=" + std::to_string(height) +
                " ! queue max-size-buffers=4 leaky=downstream "
                " ! h264parse "
                " ! avdec_h264 max-threads=2 "
                " ! videoconvert "
                " ! xvimagesink name=sink sync=false";

            GError *err = nullptr;
            pipeline_ = gst_parse_launch(pipe_desc.c_str(), &err);
            if (!pipeline_ || err)
            {
                std::string msg = err ? err->message : "unknown";
                if (err) g_error_free(err);
                throw std::runtime_error("[GstPipeline] parse_launch failed: " + msg);
            }

            appsrc_ = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline_), "src"));
            if (!appsrc_)
                throw std::runtime_error("[GstPipeline] appsrc element not found");

            // ── Embed nella finestra Python se WId fornito ────────────────
            if (window_handle != 0)
            {
                GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
                if (sink)
                {
                    // Attendere il bus sync-message=prepare-window-handle
                    // oppure impostare direttamente (funziona per xvimagesink)
                    gst_video_overlay_set_window_handle(
                        GST_VIDEO_OVERLAY(sink),
                        window_handle);
                    gst_object_unref(sink);
                }
            }

            // Avvia la pipeline in PLAYING
            GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
            if (ret == GST_STATE_CHANGE_FAILURE)
                throw std::runtime_error("[GstPipeline] set_state(PLAYING) failed");

            running_.store(true);
            std::cout << "[GstPipeline] Pipeline avviata (" << width
                      << "x" << height << ")" << std::endl;
        }

        // ── pushBuffer ───────────────────────────────────────────────────────
        // Chiamato da VideoEventHandler::onMediaWithTimestampIndication
        // NOTA: buffer.data appartiene ad aasdk — viene tenuto vivo dalla callback
        //       Solo DOPO il return di questa funzione aasdk può rilasciare il buffer.
        //       Per sicurezza usiamo gst_buffer_new_memdup (copia una volta sola).
        void pushBuffer(aasdk::messenger::Timestamp::ValueType ts,
                        const uint8_t *data,
                        std::size_t size)
        {
            if (!running_.load() || !appsrc_) return;

            // gst_buffer_new_memdup: una singola copia, poi GStreamer gestisce lifetime
            GstBuffer *buf = gst_buffer_new_memdup(data, static_cast<gsize>(size));

            // Timestamp GStreamer in nanosecondi (aasdk usa microsecondi)
            GST_BUFFER_PTS(buf) = static_cast<GstClockTime>(ts) * GST_USECOND;
            GST_BUFFER_DTS(buf) = GST_BUFFER_PTS(buf);

            GstFlowReturn flow = gst_app_src_push_buffer(appsrc_, buf);
            // gst_buffer_new_memdup trasferisce ownership a buf;
            // gst_app_src_push_buffer la trasferisce alla pipeline.
            // NON fare gst_buffer_unref(buf) dopo push.

            if (flow != GST_FLOW_OK)
            {
                std::cerr << "[GstPipeline] pushBuffer flow error: " << flow << std::endl;
            }
        }

        // ── stop ─────────────────────────────────────────────────────────────
        void stop()
        {
            if (!running_.exchange(false)) return;
            if (appsrc_)
            {
                gst_app_src_end_of_stream(appsrc_);
                gst_object_unref(appsrc_);
                appsrc_ = nullptr;
            }
            if (pipeline_)
            {
                gst_element_set_state(pipeline_, GST_STATE_NULL);
                gst_object_unref(pipeline_);
                pipeline_ = nullptr;
            }
            std::cout << "[GstPipeline] Fermata." << std::endl;
        }

        bool isRunning() const { return running_.load(); }

    private:
        GstElement   *pipeline_ = nullptr;
        GstAppSrc    *appsrc_   = nullptr;
        std::atomic<bool> running_{false};
    };

} // namespace nemo
```

### 3.2 Note su architetture target

| Piattaforma | Decoder consigliato | Sink consigliato |
|---|---|---|
| x86_64 (Atom Z3770) | `avdec_h264` (libav) | `xvimagesink` o `glimagesink` |
| Raspberry Pi 4 (ARM) | `v4l2h264dec` | `kmssink` o `xvimagesink` |
| Raspberry Pi 3 (ARM) | `omxh264dec` (legacy) | `xvimagesink` |

Per switchare decoder in runtime senza ricompilare, leggere la variabile d'ambiente:

```cpp
const char *dec = std::getenv("NEMO_VIDEO_DECODER");
std::string decoder_elem = (dec && *dec) ? std::string(dec) : "avdec_h264 max-threads=2";
```

### 3.3 Collegamento in `VideoEventHandler`

Aggiungere il membro `pipeline_` e sbloccare la riga commentata:

```cpp
// In VideoEventHandler — aggiungere come membro private:
    std::shared_ptr<nemo::GstPipeline> pipeline_;

// Nel costruttore aggiungere il parametro:
    explicit VideoEventHandler(
        boost::asio::io_service::strand &strand,
        aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel,
        std::shared_ptr<IOrchestrator> orchestrator,
        std::shared_ptr<nemo::GstPipeline> pipeline = nullptr)  // ← NUOVO
        : strand_(strand),
          channel_(std::move(channel)),
          orchestrator_(std::move(orchestrator)),
          pipeline_(std::move(pipeline)) {}

// In onMediaWithTimestampIndication sostituire il commento Phase 5:
    if (pipeline_ && pipeline_->isRunning())
        pipeline_->pushBuffer(ts, buffer.data, buffer.size);
```

---

## 4. Step 10 — Finestra PyQt6 e Window ID

### 4.1 Nuovo file: `python/ui/main_window.py`

```python
# python/ui/main_window.py
import sys
import os

current_dir = os.path.dirname(os.path.abspath(__file__))
sys.path.append(os.path.join(current_dir, "..", ".."))
sys.path.append(os.path.join(current_dir, ".."))

from PyQt6.QtWidgets import QApplication, QMainWindow, QWidget, QVBoxLayout
from PyQt6.QtCore    import Qt, QTimer
from PyQt6.QtGui     import QPalette, QColor
import time

try:
    import build.nemo_head_unit as core
except ImportError as e:
    print(f"[ERRORE] Impossibile importare nemo_head_unit: {e}")
    sys.exit(1)

# Importa l'Orchestrator dalla Fase 4 (riutilizzo completo)
from test_interactive_phase4 import InteractiveOrchestrator


class VideoWidget(QWidget):
    """Widget con sfondo nero che ospita il rendering GStreamer."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WidgetAttribute.WA_NativeWindow, True)
        self.setAttribute(Qt.WidgetAttribute.WA_PaintOnScreen, True)
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent, True)

        # Sfondo nero finché GStreamer non prende possesso della finestra
        palette = self.palette()
        palette.setColor(QPalette.ColorRole.Window, QColor("black"))
        self.setPalette(palette)
        self.setAutoFillBackground(True)

    def get_window_id(self) -> int:
        """Restituisce il WId X11 da passare a GstPipeline::init()."""
        return int(self.winId())


class MainWindow(QMainWindow):
    def __init__(self, width: int = 800, height: int = 480):
        super().__init__()
        self.setWindowTitle("NemoHeadUnit — Android Auto")
        self.setFixedSize(width, height)

        self._width  = width
        self._height = height

        # Widget video (occupa tutta la finestra)
        self._video_widget = VideoWidget(self)
        self._video_widget.setGeometry(0, 0, width, height)

        # Oggetti C++ core
        self._runner      = None
        self._usb_manager = None
        self._gst_video   = None   # GstVideoSink C++ wrapper (vedi §6)

    def start_headunit(self):
        """Inizializza e avvia l'intera catena C++."""
        # 1. Assicura che il widget abbia un WId nativo
        self._video_widget.show()
        QApplication.processEvents()
        wid = self._video_widget.get_window_id()
        print(f"[UI] Window ID nativo: {wid}")

        # 2. Event loop Boost.Asio
        self._runner = core.IoContextRunner()

        # 3. Crypto
        crypto = core.CryptoManager()
        crypto.initialize()

        # 4. GStreamer sink C++ (esposto via pybind11 — vedi §6)
        self._gst_video = core.GstVideoSink(self._width, self._height)
        self._gst_video.set_window_id(wid)
        # La pipeline viene avviata da C++ quando il video channel è aperto

        # 5. Orchestrator Python (riuso Phase 4) + iniezione del sink
        orchestrator = InteractiveOrchestrator(
            screen_width=self._width,
            screen_height=self._height,
        )
        # Override del placeholder Phase 5
        gst_ref = self._gst_video
        def on_video_channel_open_request(payload: bytes) -> bytes:
            print("[UI] Canale video aperto → avvio GStreamer pipeline")
            gst_ref.start_pipeline()
            return b""
        orchestrator.on_video_channel_open_request = on_video_channel_open_request

        # 6. USB + avvio
        self._usb_manager = core.UsbHubManager(self._runner)
        self._usb_manager.set_crypto_manager(crypto)
        self._usb_manager.set_orchestrator(orchestrator)
        self._usb_manager.set_video_sink(self._gst_video)   # ← nuovo binding §6
        self._usb_manager.start(self._on_connect)
        self._runner.start()  # avvia io_service in thread C++ separato

    def _on_connect(self, success: bool, message: str):
        if success:
            print(f"[UI] Connessione AOAP avviata: {message}")
        else:
            print(f"[UI] Errore connessione: {message}")

    def closeEvent(self, event):
        print("[UI] Chiusura applicazione...")
        if self._gst_video:
            self._gst_video.stop()
        if self._usb_manager:
            self._usb_manager.stop()
        if self._runner:
            self._runner.stop()
        event.accept()


def main():
    app = QApplication(sys.argv)
    window = MainWindow(width=800, height=480)
    window.show()

    # Avvia la catena C++ con un piccolo delay per assicurare
    # che il WId sia valido (la finestra deve essere mostrata)
    QTimer.singleShot(200, window.start_headunit)

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
```

### 4.2 Dipendenza Python

```bash
pip install PyQt6
```

---

## 5. Modifiche `CMakeLists.txt`

Aggiungere GStreamer come dipendenza e il nuovo sorgente `gst_pipeline.hpp` (header-only, nessun `.cpp` da aggiungere). Il wrapper `GstVideoSink` è però necessario per il binding pybind11:

```cmake
# ── Aggiungi dopo find_package(Threads REQUIRED) ──────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTREAMER        REQUIRED gstreamer-1.0)
pkg_check_modules(GSTREAMER_APP    REQUIRED gstreamer-app-1.0)
pkg_check_modules(GSTREAMER_VIDEO  REQUIRED gstreamer-video-1.0)

# ── Nel target pybind11_add_module aggiungere il nuovo sorgente ───────────────
pybind11_add_module(nemo_head_unit
    src/binding.cpp
    src/crypto/crypto_manager.cpp
    src/usb/libusb_context.cpp
    src/usb/usb_hub_manager.cpp
    src/gst/gst_video_sink.cpp   # ← NUOVO (wrapper GstPipeline per pybind11)
)

# ── In target_include_directories aggiungere ──────────────────────────────────
target_include_directories(nemo_head_unit PRIVATE
    ${LIBUSB_INCLUDE_DIRS}
    ${GSTREAMER_INCLUDE_DIRS}
    ${GSTREAMER_APP_INCLUDE_DIRS}
    ${GSTREAMER_VIDEO_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${aasdk_SOURCE_DIR}/include
)

# ── In target_link_libraries aggiungere ───────────────────────────────────────
target_link_libraries(nemo_head_unit PRIVATE
    aasdk
    Boost::system
    protobuf::libprotobuf
    OpenSSL::SSL
    OpenSSL::Crypto
    ${LIBUSB_LIBRARIES}
    ${GSTREAMER_LIBRARIES}
    ${GSTREAMER_APP_LIBRARIES}
    ${GSTREAMER_VIDEO_LIBRARIES}
    Threads::Threads
)
```

### 5.1 Installazione dipendenze di sistema

```bash
# Ubuntu/Debian (x86_64 e ARM)
sudo apt-get install -y \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-libav \
    gstreamer1.0-x

# Test pipeline manuale (deve mostrare un pattern video)
gst-launch-1.0 videotestsrc ! autovideosink
```

---

## 6. Binding pybind11 — nuove API

### 6.1 Nuovo file: `src/gst/gst_video_sink.cpp`

Questo file espone `GstPipeline` a Python come classe `GstVideoSink`.

```cpp
// src/gst/gst_video_sink.cpp
#include "gst_pipeline.hpp"
#include <pybind11/pybind11.h>

namespace nemo
{
    // Wrapper con lifecycle gestito da pybind11
    class GstVideoSink
    {
    public:
        using Pointer = std::shared_ptr<GstVideoSink>;

        GstVideoSink(int width = 800, int height = 480)
            : width_(width), height_(height) {}

        void setWindowId(guintptr wid) { window_id_ = wid; }

        // Chiamato da Python quando on_video_channel_open_request è triggered
        void startPipeline()
        {
            if (pipeline_) return;  // idempotente
            pipeline_ = std::make_shared<GstPipeline>();
            pipeline_->init(window_id_, width_, height_);
        }

        // Chiamato da VideoEventHandler (C++ to C++, no GIL)
        void pushBuffer(aasdk::messenger::Timestamp::ValueType ts,
                        const uint8_t *data, std::size_t size)
        {
            if (pipeline_) pipeline_->pushBuffer(ts, data, size);
        }

        void stop()
        {
            if (pipeline_) { pipeline_->stop(); pipeline_.reset(); }
        }

        bool isRunning() const
        {
            return pipeline_ && pipeline_->isRunning();
        }

    private:
        int      width_     = 800;
        int      height_    = 480;
        guintptr window_id_ = 0;
        std::shared_ptr<GstPipeline> pipeline_;
    };

} // namespace nemo

// Registrazione in binding.cpp (aggiungere nella PYBIND11_MODULE)
// -- NON ricreare il modulo, aggiungere dentro nemo_head_unit:
//
// py::class_<nemo::GstVideoSink, std::shared_ptr<nemo::GstVideoSink>>(m, "GstVideoSink")
//     .def(py::init<int, int>(), py::arg("width")=800, py::arg("height")=480)
//     .def("set_window_id",   &nemo::GstVideoSink::setWindowId)
//     .def("start_pipeline",  &nemo::GstVideoSink::startPipeline)
//     .def("stop",            &nemo::GstVideoSink::stop,
//          py::call_guard<py::gil_scoped_release>())
//     .def("is_running",      &nemo::GstVideoSink::isRunning);
```

### 6.2 Aggiunta in `binding.cpp`

All'interno di `PYBIND11_MODULE(nemo_head_unit, m)` aggiungere:

```cpp
// Phase 5: GStreamer Video Sink
#include "gst/gst_video_sink.cpp"   // include diretto per semplicità build

// ...(dentro PYBIND11_MODULE)...
py::class_<nemo::GstVideoSink, std::shared_ptr<nemo::GstVideoSink>>(m, "GstVideoSink")
    .def(py::init<int, int>(), py::arg("width")=800, py::arg("height")=480)
    .def("set_window_id",  &nemo::GstVideoSink::setWindowId,
         "Passa il WId X11/Wayland del widget PyQt6 al renderer GStreamer.")
    .def("start_pipeline", &nemo::GstVideoSink::startPipeline,
         "Avvia la pipeline GStreamer (da chiamare quando il canale video è aperto).")
    .def("stop",           &nemo::GstVideoSink::stop,
         py::call_guard<py::gil_scoped_release>(),
         "Ferma e distrugge la pipeline GStreamer.")
    .def("is_running",     &nemo::GstVideoSink::isRunning);
```

Aggiungere inoltre il metodo `set_video_sink` su `UsbHubManager` per passare il sink a `VideoEventHandler` tramite la session:

```cpp
// src/usb/usb_hub_manager.hpp — aggiungere
void setVideoSink(std::shared_ptr<nemo::GstVideoSink> sink);

// Il sink viene poi passato al costruttore di VideoEventHandler
// dentro SessionManager::createVideoEventHandler()
```

---

## 7. Script di test Phase 5

### 7.1 `python/test_phase5_dump.py` — Solo dump H.264

```python
#!/usr/bin/env python3
"""
Test Phase 5a: verifica ricezione NAL units e dump su file.
Prerequisiti: Fase 4 funzionante, telefono connesso via USB.
"""
import sys, os, time
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(os.path.dirname(os.path.abspath(__file__)))

import build.nemo_head_unit as core
from test_interactive_phase4 import InteractiveOrchestrator

DUMP_PATH = "video_dump.h264"

def main():
    print("=" * 60)
    print(" TEST PHASE 5a — Dump NAL Units H.264")
    print(f" Output: {DUMP_PATH}")
    print("=" * 60)

    if hasattr(core, "enable_aasdk_logging"):
        core.enable_aasdk_logging()

    runner = core.IoContextRunner()
    crypto = core.CryptoManager()
    crypto.initialize()

    orchestrator = InteractiveOrchestrator(screen_width=800, screen_height=480)

    # Abilita il dump H.264 tramite il binding (da aggiungere in §6)
    # core.enable_video_dump(DUMP_PATH)  ← da esporre in binding.cpp

    usb = core.UsbHubManager(runner)
    usb.set_crypto_manager(crypto)
    usb.set_orchestrator(orchestrator)
    usb.start(lambda ok, msg: print(f"[USB] {'OK' if ok else 'ERR'}: {msg}"))
    runner.start()

    print("[INFO] Collega il telefono via USB. CTRL+C per uscire.")
    print(f"[INFO] Il dump sarà salvato in: {DUMP_PATH}")
    try:
        while True:
            time.sleep(1)
            if os.path.exists(DUMP_PATH):
                size_kb = os.path.getsize(DUMP_PATH) / 1024
                print(f"[Dump] {size_kb:.1f} KB scritti...")
                if size_kb >= 5000:
                    print("[Dump] Completato! Verifica con: vlc --demux h264 video_dump.h264")
                    break
    except KeyboardInterrupt:
        print("Uscita...")
    finally:
        usb.stop()
        runner.stop()

if __name__ == "__main__":
    main()
```

### 7.2 `python/ui/main_window.py` — Rendering E2E (vedi §4)

Il file è già documentato nella sezione 4. Avviarlo con:

```bash
# Dalla root del repo, dopo aver compilato con cmake
export DISPLAY=:0   # se in headless/SSH
python3 python/ui/main_window.py
```

---

## 8. Criteri di accettazione

Ogni step ha un criterio di accettazione autonomo e verificabile:

| Step | Criterio | Verifica |
|---|---|---|
| **8a** | `onMediaWithTimestampIndication` stampa log con `size > 0` | Output terminale |
| **8b** | `video_dump.h264` aperto con VLC mostra schermata Android Auto | VLC / ffprobe |
| **9a** | `gst-launch-1.0 videotestsrc ! autovideosink` funziona | Shell |
| **9b** | Pipeline C++ avviata senza eccezioni, log `[GstPipeline] Pipeline avviata` | Output terminale |
| **9c** | `pushBuffer` non produce `GST_FLOW_ERROR` nei log | Output terminale |
| **10a** | `MainWindow` aperta, sfondo nero visibile | Visivo |
| **10b** | Log `[UI] Window ID nativo: <numero>` a console | Output terminale |
| **10c** | Dopo connessione USB, schermata Android Auto appare nel widget | **MVP completato** |

---

## 9. Troubleshooting frequente

### `xvimagesink` non disponibile

```bash
# Installare gstreamer1.0-x
sudo apt-get install gstreamer1.0-x

# In alternativa usare autovideosink nella pipeline (solo per test)
# Modificare pipe_desc in gst_pipeline.hpp: sostituire xvimagesink con autovideosink
```

### Il video non appare nel widget PyQt6 (schermo nero)

- Assicurarsi che `WA_NativeWindow` sia impostato sul widget **prima** di chiamare `get_window_id()`.
- Aggiungere `QApplication.processEvents()` tra `widget.show()` e `get_window_id()` per garantire che la finestra X11 sia stata creata.
- Verificare che `gst_video_overlay_set_window_handle` riceva il `WId` corretto: il log `[UI] Window ID nativo: 0` indica che la finestra non è ancora stata mostrata.

### Artefatti video o freeze su Raspberry Pi

- Aumentare il `queue max-size-buffers` da 4 a 8 in `gst_pipeline.hpp`.
- Usare `v4l2h264dec` al posto di `avdec_h264` per lo hardware decoding.
- Verificare la temperatura: throttling termico riduce il framerate. `vcgencmd measure_temp`.

### `AVChannelSetupResponse` con `STATUS_WAIT` non sblocca il video

- Nella Fase 4, `on_av_channel_setup_request` per `CH_VIDEO` usa `STATUS_WAIT`. Cambiare in `STATUS_READY` per la Fase 5 e verificare che il telefono inizi a mandare NAL units.
- Se il telefono non invia NAL units neanche con `STATUS_READY`, verificare che `SENSOR_DRIVING_STATUS_DATA` risponda con `DRIVE_STATUS_UNRESTRICTED` (gate obbligatorio).

### GIL deadlock al momento della chiusura

- Chiamare sempre `usb.stop()` e `runner.stop()` **prima** di lasciare lo scope Python (fare override di `closeEvent` in `MainWindow`).
- Non tenere riferimenti Python a oggetti C++ dopo che `IoContextRunner` è fermato.

---

*Documento generato il 10/03/2026 — NemoHeadUnit Fase 5*
