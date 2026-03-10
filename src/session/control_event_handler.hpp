#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <aasdk/Channel/Control/IControlServiceChannelEventHandler.hpp>
#include <aasdk/Channel/Control/IControlServiceChannel.hpp>
#include <aasdk/Channel/Promise.hpp>
#include <aasdk/Messenger/Cryptor.hpp>
#include "iorchestrator.hpp"

namespace nemo {

class ControlEventHandler : public aasdk::channel::control::IControlServiceChannelEventHandler,
                            public std::enable_shared_from_this<ControlEventHandler> {
public:
    using Pointer = std::shared_ptr<ControlEventHandler>;

    ControlEventHandler(boost::asio::io_service::strand& strand,
                        aasdk::channel::control::IControlServiceChannel::Pointer channel,
                        aasdk::messenger::ICryptor::Pointer cryptor,
                        std::shared_ptr<IOrchestrator> orchestrator)
        : strand_(strand),
          channel_(std::move(channel)),
          cryptor_(std::move(cryptor)),
          orchestrator_(std::move(orchestrator)) {}

    aasdk::channel::SendPromise::Pointer makePromise(const char* tag) {
        auto p = aasdk::channel::SendPromise::defer(strand_);
        p->then(
            [tag]() {
                std::cout << "[" << tag << "] send OK." << std::endl;
            },
            [tag](const aasdk::error::Error& e) {
                std::cerr << "[" << tag << "] send FAILED: " << e.what() << std::endl;
            }
        );
        return p;
    }

    // -----------------------------------------------------------------------
    // Handshake TLS
    // -----------------------------------------------------------------------

    void onVersionResponse(uint16_t majorCode, uint16_t minorCode,
                           aap_protobuf::shared::MessageStatus status) override {
        std::cout << "[Control] VersionResponse: " << majorCode << "." << minorCode
                  << " Status: " << status << std::endl;
        if (!orchestrator_) throw std::runtime_error("Orchestrator non impostato");

        std::string first_chunk = orchestrator_->onVersionStatus(majorCode, minorCode, status);
        if (first_chunk.empty()) throw std::runtime_error("Python MUST return first handshake chunk");

        aasdk::common::Data data(first_chunk.begin(), first_chunk.end());
        channel_->sendHandshake(data, makePromise("Control/SendHandshake1"));
        channel_->receive(this->shared_from_this());
    }

    void onHandshake(const aasdk::common::DataConstBuffer& payload) override {
        std::cout << "[Control] Handshake chunk ricevuto, size: " << payload.size << std::endl;
        if (!orchestrator_) throw std::runtime_error("Orchestrator non impostato");

        std::string in(reinterpret_cast<const char*>(payload.cdata), payload.size);
        std::string out = orchestrator_->onHandshake(in);

        if (!out.empty()) {
            aasdk::common::Data data(out.begin(), out.end());
            channel_->sendHandshake(data, makePromise("Control/SendHandshakeX"));
        } else {
            std::string auth_bytes = orchestrator_->getAuthCompleteResponse();
            if (auth_bytes.empty()) throw std::runtime_error("Python MUST return AuthComplete bytes");
            aap_protobuf::service::control::message::AuthResponse response;
            if (!response.ParseFromString(auth_bytes))
                throw std::runtime_error("AuthResponse parse failed");
            channel_->sendAuthComplete(response, makePromise("Control/AuthComplete"));
        }
        channel_->receive(this->shared_from_this());
    }

    // -----------------------------------------------------------------------
    // Service Discovery
    // -----------------------------------------------------------------------

    void onServiceDiscoveryRequest(
        const aap_protobuf::service::control::message::ServiceDiscoveryRequest& request) override {
        std::cout << "[Control] ServiceDiscoveryRequest received." << std::endl;
        if (!orchestrator_) throw std::runtime_error("Orchestrator non impostato");

        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_->onServiceDiscoveryRequest(req_str);

        aap_protobuf::service::control::message::ServiceDiscoveryResponse response;
        if (!response.ParseFromString(res_str))
            throw std::runtime_error("ServiceDiscoveryResponse parse failed");

        channel_->sendServiceDiscoveryResponse(response, makePromise("Control/ServiceDiscoveryResponse"));
        channel_->receive(this->shared_from_this());
    }

    // -----------------------------------------------------------------------
    // Audio Focus
    // Ref: AndroidAutoEntity-17.cpp::onAudioFocusRequest()
    // -----------------------------------------------------------------------

    void onAudioFocusRequest(
        const aap_protobuf::service::control::message::AudioFocusRequest& request) override {
        std::cout << "[Control] AudioFocusRequest received." << std::endl;
        if (!orchestrator_) throw std::runtime_error("Orchestrator non impostato");

        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_->onAudioFocusRequest(req_str);

        aap_protobuf::service::control::message::AudioFocusNotification response;
        if (!response.ParseFromString(res_str))
            throw std::runtime_error("AudioFocusNotification parse failed");

        channel_->sendAudioFocusResponse(response, makePromise("Control/AudioFocusResponse"));
        channel_->receive(this->shared_from_this());
    }

    // -----------------------------------------------------------------------
    // Navigation Focus
    // Ref: AndroidAutoEntity-17.cpp::onNavigationFocusRequest()
    // Prima: silent drop. Ora: chiama orchestrator e invia NavFocusNotification.
    // -----------------------------------------------------------------------

    void onNavigationFocusRequest(
        const aap_protobuf::service::control::message::NavFocusRequestNotification& request) override {
        std::cout << "[Control] NavFocusRequest received." << std::endl;
        if (!orchestrator_) {
            channel_->receive(this->shared_from_this());
            return;
        }
        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_->onNavigationFocusRequest(req_str);

        if (!res_str.empty()) {
            aap_protobuf::service::control::message::NavFocusNotification response;
            if (response.ParseFromString(res_str)) {
                channel_->sendNavigationFocusResponse(response,
                    makePromise("Control/NavFocusResponse"));
            }
        }
        channel_->receive(this->shared_from_this());
    }

    // -----------------------------------------------------------------------
    // Voice Session
    // Ref: AndroidAutoEntity-17.cpp::onVoiceSessionRequest() -> solo receive()
    // Prima: silent drop. Ora: delega a orchestrator (sink silente).
    // -----------------------------------------------------------------------

    void onVoiceSessionRequest(
        const aap_protobuf::service::control::message::VoiceSessionNotification& request) override {
        std::cout << "[Control] VoiceSessionRequest received (sink silente)." << std::endl;
        if (orchestrator_) {
            std::string req_str = request.SerializeAsString();
            orchestrator_->onVoiceSessionRequest(req_str);  // ritorna "", nessuna risposta
        }
        channel_->receive(this->shared_from_this());
    }

    // -----------------------------------------------------------------------
    // Battery Status
    // Ref: AndroidAutoEntity-17.cpp::onBatteryStatusNotification() -> solo receive()
    // Prima: silent drop. Ora: delega a orchestrator (sink silente).
    // -----------------------------------------------------------------------

    void onBatteryStatusNotification(
        const aap_protobuf::service::control::message::BatteryStatusNotification& notification) override {
        std::cout << "[Control] BatteryStatusNotification received (sink silente)." << std::endl;
        if (orchestrator_) {
            std::string req_str = notification.SerializeAsString();
            orchestrator_->onBatteryStatusNotification(req_str);  // ritorna "", nessuna risposta
        }
        channel_->receive(this->shared_from_this());
    }

    // -----------------------------------------------------------------------
    // ByeBye
    // -----------------------------------------------------------------------

    void onByeByeRequest(
        const aap_protobuf::service::control::message::ByeByeRequest& request) override {
        std::cout << "[Control] ByeByeRequest received. Session ending." << std::endl;
        aap_protobuf::service::control::message::ByeByeResponse response;
        channel_->sendShutdownResponse(response, makePromise("Control/ByeByeResponse"));
    }

    void onByeByeResponse(
        const aap_protobuf::service::control::message::ByeByeResponse& response) override {
        std::cout << "[Control] ByeByeResponse received." << std::endl;
    }

    // -----------------------------------------------------------------------
    // Ping
    // Ref: AndroidAutoEntity-17.cpp::onPingRequest()
    // -----------------------------------------------------------------------

    void onPingRequest(
        const aap_protobuf::service::control::message::PingRequest& request) override {
        if (!orchestrator_) throw std::runtime_error("Orchestrator non impostato");

        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_->onPingRequest(req_str);

        aap_protobuf::service::control::message::PingResponse response;
        if (!response.ParseFromString(res_str))
            throw std::runtime_error("PingResponse parse failed");

        channel_->sendPingResponse(response, makePromise("Control/PingResponse"));
        channel_->receive(this->shared_from_this());
    }

    void onPingResponse(
        const aap_protobuf::service::control::message::PingResponse& response) override {
        channel_->receive(this->shared_from_this());
    }

    void onChannelError(const aasdk::error::Error& e) override {
        std::cerr << "[Control] Channel Error: " << e.what() << std::endl;
    }

private:
    boost::asio::io_service::strand& strand_;
    aasdk::channel::control::IControlServiceChannel::Pointer channel_;
    aasdk::messenger::ICryptor::Pointer cryptor_;
    std::shared_ptr<IOrchestrator> orchestrator_;
};

} // namespace nemo
