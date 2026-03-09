#include <pybind11/pybind11.h>
#include <pybind11/functional.h>
#include <iostream>
#include "core/io_context_runner.hpp"
#include "crypto/crypto_manager.hpp"
#include "usb/usb_hub_manager.hpp"
#include "python/py_orchestrator.hpp"

namespace py = pybind11;

void hello_world() {
    std::cout << "[NemoHeadUnit Core C++] Binding inizializzato con successo!" << std::endl;
}

PYBIND11_MODULE(nemo_head_unit, m) {
    m.doc() = "NemoHeadUnit C++ Core extension module";
    
    m.def("hello_world", &hello_world, "Stampa un messaggio di test");

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

    // Phase 3: Usb Hub Manager
    py::class_<nemo::UsbHubManager, std::shared_ptr<nemo::UsbHubManager>>(m, "UsbHubManager")
        .def(py::init<nemo::IoContextRunner&>())
        .def("start", &nemo::UsbHubManager::start, py::call_guard<py::gil_scoped_release>())
        .def("stop", &nemo::UsbHubManager::stop, py::call_guard<py::gil_scoped_release>())
        .def("set_orchestrator", 
            [](std::shared_ptr<nemo::UsbHubManager> self, py::object orch) {
                self->setOrchestrator(std::make_shared<nemo::PyOrchestrator>(std::move(orch)));
            }, "Registra la classe Python che gestisce il protocollo AA.");
}
