#include <iostream>
#include <chrono>
#include <thread>
#include "core/io_context_runner.hpp"
#include "usb/usb_hub_manager.hpp"

int main() {
    std::cout << "=== NemoHeadUnit Headless Integrator (Fase 4) ===" << std::endl;
    std::cout << "In attesa di connessione USB Android..." << std::endl;

    nemo::IoContextRunner runner;
    runner.start();

    auto hub = std::make_shared<nemo::UsbHubManager>(runner);

    bool device_found = false;
    hub->start([&](bool success, std::string msg) {
        if (success) {
            std::cout << "\n>>> ANDROID AUTO SESSION AVVIATA: " << msg << " <<<\n" << std::endl;
            device_found = true;
        } else {
            std::cout << "\n>>> ERRORE USB: " << msg << " <<<\n" << std::endl;
        }
    });

    // Tieni in vita il processo per 60 secondi in attesa del telefono
    int timeout = 60;
    while (timeout > 0) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        timeout--;
    }

    std::cout << "Spegnimento servizi..." << std::endl;
    hub->stop();
    runner.stop();

    return 0;
}
