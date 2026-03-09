#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <iostream>
#include "core/io_context_runner.hpp"
#include "crypto/crypto_manager.hpp"
#include "usb/usb_hub_manager.hpp"
#include "python/py_orchestrator.hpp"
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
    logger.setCategoryLevel(aasdk::common::LogCategory::TRANSPORT, aasdk::common::LogLevel::DEBUG);
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
        .def("stop", &nemo::IoContextRunner::stop, py::call_guard<py::gil_scoped_release>());

    // Phase 2: Crypto Manager
    py::class_<nemo::CryptoManager, std::shared_ptr<nemo::CryptoManager>>(m, "CryptoManager")
        .def(py::init<>())
        .def("initialize", &nemo::CryptoManager::initialize)
        .def("get_certificate", &nemo::CryptoManager::getCertificate)
        .def("get_private_key", &nemo::CryptoManager::getPrivateKey);

    // Phase 4: Cryptor per delegare SSL a Python
    py::class_<aasdk::messenger::ICryptor, std::shared_ptr<aasdk::messenger::ICryptor>>(m, "ICryptor")
        .def("do_handshake", &aasdk::messenger::ICryptor::doHandshake)
        .def("write_handshake_buffer", [](aasdk::messenger::ICryptor &self, py::bytes data)
             {
            std::string str = data;
            aasdk::common::DataConstBuffer buf(reinterpret_cast<const uint8_t*>(str.data()), str.size());
            self.writeHandshakeBuffer(buf); })
        .def("read_handshake_buffer", [](aasdk::messenger::ICryptor &self)
             {
            auto buf = self.readHandshakeBuffer();
            // std::vector<uint8_t> in C++ usa .data() per ottenere il puntatore raw
            return py::bytes(reinterpret_cast<const char*>(buf.data()), buf.size()); });

    // Phase 3: Usb Hub Manager
    py::class_<nemo::UsbHubManager, std::shared_ptr<nemo::UsbHubManager>>(m, "UsbHubManager")
        .def(py::init<nemo::IoContextRunner &>())
        .def("start", &nemo::UsbHubManager::start, py::call_guard<py::gil_scoped_release>())
        .def("stop", &nemo::UsbHubManager::stop, py::call_guard<py::gil_scoped_release>())
        .def("set_orchestrator", [](std::shared_ptr<nemo::UsbHubManager> self, py::object orch)
             { self->setOrchestrator(std::make_shared<nemo::PyOrchestrator>(std::move(orch))); }, "Registra la classe Python che gestisce il protocollo AA.")
        .def("set_crypto_manager", &nemo::UsbHubManager::setCryptoManager);
}
