#pragma once

#include <iostream>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkService.hpp>

namespace nemo {

class VideoEventHandler : public aasdk::channel::mediasink::video::IVideoMediaSinkServiceEventHandler {
public:
    using Pointer = std::shared_ptr<VideoEventHandler>;

    VideoEventHandler(aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel)
        : channel_(std::move(channel)) {}

    void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest& request) override {
        std::cout << "[Video] ChannelOpenRequest received." << std::endl;
        aap_protobuf::service::control::message::ChannelOpenResponse response;
        response.set_status(aap_protobuf::service::control::message::ChannelOpenResponse::OK);
        channel_->sendChannelOpenResponse(response, nullptr);
    }

    void onChannelSetupRequest(const aap_protobuf::service::media::video::message::VideoSetupRequest& request) override {
        std::cout << "[Video] ChannelSetupRequest received." << std::endl;
        
        // Accept config (mock phase 4)
        aap_protobuf::service::media::shared::message::Config response;
        channel_->sendChannelSetupResponse(response, nullptr);
    }

    void onStartIndication(const aap_protobuf::service::media::shared::message::StartIndication& request) override {
        std::cout << "[Video] StartIndication received. We should start decoding! (Fase 5)" << std::endl;
    }

    void onStopIndication(const aap_protobuf::service::media::shared::message::StopIndication& request) override {
        std::cout << "[Video] StopIndication received." << std::endl;
    }

    void onMediaWithTimestampIndication(
        std::chrono::microseconds timestamp,
        const aasdk::common::DataConstBuffer& payload) override {
        // Fase 4: dropping video frames. Fase 5: will extract NAL and feed to GStreamer
    }

    void onVideoFocusRequest(
        const aap_protobuf::service::media::video::message::VideoFocusRequest& request) override {
        std::cout << "[Video] VideoFocusRequest received." << std::endl;
        aap_protobuf::service::media::video::message::VideoFocusNotification response;
        response.set_focus_state(aap_protobuf::service::media::video::message::VideoFocusNotification::GAINED);
        response.set_reason(aap_protobuf::service::media::video::message::VideoFocusNotification::USER_REQUEST);
        channel_->sendVideoFocusIndication(response, nullptr);
    }

    void onChannelError(const aasdk::error::Error& e) override {
        std::cerr << "[Video] Channel Error: " << e.what() << std::endl;
    }

private:
    aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel_;
};

} // namespace nemo