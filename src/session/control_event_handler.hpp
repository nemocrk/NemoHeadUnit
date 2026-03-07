#pragma once

#include <iostream>
#include <string>
#include <aasdk/Channel/Control/IControlServiceChannelEventHandler.hpp>
#include <aasdk/Channel/Control/IControlServiceChannel.hpp>
#include <aasdk/Channel/Promise.hpp>
#include <aasdk/Messenger/Cryptor.hpp>

namespace nemo {

class ControlEventHandler : public aasdk::channel::control::IControlServiceChannelEventHandler {
public:
    using Pointer = std::shared_ptr<ControlEventHandler>;

    ControlEventHandler(boost::asio::io_service::strand& strand, 
                        aasdk::channel::control::IControlServiceChannel::Pointer channel,
                        aasdk::messenger::ICryptor::Pointer cryptor)
        : strand_(strand), channel_(std::move(channel)), cryptor_(std::move(cryptor)) {}

    aasdk::channel::SendPromise::Pointer makePromise(const char* tag) {
        auto p = aasdk::channel::SendPromise::defer(strand_);
        p->then(
            []() {},
            [tag](const aasdk::error::Error& e) {
                std::cerr << "[" << tag << "] send failed: " << e.what() << std::endl;
            }
        );
        return p;
    }

    void onVersionResponse(uint16_t majorCode, uint16_t minorCode, aap_protobuf::shared::MessageStatus status) override {
        std::cout << "[Control] VersionResponse: " << majorCode << "." << minorCode << " Status: " << status << std::endl;
        if (status == 0) {
            std::cout << "[Control] Inizio SSL Handshake..." << std::endl;
            try {
                cryptor_->doHandshake();
                channel_->sendHandshake(cryptor_->readHandshakeBuffer(), makePromise("Control/SendHandshake1"));
            } catch (const std::exception& e) {
                std::cerr << "[Control] Errore inzializzazione handshake: " << e.what() << std::endl;
            }
        }
    }

    void onHandshake(const aasdk::common::DataConstBuffer &payload) override {
        std::cout << "[Control] Handshake chunk ricevuto, size: " << payload.size << std::endl;
        try {
            cryptor_->writeHandshakeBuffer(payload);
            if (!cryptor_->doHandshake()) {
                std::cout << "[Control] Handshake incompleto, invio prossimo blocco..." << std::endl;
                channel_->sendHandshake(cryptor_->readHandshakeBuffer(), makePromise("Control/SendHandshake2"));
            } else {
                std::cout << "[Control] SSL Handshake COMPLETATO. Invio AuthComplete..." << std::endl;
                aap_protobuf::service::control::message::AuthResponse response;
                response.set_status(static_cast<decltype(response.status())>(0));
                channel_->sendAuthComplete(response, makePromise("Control/AuthComplete"));
            }
        } catch (const std::exception& e) {
            std::cerr << "[Control] Errore durante l'handshake: " << e.what() << std::endl;
        }
    }

    void onServiceDiscoveryRequest(const aap_protobuf::service::control::message::ServiceDiscoveryRequest &request) override {
        std::cout << "[Control] ServiceDiscoveryRequest received." << std::endl;
        aap_protobuf::service::control::message::ServiceDiscoveryResponse response;
        
        response.set_head_unit_make(std::string("NemoHeadUnit"));
        response.set_model(std::string("MVP"));
        response.set_year(std::string("2026"));
        
        channel_->sendServiceDiscoveryResponse(response, makePromise("Control/ServiceDiscoveryResponse"));
    }

    void onAudioFocusRequest(const aap_protobuf::service::control::message::AudioFocusRequest &request) override {
        std::cout << "[Control] AudioFocusRequest received." << std::endl;
        aap_protobuf::service::control::message::AudioFocusNotification response;
        response.set_focus_state(static_cast<decltype(response.focus_state())>(1));
        channel_->sendAudioFocusResponse(response, makePromise("Control/AudioFocusResponse"));
    }

    void onByeByeRequest(const aap_protobuf::service::control::message::ByeByeRequest &request) override {
        std::cout << "[Control] ByeByeRequest received. Session ending." << std::endl;
    }

    void onByeByeResponse(const aap_protobuf::service::control::message::ByeByeResponse &response) override {
        std::cout << "[Control] ByeByeResponse received." << std::endl;
    }

    void onBatteryStatusNotification(const aap_protobuf::service::control::message::BatteryStatusNotification &notification) override {
        // Ignore
    }

    void onNavigationFocusRequest(const aap_protobuf::service::control::message::NavFocusRequestNotification &request) override {
        std::cout << "[Control] NavFocusRequest received." << std::endl;
    }

    void onVoiceSessionRequest(const aap_protobuf::service::control::message::VoiceSessionNotification &request) override {
        std::cout << "[Control] VoiceSessionRequest received." << std::endl;
    }

    void onPingRequest(const aap_protobuf::service::control::message::PingRequest &request) override {
        aap_protobuf::service::control::message::PingResponse response;
        response.set_timestamp(request.timestamp());
        channel_->sendPingResponse(response, makePromise("Control/PingResponse"));
    }

    void onPingResponse(const aap_protobuf::service::control::message::PingResponse &response) override {
        // Ignore
    }

    void onChannelError(const aasdk::error::Error &e) override {
        std::cerr << "[Control] Channel Error: " << e.what() << std::endl;
    }

private:
    boost::asio::io_service::strand& strand_;
    aasdk::channel::control::IControlServiceChannel::Pointer channel_;
    aasdk::messenger::ICryptor::Pointer cryptor_;
};

} // namespace nemo
