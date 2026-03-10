#include "usb_hub_manager.hpp"
#include <iostream>
#include <pybind11/pybind11.h>
#include <fstream>
#include <sys/stat.h>

namespace nemo {

UsbHubManager::UsbHubManager(IoContextRunner& runner) : runner_(runner) {}

UsbHubManager::~UsbHubManager() {
    stop();
}

bool UsbHubManager::start(ConnectCallback callback) {
    python_callback_ = callback;
    boost::asio::io_context& io_ctx = runner_.get_io_context();

    libusb_context_ = std::make_unique<LibusbContext>(io_ctx);
    if (!libusb_context_->initialize()) {
        if (python_callback_) {
            pybind11::gil_scoped_acquire acquire;
            python_callback_(false, "Errore init libusb");
        }
        return false;
    }

    usb_wrapper_ = std::make_unique<aasdk::usb::USBWrapper>(libusb_context_->get_context());
    query_factory_ = std::make_unique<aasdk::usb::AccessoryModeQueryFactory>(*usb_wrapper_, io_ctx);
    query_chain_factory_ = std::make_unique<aasdk::usb::AccessoryModeQueryChainFactory>(
        *usb_wrapper_, io_ctx, *query_factory_
    );
    
    usb_hub_ = std::make_shared<aasdk::usb::USBHub>(*usb_wrapper_, io_ctx, *query_chain_factory_);
    
    boost::asio::post(io_ctx, [this, self = shared_from_this()]() {
        this->startDiscovery();
    });

    return true;
}

void UsbHubManager::startDiscovery() {
    std::cout << "[UsbHubManager] Avvio scansione USB (attendere device Google/AOAP)..." << std::endl;
    auto promise = aasdk::usb::IUSBHub::Promise::defer(runner_.get_io_context());
    
    promise->then(
        [this, self = shared_from_this()](aasdk::usb::DeviceHandle handle) {
            this->onDeviceDiscovered(std::move(handle));
        },
        [this, self = shared_from_this()](const aasdk::error::Error& e) {
            this->onDiscoveryFailed(e);
        }
    );
    
    usb_hub_->start(std::move(promise));
}

void UsbHubManager::ensureCertificatesExist(const std::string& cert_str, const std::string& key_str) {
    mkdir("cert", 0777);
    std::ofstream cert_file("cert/headunit.crt");
    if(cert_file.is_open()) {
        cert_file << cert_str;
        cert_file.close();
    }
    
    std::ofstream key_file("cert/headunit.key");
    if(key_file.is_open()) {
        key_file << key_str;
        key_file.close();
    }
}

void UsbHubManager::onDeviceDiscovered(aasdk::usb::DeviceHandle handle) {
    std::cout << "[UsbHubManager] Device compatibile rilevato (MODO AOAP ATTIVO). Costruzione Transport..." << std::endl;
    
    aoap_device_ = aasdk::usb::AOAPDevice::create(*usb_wrapper_, runner_.get_io_context(), std::move(handle));
    
    if (!aoap_device_) {
        if (python_callback_) {
            pybind11::gil_scoped_acquire acquire;
            python_callback_(false, "Fallita la creazione di AOAPDevice");
        }
        return;
    }

    usb_transport_ = std::make_shared<aasdk::transport::USBTransport>(
        runner_.get_io_context(), aoap_device_
    );

    ssl_wrapper_ = std::make_shared<aasdk::transport::SSLWrapper>();
    cryptor_ = std::make_shared<aasdk::messenger::Cryptor>(ssl_wrapper_);
    
    if(crypto_manager_) {
        ensureCertificatesExist(crypto_manager_->getCertificate(), crypto_manager_->getPrivateKey());
    }
    
    cryptor_->init();

    if (orchestrator_) {
        orchestrator_->setCryptor(cryptor_);
    }

    message_in_stream_ = std::make_shared<aasdk::messenger::MessageInStream>(
        runner_.get_io_context(), usb_transport_, cryptor_
    );
    message_out_stream_ = std::make_shared<aasdk::messenger::MessageOutStream>(
        runner_.get_io_context(), usb_transport_, cryptor_
    );

    messenger_ = std::make_shared<aasdk::messenger::Messenger>(
        runner_.get_io_context(), message_in_stream_, message_out_stream_
    );

    // Phase 5: video_sink_ (se impostato) viene propagato a SessionManager
    // che lo passa a VideoEventHandler. Se nullptr, comportamento identico
    // alla Phase 4 (dump/log only, senza GStreamer).
    session_manager_ = std::make_shared<SessionManager>(
        runner_.get_io_context(),
        messenger_,
        cryptor_,
        orchestrator_,
        video_sink_          // Phase 5: nullptr-safe
    );
    session_manager_->start();

    std::cout << "[UsbHubManager] Transport, Messenger e SessionManager operativi." << std::endl;
    if (python_callback_) {
        pybind11::gil_scoped_acquire acquire;
        python_callback_(true, "Connessione AOAP stabilita e Sessione AA avviata");
    }
}

void UsbHubManager::onDiscoveryFailed(const aasdk::error::Error& e) {
    std::cerr << "[UsbHubManager] Discovery fallita o interrotta. Errore: " << e.what() << std::endl;
    if (python_callback_) {
        pybind11::gil_scoped_acquire acquire;
        python_callback_(false, std::string("Discovery fail: ") + e.what());
    }
}

void UsbHubManager::stop() {
    if (session_manager_) {
        session_manager_->stop();
        session_manager_.reset();
    }
    if (usb_hub_) {
        usb_hub_->cancel();
    }
    if (messenger_) {
        messenger_->stop();
        messenger_.reset();
    }
    message_in_stream_.reset();
    message_out_stream_.reset();
    if (cryptor_) {
        cryptor_->deinit();
        cryptor_.reset();
    }
    ssl_wrapper_.reset();
    if (usb_transport_) {
        usb_transport_->stop();
        usb_transport_.reset();
    }
    aoap_device_.reset();
    usb_hub_.reset();
}

}
