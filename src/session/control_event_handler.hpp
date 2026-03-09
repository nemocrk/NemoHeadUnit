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

class ControlEventHandler : public aasdk::channel::control::IControlServiceChannelEventHandler {
public:
    using Pointer = std::shared_ptr<ControlEventHandler>;

    ControlEventHandler(boost::asio::io_service::strand& strand, 
                        aasdk::channel::control::IControlServiceChannel::Pointer channel,
                        aasdk::messenger::ICryptor::Pointer cryptor,
                        std::shared_ptr<IOrchestrator> orchestrator)
        : strand_(strand), channel_(std::move(channel)), cryptor_(std::move(cryptor)), orchestrator_(std::move(orchestrator)) {}

    aasdk::channel::SendPromise::Pointer makePromise(const char* tag) {
        auto p = aasdk::channel::SendPromise::defer(strand_);
        p->then(
            [tag]() {
                std::cout << "[" << tag << "] send promise fulfilled (SUCCESS)" << std::endl;
            },
            [tag](const aasdk::error::Error& e) {
                std::cerr << "[" << tag << "] send failed: " << e.what() << std::endl;
            }
        );
        return p;
    }

    void onVersionResponse(uint16_t majorCode, uint16_t minorCode, aap_protobuf::shared::MessageStatus status) override {
        std::cout << "[Control] VersionResponse: " << majorCode << "." << minorCode << " Status: " << status << std::endl;
        if (!orchestrator_) {
            throw std::runtime_error("Orchestrator non impostato");
        }

        std::cout << "[Control] Chiamo Python onVersionStatus..." << std::endl;
        std::string first_chunk = orchestrator_->onVersionStatus(majorCode, minorCode, status);
        std::cout << "[Control] Python ha ritornato chunk TLS di size: " << first_chunk.size() << std::endl;

        if (first_chunk.empty()) {
            throw std::runtime_error("Python MUST return first handshake chunk");
        }

        // Invia primo flight TLS deciso da Python
        aasdk::common::Data data(first_chunk.begin(), first_chunk.end());
        std::cout << "[Control] Invio primo Handshake Chunk via AASDK..." << std::endl;
        channel_->sendHandshake(data, makePromise("Control/SendHandshake1"));
        std::cout << "[Control] Chiamata sendHandshake1 completata (asincrona)." << std::endl;
    }

    void onHandshake(const aasdk::common::DataConstBuffer &payload) override {
        std::cout << "[Control] Handshake chunk ricevuto, size: " << payload.size << std::endl;
        if (!orchestrator_) {
            throw std::runtime_error("Orchestrator non impostato");
        }

        std::string in(reinterpret_cast<const char*>(payload.cdata), payload.size);
        std::string out = orchestrator_->onHandshake(in);
        
        if (!out.empty()) {
            // C'è ancora handshake da fare
            aasdk::common::Data data(out.begin(), out.end());
            std::cout << "[Control] Invio successivo Handshake Chunk via AASDK..." << std::endl;
            channel_->sendHandshake(data, makePromise("Control/SendHandshakeX"));
            return;
        }

        // out è vuoto => Python ha dichiarato handshake finito.
        // Python DEVE fornire l'AuthCompleteRequest Protobuf.
        std::cout << "[Control] Handshake TLS completato da Python. Richiesta AuthResponse..." << std::endl;
        std::string auth_bytes = orchestrator_->getAuthCompleteResponse();
        if (auth_bytes.empty()) {
            throw std::runtime_error("Python MUST return AuthComplete/AuthResponse bytes");
        }

        aap_protobuf::service::control::message::AuthResponse response;
        if (!response.ParseFromString(auth_bytes)) {
            throw std::runtime_error("AuthResponse ParseFromString failed");
        }
        channel_->sendAuthComplete(response, makePromise("Control/AuthComplete"));
    }

    void onServiceDiscoveryRequest(const aap_protobuf::service::control::message::ServiceDiscoveryRequest &request) override {
        std::cout << "[Control] ServiceDiscoveryRequest received." << std::endl;
        if (!orchestrator_) throw std::runtime_error("Orchestrator non impostato");
        
        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_->onServiceDiscoveryRequest(req_str);
        
        aap_protobuf::service::control::message::ServiceDiscoveryResponse response;
        if (!response.ParseFromString(res_str)) {
             throw std::runtime_error("ServiceDiscoveryResponse ParseFromString failed");
        }
        
        channel_->sendServiceDiscoveryResponse(response, makePromise("Control/ServiceDiscoveryResponse"));
    }

    void onAudioFocusRequest(const aap_protobuf::service::control::message::AudioFocusRequest &request) override {
        std::cout << "[Control] AudioFocusRequest received." << std::endl;
        if (!orchestrator_) throw std::runtime_error("Orchestrator non impostato");
        
        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_->onAudioFocusRequest(req_str);
        
        aap_protobuf::service::control::message::AudioFocusNotification response;
        if (!response.ParseFromString(res_str)) {
            throw std::runtime_error("AudioFocusNotification ParseFromString failed");
        }
        
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
        if (!orchestrator_) throw std::runtime_error("Orchestrator non impostato");

        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_->onPingRequest(req_str);

        aap_protobuf::service::control::message::PingResponse response;
        if (!response.ParseFromString(res_str)) {
             throw std::runtime_error("PingResponse ParseFromString failed");
        }
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
    std::shared_ptr<IOrchestrator> orchestrator_;
};

} // namespace nemo
