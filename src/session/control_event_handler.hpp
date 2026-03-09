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
            []() {},
            [tag](const aasdk::error::Error& e) {
                std::cerr << "[" << tag << "] send failed: " << e.what() << std::endl;
            }
        );
        return p;
    }

    void onVersionResponse(uint16_t majorCode, uint16_t minorCode, aap_protobuf::shared::MessageStatus status) override {
        std::cout << "[Control] VersionResponse: " << majorCode << "." << minorCode << " Status: " << status << std::endl;
        if (orchestrator_) {
            orchestrator_->onVersionStatus(majorCode, minorCode, status);
        }
    }

    void onHandshake(const aasdk::common::DataConstBuffer &payload) override {
        std::cout << "[Control] Handshake chunk ricevuto, size: " << payload.size << std::endl;
        if (orchestrator_) {
            // Convert buffer to string payload per inviarlo a Python
            std::string payload_bytes(reinterpret_cast<const char*>(payload.data), payload.size);
            std::string response_chunk = orchestrator_->onHandshake(payload_bytes);
            
            if (!response_chunk.empty()) {
                // Il python ci ha restituito il prossimo chunk da mandare per procedere
                aasdk::common::Data chunk_data(response_chunk.begin(), response_chunk.end());
                channel_->sendHandshake(aasdk::common::DataConstBuffer(chunk_data), makePromise("Control/SendHandshakeX"));
            } else {
                // Risposta vuota da python = Handshake terminato, mando io l'AuthComplete
                std::cout << "[Control] Handshake Python completato. Invio AuthComplete..." << std::endl;
                aap_protobuf::service::control::message::AuthResponse response;
                response.set_status(static_cast<decltype(response.status())>(0));
                channel_->sendAuthComplete(response, makePromise("Control/AuthComplete"));
            }
        }
    }

    // ... il resto resta uguale ...
    void onServiceDiscoveryRequest(const aap_protobuf::service::control::message::ServiceDiscoveryRequest &request) override {
        std::cout << "[Control] ServiceDiscoveryRequest received." << std::endl;
        
        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_ ? orchestrator_->onServiceDiscoveryRequest(req_str) : "";
        
        aap_protobuf::service::control::message::ServiceDiscoveryResponse response;
        if (!res_str.empty()) {
            response.ParseFromString(res_str);
        } else {
            // Fallback mock C++
            response.set_head_unit_make(std::string("NemoHeadUnit"));
            response.set_model(std::string("MVP"));
            response.set_year(std::string("2026"));
        }
        
        channel_->sendServiceDiscoveryResponse(response, makePromise("Control/ServiceDiscoveryResponse"));
    }

    void onAudioFocusRequest(const aap_protobuf::service::control::message::AudioFocusRequest &request) override {
        std::cout << "[Control] AudioFocusRequest received." << std::endl;
        
        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_ ? orchestrator_->onAudioFocusRequest(req_str) : "";
        
        aap_protobuf::service::control::message::AudioFocusNotification response;
        if (!res_str.empty()) {
            response.ParseFromString(res_str);
        } else {
            response.set_focus_state(static_cast<decltype(response.focus_state())>(1));
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
        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_ ? orchestrator_->onPingRequest(req_str) : "";

        aap_protobuf::service::control::message::PingResponse response;
        if (!res_str.empty()) {
            response.ParseFromString(res_str);
        } else {
            response.set_timestamp(request.timestamp());
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
