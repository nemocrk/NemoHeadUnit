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

class ControlEventHandler : public aasdk::channel::control::IControlServiceChannelEventHandler, public std::enable_shared_from_this<ControlEventHandler> {
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
                std::cout << "[" << tag << "] send promise fulfilled (SUCCESS)." << std::endl;
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

        aasdk::common::Data data(first_chunk.begin(), first_chunk.end());
        std::cout << "[Control] Invio primo Handshake Chunk via AASDK..." << std::endl;
        channel_->sendHandshake(data, makePromise("Control/SendHandshake1"));
        channel_->receive(this->shared_from_this());
    }

    void onHandshake(const aasdk::common::DataConstBuffer &payload) override {
        std::cout << "[Control] Handshake chunk ricevuto, size: " << payload.size << std::endl;
        if (!orchestrator_) {
            throw std::runtime_error("Orchestrator non impostato");
        }

        std::string in(reinterpret_cast<const char*>(payload.cdata), payload.size);
        std::string out = orchestrator_->onHandshake(in);
        
        if (!out.empty()) {
            aasdk::common::Data data(out.begin(), out.end());
            std::cout << "[Control] Invio successivo Handshake Chunk via AASDK..." << std::endl;
            channel_->sendHandshake(data, makePromise("Control/SendHandshakeX"));
        } else {
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

        channel_->receive(this->shared_from_this());
    }

    // NOTA: Android Auto ha la ServiceDiscovery *Invertita* rispetto ai vecchi docs.
    // Lo smartphone manda la Request; la HU risponde con la Response popolata.
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
        channel_->receive(this->shared_from_this());
    }

    // Dopo la ServiceDiscoveryResponse, Android Auto apre ogni canale con una ChannelOpenRequest
    // sul canale CONTROL. La HU deve rispondere con ChannelOpenResponse status=OK e mettere
    // in ascolto il canale specifico (qui delegato all'orchestrator via Python).
    void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) override {
        const auto channelId = request.channel_id();
        std::cout << "[Control] ChannelOpenRequest per ChannelId=" << channelId << std::endl;
        if (!orchestrator_) throw std::runtime_error("Orchestrator non impostato");

        // Delega a Python la costruzione della risposta (status=0 = OK)
        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_->onChannelOpenRequest(req_str);

        aap_protobuf::service::control::message::ChannelOpenResponse response;
        if (!response.ParseFromString(res_str)) {
            throw std::runtime_error("ChannelOpenResponse ParseFromString failed");
        }

        channel_->sendChannelOpenResponse(response, makePromise("Control/ChannelOpenResponse"));
        channel_->receive(this->shared_from_this());
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
        channel_->receive(this->shared_from_this());
    }

    void onByeByeRequest(const aap_protobuf::service::control::message::ByeByeRequest &request) override {
        std::cout << "[Control] ByeByeRequest received. Session ending." << std::endl;
    }

    void onByeByeResponse(const aap_protobuf::service::control::message::ByeByeResponse &response) override {
        std::cout << "[Control] ByeByeResponse received." << std::endl;
    }

    void onBatteryStatusNotification(const aap_protobuf::service::control::message::BatteryStatusNotification &notification) override {
        channel_->receive(this->shared_from_this());
    }

    void onNavigationFocusRequest(const aap_protobuf::service::control::message::NavFocusRequestNotification &request) override {
        std::cout << "[Control] NavFocusRequest received." << std::endl;
        channel_->receive(this->shared_from_this());
    }

    void onVoiceSessionRequest(const aap_protobuf::service::control::message::VoiceSessionNotification &request) override {
        std::cout << "[Control] VoiceSessionRequest received." << std::endl;
        channel_->receive(this->shared_from_this());
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
        channel_->receive(this->shared_from_this());
    }

    void onPingResponse(const aap_protobuf::service::control::message::PingResponse &response) override {
        channel_->receive(this->shared_from_this());
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
