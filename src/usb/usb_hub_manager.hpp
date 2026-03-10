#pragma once

#include <memory>
#include <functional>
#include <boost/asio.hpp>
#include "usb/libusb_context.hpp"
#include "core/io_context_runner.hpp"
#include "session/session_manager.hpp"
#include "session/iorchestrator.hpp"
#include "crypto/crypto_manager.hpp"
#include "gst/gst_video_sink.hpp"
#include <aasdk/USB/USBWrapper.hpp>
#include <aasdk/USB/USBHub.hpp>
#include <aasdk/USB/AccessoryModeQueryFactory.hpp>
#include <aasdk/USB/AccessoryModeQueryChainFactory.hpp>
#include <aasdk/USB/AOAPDevice.hpp>
#include <aasdk/Transport/USBTransport.hpp>
#include <aasdk/Transport/SSLWrapper.hpp>
#include <aasdk/Messenger/Cryptor.hpp>
#include <aasdk/Messenger/MessageInStream.hpp>
#include <aasdk/Messenger/MessageOutStream.hpp>
#include <aasdk/Messenger/Messenger.hpp>

namespace nemo {

    class UsbHubManager : public std::enable_shared_from_this<UsbHubManager> {
    public:
        using Pointer = std::shared_ptr<UsbHubManager>;
        
        using ConnectCallback = std::function<void(bool success, std::string message)>;

        UsbHubManager(IoContextRunner& runner);
        ~UsbHubManager();

        bool start(ConnectCallback callback);
        void stop();

        void setOrchestrator(std::shared_ptr<IOrchestrator> orchestrator) {
            orchestrator_ = std::move(orchestrator);
        }

        void setCryptoManager(std::shared_ptr<CryptoManager> crypto) {
            crypto_manager_ = std::move(crypto);
        }

        // Phase 5: imposta il GstVideoSink da propagare a SessionManager
        void setVideoSink(std::shared_ptr<GstVideoSink> sink) {
            video_sink_ = std::move(sink);
        }

    private:
        void startDiscovery();
        void onDeviceDiscovered(aasdk::usb::DeviceHandle handle);
        void onDiscoveryFailed(const aasdk::error::Error& e);
        void ensureCertificatesExist(const std::string& cert_str, const std::string& key_str);

        IoContextRunner& runner_;
        ConnectCallback python_callback_;
        std::shared_ptr<IOrchestrator> orchestrator_;
        std::shared_ptr<CryptoManager> crypto_manager_;
        std::shared_ptr<GstVideoSink>  video_sink_;  // Phase 5

        std::unique_ptr<LibusbContext> libusb_context_;
        std::unique_ptr<aasdk::usb::USBWrapper> usb_wrapper_;
        std::unique_ptr<aasdk::usb::AccessoryModeQueryFactory> query_factory_;
        std::unique_ptr<aasdk::usb::AccessoryModeQueryChainFactory> query_chain_factory_;
        aasdk::usb::IUSBHub::Pointer usb_hub_;

        aasdk::usb::IAOAPDevice::Pointer aoap_device_;
        aasdk::transport::ITransport::Pointer usb_transport_;
        aasdk::transport::ISSLWrapper::Pointer ssl_wrapper_;
        aasdk::messenger::ICryptor::Pointer cryptor_;
        aasdk::messenger::IMessageInStream::Pointer message_in_stream_;
        aasdk::messenger::IMessageOutStream::Pointer message_out_stream_;
        aasdk::messenger::IMessenger::Pointer messenger_;
        
        SessionManager::Pointer session_manager_;
    };

}
