#pragma once

#include <iostream>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkService.hpp>

namespace nemo {

class VideoEventHandler : public aasdk::channel::mediasink::video::IVideoMediaSinkServiceEventHandler {
public:
    using Pointer = std::shared_ptr<VideoEventHandler>;

    explicit VideoEventHandler(aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel)
        : channel_(std::move(channel)) {}

    void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest& request) override {
        std::cout << "[Video] ChannelOpenRequest received." << std::endl;
        aap_protobuf::service::control::message::ChannelOpenResponse response;
        response.set_status(static_cast<decltype(response.status())>(0));
        channel_->sendChannelOpenResponse(response, nullptr);
    }

    void onMediaChannelSetupRequest(const aap_protobuf::service::media::shared::message::Setup& request) override {
        std::cout << "[Video] Setup received (mock)." << std::endl;
        // In Fase 4 possiamo non rispondere o rispondere con default se API lo prevede lato channel_.
    }

    void onMediaChannelStartIndication(const aap_protobuf::service::media::shared::message::Start& indication) override {
        std::cout << "[Video] Start received." << std::endl;
    }

    void onMediaChannelStopIndication(const aap_protobuf::service::media::shared::message::Stop& indication) override {
        std::cout << "[Video] Stop received." << std::endl;
    }

    void onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType ts,
                                        const aasdk::common::DataConstBuffer& buffer) override {
        (void)ts; (void)buffer;
        // Fase 4: drop frame. Fase 5: NAL -> pipeline C++ (GStreamer/libavcodec).
    }

    void onMediaIndication(const aasdk::common::DataConstBuffer& buffer) override {
        (void)buffer;
    }

    void onVideoFocusRequest(
        const aap_protobuf::service::media::video::message::VideoFocusRequestNotification& request) override {
        std::cout << "[Video] Focus request." << std::endl;
        (void)request;
    }

    void onChannelError(const aasdk::error::Error& e) override {
        std::cerr << "[Video] Channel Error: " << e.what() << std::endl;
    }

private:
    aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel_;
};

} // namespace nemo
