#include <pybind11/pybind11.h>
#include <iostream>

namespace py = pybind11;

// Funzione base per verificare l'integrazione C++ -> Python
void hello_world() {
    std::cout << "[NemoHeadUnit Core C++] Binding inizializzato con successo!" << std::endl;
}

PYBIND11_MODULE(nemo_head_unit, m) {
    m.doc() = "NemoHeadUnit C++ Core extension module";
    
    m.def("hello_world", &hello_world, "Stampa un messaggio di test per verificare il modulo pybind11");
}