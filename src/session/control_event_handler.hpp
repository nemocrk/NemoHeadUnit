#pragma once

#include <iostream>
#include <string>
#include <aasdk/Channel/Control/IControlServiceChannelEventHandler.hpp>
#include <aasdk/Channel/Control/IControlServiceChannel.hpp>

namespace nemo {

class ControlEventHandler : public aasdk::channel::control::IControlServiceChannelEventHandler {
public:
    using Pointer = std::shared_ptr<ControlEventHandler>;

    ControlEventHandler(aasdk::channel::control::IControlServiceChannel::Pointer channel)
        : channel_(std::move(channel)) {}

    void onVersionResponse(uint16_t majorCode, uint16_t minorCode, aap_protobuf::shared::MessageStatus status) override {
        std::cout << "[Control] VersionResponse: " << majorCode << "." << minorCode << " Status: " << status << std::endl;
    }

    void onHandshake(const aasdk::common::DataConstBuffer &payload) override {
        std::cout << "[Control] Handshake received. Sending AuthComplete..." << std::endl;
        aap_protobuf::service::control::message::AuthResponse response;
        response.set_status(static_cast<decltype(response.status())>(0));
        channel_->sendAuthComplete(response, nullptr);
    }

    void onServiceDiscoveryRequest(const aap_protobuf::service::control::message::ServiceDiscoveryRequest &request) override {
        std::cout << "[Control] ServiceDiscoveryRequest received." << std::endl;
        aap_protobuf::service::control::message::ServiceDiscoveryResponse response;
        
        response.set_head_unit_make(std::string("NemoHeadUnit"));
        response.set_model(std::string("MVP"));
        response.set_year(std::string("2026"));
        
        channel_->sendServiceDiscoveryResponse(response, nullptr);
    }

    void onAudioFocusRequest(const aap_protobuf::service::control::message::AudioFocusRequest &request) override {
        std::cout << "[Control] AudioFocusRequest received." << std::endl;
        aap_protobuf::service::control::message::AudioFocusNotification response;
        response.set_focus_state(static_cast<decltype(response.focus_state())>(1));
        channel_->sendAudioFocusResponse(response, nullptr);
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
        channel_->sendPingResponse(response, nullptr);
    }

    void onPingResponse(const aap_protobuf::service::control::message::PingResponse &response) override {
        // Ignore
    }

    void onChannelError(const aasdk::error::Error &e) override {
        std::cerr << "[Control] Channel Error: " << e.what() << std::endl;
    }

private:
    aasdk::channel::control::IControlServiceChannel::Pointer channel_;
};

} // namespace nemo
