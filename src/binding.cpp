#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <iostream>
#include "core/io_context_runner.hpp"
#include "crypto/crypto_manager.hpp"
#include "usb/usb_hub_manager.hpp"
#include "python/py_orchestrator.hpp"
#include "gst/gst_video_sink.hpp"
#include <aasdk/Messenger/ICryptor.hpp>
#include <aasdk/Common/ModernLogger.hpp>

namespace py = pybind11;

void hello_world()
{
    std::cout << "[NemoHeadUnit Core C++] Binding inizializzato con successo!" << std::endl;
}

void enable_aasdk_logging()
{
    std::cout << "[NemoHeadUnit] Abilitazione aasdk ModernLogger (TRACE/DEBUG)..." << std::endl;
    auto &logger = aasdk::common::ModernLogger::getInstance();
    logger.setLevel(aasdk::common::LogLevel::TRACE);
    logger.setCategoryLevel(aasdk::common::LogCategory::USB, aasdk::common::LogLevel::DEBUG);
    logger.setCategoryLevel(aasdk::common::LogCategory::TCP, aasdk::common::LogLevel::DEBUG);
}

PYBIND11_MODULE(nemo_head_unit, m)
{
    m.doc() = "NemoHeadUnit C++ Core extension module";

    m.def("hello_world", &hello_world, "Stampa un messaggio di test");
    m.def("enable_aasdk_logging", &enable_aasdk_logging, "Abilita i log nativi di aasdk per il debug");

    // Phase 2: Event Loop Runner
    py::class_<nemo::IoContextRunner, std::shared_ptr<nemo::IoContextRunner>>(m, "IoContextRunner")
        .def(py::init<>())
        .def("start", &nemo::IoContextRunner::start, py::call_guard<py::gil_scoped_release>())
        .def("stop",  &nemo::IoContextRunner::stop,  py::call_guard<py::gil_scoped_release>());

    // Phase 2: Crypto Manager
    py::class_<nemo::CryptoManager, std::shared_ptr<nemo::CryptoManager>>(m, "CryptoManager")
        .def(py::init<>())
        .def("initialize",       &nemo::CryptoManager::initialize)
        .def("get_certificate",  &nemo::CryptoManager::getCertificate)
        .def("get_private_key",  &nemo::CryptoManager::getPrivateKey);

    // Phase 4: ICryptor
    py::class_<aasdk::messenger::ICryptor, std::shared_ptr<aasdk::messenger::ICryptor>>(m, "ICryptor")
        .def("do_handshake", &aasdk::messenger::ICryptor::doHandshake)
        .def("write_handshake_buffer", [](aasdk::messenger::ICryptor &self, py::bytes data)
             {
            std::string str = data;
            aasdk::common::DataConstBuffer buf(
                reinterpret_cast<const uint8_t*>(str.data()), str.size());
            self.writeHandshakeBuffer(buf); })
        .def("read_handshake_buffer", [](aasdk::messenger::ICryptor &self)
             {
            auto buf = self.readHandshakeBuffer();
            return py::bytes(
                reinterpret_cast<const char*>(buf.data()), buf.size()); });

    // -------------------------------------------------------------------------
    // Phase 5: GstVideoSink
    // -------------------------------------------------------------------------
    py::class_<nemo::GstVideoSink, std::shared_ptr<nemo::GstVideoSink>>(m, "GstVideoSink")
        .def(py::init<int, int>(),
             py::arg("width")  = 800,
             py::arg("height") = 480)
        .def("set_window_id",
             [](nemo::GstVideoSink &self, uintptr_t wid) {
                 self.setWindowId(static_cast<guintptr>(wid));
             },
             py::arg("wid"))
        .def("start_pipeline",
             &nemo::GstVideoSink::startPipeline,
             py::call_guard<py::gil_scoped_release>())
        .def("stop",
             &nemo::GstVideoSink::stop,
             py::call_guard<py::gil_scoped_release>())
        .def("is_running",
             &nemo::GstVideoSink::isRunning);

    // -------------------------------------------------------------------------
    // Phase 3+5: UsbHubManager
    // enable_video_dump aggiunto in Phase 5b (dump H.264)
    // -------------------------------------------------------------------------
    py::class_<nemo::UsbHubManager, std::shared_ptr<nemo::UsbHubManager>>(m, "UsbHubManager")
        .def(py::init<nemo::IoContextRunner &>())
        .def("start", &nemo::UsbHubManager::start,
             py::call_guard<py::gil_scoped_release>())
        .def("stop",  &nemo::UsbHubManager::stop,
             py::call_guard<py::gil_scoped_release>())
        .def("set_orchestrator",
             [](std::shared_ptr<nemo::UsbHubManager> self, py::object orch) {
                 self->setOrchestrator(
                     std::make_shared<nemo::PyOrchestrator>(std::move(orch)));
             })
        .def("set_crypto_manager", &nemo::UsbHubManager::setCryptoManager)
        .def("set_video_sink",
             [](std::shared_ptr<nemo::UsbHubManager> self,
                std::shared_ptr<nemo::GstVideoSink>  sink) {
                 self->setVideoSink(std::move(sink));
             },
             py::arg("sink"))
        // ── enable_video_dump ─────────────────────────────────────────────
        // Abilita la scrittura del dump H.264 grezzo su file.
        // Chiamare dopo usb.start() ma prima che arrivino NAL units.
        // Thread-safe: usa lo strand interno di SessionManager.
        // Il dump si chiude automaticamente dopo 5 MB (DUMP_LIMIT_ in
        // VideoEventHandler). Verificare output con:
        //   vlc --demux h264 <path>
        //   ffprobe -v quiet -show_streams -select_streams v <path>
        // -----------------------------------------------------------------
        .def("enable_video_dump",
             [](std::shared_ptr<nemo::UsbHubManager> self,
                const std::string& path) {
                 self->enableVideoDump(path);
             },
             py::arg("path"),
             py::call_guard<py::gil_scoped_release>(),
             "Abilita il dump H.264 su file. Chiamare dopo start().");
}
