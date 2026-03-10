#pragma once

#include <memory>
#include <functional>
#include <string>
#include <boost/asio.hpp>
#include "usb/libusb_context.hpp"
#include "core/io_context_runner.hpp"
#include "session/session_manager.hpp"
#include "session/iorchestrator.hpp"
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
        using Pointer       = std::shared_ptr<UsbHubManager>;
        using ConnectCallback = std::function<void(bool success, std::string message)>;

        explicit UsbHubManager(IoContextRunner& runner);
        ~UsbHubManager();

        bool start(ConnectCallback callback);
        void stop();

        void setOrchestrator(std::shared_ptr<IOrchestrator> orchestrator) {
            orchestrator_ = std::move(orchestrator);
        }

        // ── Refactor: Python CryptoManager ────────────────────────────────
        // Sostituisce setCryptoManager(shared_ptr<CryptoManager>).
        // Python carica e valida i PEM, poi li passa come stringhe.
        // ensureCertificatesExist() li scrive su disco per aasdk::Cryptor.
        // -----------------------------------------------------------------
        void setCertificateAndKey(const std::string& cert, const std::string& key) {
            certificate_ = cert;
            private_key_ = key;
        }

        // Phase 5: imposta il GstVideoSink da propagare a SessionManager
        void setVideoSink(std::shared_ptr<GstVideoSink> sink) {
            video_sink_ = std::move(sink);
        }

        // ── enableVideoDump ────────────────────────────────────────────────
        // Abilita la scrittura del dump H.264 grezzo su file.
        // Chiamare dopo start() ma prima che arrivino NAL units.
        // Thread-safe: delega a SessionManager::enableVideoDump() che
        // usa lo strand interno di Boost.Asio.
        // ------------------------------------------------------------------
        void enableVideoDump(const std::string& path) {
            if (session_manager_) {
                session_manager_->enableVideoDump(path);
            } else {
                pending_dump_path_ = path;
            }
        }

    private:
        void startDiscovery();
        void onDeviceDiscovered(aasdk::usb::DeviceHandle handle);
        void onDiscoveryFailed(const aasdk::error::Error& e);
        void ensureCertificatesExist();

        IoContextRunner&              runner_;
        ConnectCallback               python_callback_;
        std::shared_ptr<IOrchestrator> orchestrator_;
        std::shared_ptr<GstVideoSink>  video_sink_;

        // Cert + key come stringhe PEM (impostate da Python via setCertificateAndKey)
        std::string certificate_;
        std::string private_key_;
        std::string pending_dump_path_;

        std::unique_ptr<LibusbContext>                               libusb_context_;
        std::unique_ptr<aasdk::usb::USBWrapper>                     usb_wrapper_;
        std::unique_ptr<aasdk::usb::AccessoryModeQueryFactory>      query_factory_;
        std::unique_ptr<aasdk::usb::AccessoryModeQueryChainFactory> query_chain_factory_;
        aasdk::usb::IUSBHub::Pointer                                usb_hub_;

        aasdk::usb::IAOAPDevice::Pointer              aoap_device_;
        aasdk::transport::ITransport::Pointer         usb_transport_;
        aasdk::transport::ISSLWrapper::Pointer        ssl_wrapper_;
        aasdk::messenger::ICryptor::Pointer           cryptor_;
        aasdk::messenger::IMessageInStream::Pointer   message_in_stream_;
        aasdk::messenger::IMessageOutStream::Pointer  message_out_stream_;
        aasdk::messenger::IMessenger::Pointer         messenger_;

        SessionManager::Pointer session_manager_;
    };

}
