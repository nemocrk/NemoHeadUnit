#include <pybind11/pybind11.h>
#include <iostream>
#include "core/io_context_runner.hpp"
#include "crypto/crypto_manager.hpp"

namespace py = pybind11;

// Funzione base per verificare l'integrazione C++ -> Python
void hello_world() {
    std::cout << "[NemoHeadUnit Core C++] Binding inizializzato con successo!" << std::endl;
}

PYBIND11_MODULE(nemo_head_unit, m) {
    m.doc() = "NemoHeadUnit C++ Core extension module";
    
    m.def("hello_world", &hello_world, "Stampa un messaggio di test per verificare il modulo pybind11");

    // Phase 2: Event Loop Runner
    py::class_<nemo::IoContextRunner>(m, "IoContextRunner")
        .def(py::init<>())
        // Rilasciamo il GIL durante lo start/stop così non si blocca l'UI Python
        .def("start", &nemo::IoContextRunner::start, py::call_guard<py::gil_scoped_release>())
        .def("stop", &nemo::IoContextRunner::stop, py::call_guard<py::gil_scoped_release>());

    // Phase 2: Crypto Manager
    py::class_<nemo::CryptoManager>(m, "CryptoManager")
        .def(py::init<>())
        .def("initialize", &nemo::CryptoManager::initialize, "Genera le chiavi e il certificato self-signed")
        .def("get_certificate", &nemo::CryptoManager::getCertificate, "Restituisce il certificato in formato PEM")
        .def("get_private_key", &nemo::CryptoManager::getPrivateKey, "Restituisce la chiave privata in formato PEM");
}