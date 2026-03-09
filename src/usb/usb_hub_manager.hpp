#pragma once

#include <memory>
#include <thread>
#include <boost/asio.hpp>
#include <aasdk/USB/AccessoryModeQueryChainFactory.hpp>
#include <aasdk/USB/AccessoryModeQueryFactory.hpp>
#include <aasdk/USB/USBHub.hpp>
#include <aasdk/USB/USBWrapper.hpp>
#include <aasdk/USB/AOAPDevice.hpp>
#include <aasdk/Transport/USBTransport.hpp>
#include <aasdk/Messenger/Cryptor.hpp>
#include <aasdk/Messenger/MessageInStream.hpp>
#include <aasdk/Messenger/MessageOutStream.hpp>
#include <aasdk/Transport/SSLWrapper.hpp>

#include "libusb_context.hpp"
#include "../io_context_runner.hpp"
#include "../session/session_manager.hpp"
#include "../crypto/crypto_manager.hpp"
#include "aasdk_logger.hpp"

namespace nemo {

class UsbHubManager : public std::enable_shared_from_this<UsbHubManager> {
public:
    using ConnectCallback = std::function<void(bool success, const std::string& message)>;

    UsbHubManager(IoContextRunner& runner);
    ~UsbHubManager();

    bool start(ConnectCallback callback);
    void stop();
    void startDiscovery();

    void setOrchestrator(std::shared_ptr<IOrchestrator> orchestrator) {
        orchestrator_ = orchestrator;
    }

    void setCryptoManager(std::shared_ptr<CryptoManager> crypto_manager) {
        crypto_manager_ = crypto_manager;
    }

private:
    void onDeviceDiscovered(aasdk::usb::DeviceHandle handle);
    void onDiscoveryFailed(const aasdk::error::Error& e);
    void ensureCertificatesExist(const std::string& cert_str, const std::string& key_str);

    IoContextRunner& runner_;
    ConnectCallback python_callback_;

    std::unique_ptr<LibusbContext> libusb_context_;
    std::unique_ptr<aasdk::usb::USBWrapper> usb_wrapper_;
    std::unique_ptr<aasdk::usb::AccessoryModeQueryFactory> query_factory_;
    std::unique_ptr<aasdk::usb::AccessoryModeQueryChainFactory> query_chain_factory_;
    std::shared_ptr<aasdk::usb::IUSBHub> usb_hub_;

    aasdk::usb::AOAPDevice::Pointer aoap_device_;
    aasdk::transport::ITransport::Pointer usb_transport_;
    aasdk::transport::ISSLWrapper::Pointer ssl_wrapper_;
    aasdk::messenger::Cryptor::Pointer cryptor_;
    aasdk::messenger::IMessageInStream::Pointer message_in_stream_;
    aasdk::messenger::IMessageOutStream::Pointer message_out_stream_;
    aasdk::messenger::IMessenger::Pointer messenger_;
    std::shared_ptr<SessionManager> session_manager_;

    std::shared_ptr<IOrchestrator> orchestrator_;
    std::shared_ptr<CryptoManager> crypto_manager_;
    std::shared_ptr<AasdkLogger> logger_;
};

}
