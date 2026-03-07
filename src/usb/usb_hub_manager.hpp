#pragma once

#include <memory>
#include <functional>
#include <boost/asio.hpp>
#include "usb/libusb_context.hpp"
#include "core/io_context_runner.hpp"
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
        
        // Callback invocata in Python quando il device è connesso o in caso di errore
        using ConnectCallback = std::function<void(bool success, std::string message)>;

        UsbHubManager(IoContextRunner& runner);
        ~UsbHubManager();

        bool start(ConnectCallback callback);
        void stop();

    private:
        void startDiscovery();
        void onDeviceDiscovered(aasdk::usb::DeviceHandle handle);
        void onDiscoveryFailed(const aasdk::error::Error& e);

        IoContextRunner& runner_;
        ConnectCallback python_callback_;

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
    };

}