#pragma once

#include <iostream>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkService.hpp>
#include <aasdk/Channel/Promise.hpp>
#include "iorchestrator.hpp"

namespace nemo {

class VideoEventHandler : public aasdk::channel::mediasink::video::IVideoMediaSinkServiceEventHandler {
public:
    using Pointer = std::shared_ptr<VideoEventHandler>;

    explicit VideoEventHandler(boost::asio::io_service::strand& strand, 
                               aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel,
                               std::shared_ptr<IOrchestrator> orchestrator)
        : strand_(strand), channel_(std::move(channel)), orchestrator_(std::move(orchestrator)) {}

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

    void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest& request) override {
        std::cout << "[Video] ChannelOpenRequest received." << std::endl;
        
        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_ ? orchestrator_->onVideoChannelOpenRequest(req_str) : "";

        aap_protobuf::service::control::message::ChannelOpenResponse response;
        if (!res_str.empty()) {
            response.ParseFromString(res_str);
        } else {
            response.set_status(static_cast<decltype(response.status())>(0));
        }
        channel_->sendChannelOpenResponse(response, makePromise("Video/ChannelOpenResponse"));
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
    boost::asio::io_service::strand& strand_;
    aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel_;
    std::shared_ptr<IOrchestrator> orchestrator_;
};

} // namespace nemo
