#pragma once

#include <iostream>
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
        response.set_status(aap_protobuf::service::control::message::AuthResponse::OK);
        channel_->sendAuthComplete(response, nullptr);
    }

    void onServiceDiscoveryRequest(const aap_protobuf::service::control::message::ServiceDiscoveryRequest &request) override {
        std::cout << "[Control] ServiceDiscoveryRequest received." << std::endl;
        aap_protobuf::service::control::message::ServiceDiscoveryResponse response;
        
        response.set_head_unit_name("NemoHeadUnit");
        response.set_car_model("MVP");
        response.set_car_year("2026");
        response.set_car_serial("00000001");
        
        // In the real impl we will add the discovered services (Audio, Video, Input) here.
        // For now, an empty list tells the phone we support nothing, which is fine for Phase 4 step 1.
        
        channel_->sendServiceDiscoveryResponse(response, nullptr);
    }

    void onAudioFocusRequest(const aap_protobuf::service::control::message::AudioFocusRequest &request) override {
        std::cout << "[Control] AudioFocusRequest received." << std::endl;
        aap_protobuf::service::control::message::AudioFocusNotification response;
        response.set_focus_type(request.focus_type());
        response.set_focus_state(aap_protobuf::service::control::message::AudioFocusNotification::GAIN);
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