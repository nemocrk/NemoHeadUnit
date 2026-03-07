#include "usb_hub_manager.hpp"
#include <iostream>

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
        if (python_callback_) python_callback_(false, "Errore init libusb");
        return false;
    }

    usb_wrapper_ = std::make_unique<aasdk::usb::USBWrapper>(libusb_context_->get_context());
    query_factory_ = std::make_unique<aasdk::usb::AccessoryModeQueryFactory>(*usb_wrapper_, io_ctx);
    query_chain_factory_ = std::make_unique<aasdk::usb::AccessoryModeQueryChainFactory>(
        *usb_wrapper_, io_ctx, *query_factory_
    );
    
    usb_hub_ = std::make_shared<aasdk::usb::USBHub>(*usb_wrapper_, io_ctx, *query_chain_factory_);
    
    // Avviamo la scansione asincronamente tramite strand o dispatch se necessario
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

void UsbHubManager::onDeviceDiscovered(aasdk::usb::DeviceHandle handle) {
    std::cout << "[UsbHubManager] Device compatibile rilevato. Costruzione Transport..." << std::endl;
    
    // Creazione astrazione device AOAP
    aoap_device_ = aasdk::usb::AOAPDevice::create(*usb_wrapper_, runner_.get_io_context(), std::move(handle));
    
    if (!aoap_device_) {
        if (python_callback_) python_callback_(false, "Fallita la creazione di AOAPDevice");
        return;
    }

    // Creazione del trasporto USB IN/OUT BULK
    usb_transport_ = std::make_shared<aasdk::transport::USBTransport>(
        runner_.get_io_context(), aoap_device_
    );

    // Creazione del Cryptor (vuoto/inizializzato)
    ssl_wrapper_ = std::make_shared<aasdk::transport::SSLWrapper>();
    cryptor_ = std::make_shared<aasdk::messenger::Cryptor>(ssl_wrapper_);
    cryptor_->init();

    // Creazione In/Out Streams
    message_in_stream_ = std::make_shared<aasdk::messenger::MessageInStream>(
        runner_.get_io_context(), usb_transport_, cryptor_
    );
    message_out_stream_ = std::make_shared<aasdk::messenger::MessageOutStream>(
        runner_.get_io_context(), usb_transport_, cryptor_
    );

    // Creazione del core Messenger che gestirà l'handshake e i canali Protobuf
    messenger_ = std::make_shared<aasdk::messenger::Messenger>(
        runner_.get_io_context(), message_in_stream_, message_out_stream_
    );

    std::cout << "[UsbHubManager] Transport e Messenger operativi." << std::endl;
    if (python_callback_) {
        python_callback_(true, "Connessione AOAP stabilita e Messenger pronto");
    }
}

void UsbHubManager::onDiscoveryFailed(const aasdk::error::Error& e) {
    std::cerr << "[UsbHubManager] Discovery fallita o interrotta. Errore: " << e.what() << std::endl;
    if (python_callback_) {
        python_callback_(false, std::string("Discovery fail: ") + e.what());
    }
}

void UsbHubManager::stop() {
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