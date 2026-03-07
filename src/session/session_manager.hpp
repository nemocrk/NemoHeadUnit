#pragma once

#include <memory>
#include <boost/asio.hpp>
#include <aasdk/Messenger/IMessenger.hpp>
#include <aasdk/Channel/Control/ControlServiceChannel.hpp>
#include <aasdk/Channel/MediaSink/Video/VideoMediaSinkService.hpp>
#include <aasdk/Channel/Promise.hpp>
#include "control_event_handler.hpp"
#include "video_event_handler.hpp"

namespace nemo {

class SessionManager : public std::enable_shared_from_this<SessionManager> {
public:
    using Pointer = std::shared_ptr<SessionManager>;

    SessionManager(boost::asio::io_context& io_ctx, aasdk::messenger::IMessenger::Pointer messenger)
        : strand_(io_ctx), messenger_(std::move(messenger)) {}

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

    void start() {
        strand_.dispatch([this, self = shared_from_this()]() {
            std::cout << "[SessionManager] Starting Mock Channels..." << std::endl;

            // Start Control Service (Channel 0)
            control_channel_ = std::make_shared<aasdk::channel::control::ControlServiceChannel>(
                strand_, messenger_
            );
            control_handler_ = std::make_shared<ControlEventHandler>(strand_, control_channel_);
            control_channel_->receive(control_handler_);
            control_channel_->sendVersionRequest(makePromise("Control/VersionRequest"));

            // Start Video Sink Service (Channel 1 - Usually it's channel 2 or dynamically assigned by discovery)
            // For the mock, we assign it ID 2.
            const auto videoChannelId = static_cast<aasdk::messenger::ChannelId>(2);
            video_channel_ = std::make_shared<aasdk::channel::mediasink::video::VideoMediaSinkService>(
                strand_, messenger_, videoChannelId
            );
            video_handler_ = std::make_shared<VideoEventHandler>(strand_, video_channel_);
            video_channel_->receive(video_handler_);
        });
    }

    void stop() {
        strand_.dispatch([this, self = shared_from_this()]() {
            control_channel_.reset();
            control_handler_.reset();
            video_channel_.reset();
            video_handler_.reset();
        });
    }

private:
    boost::asio::io_service::strand strand_;
    aasdk::messenger::IMessenger::Pointer messenger_;

    aasdk::channel::control::IControlServiceChannel::Pointer control_channel_;
    ControlEventHandler::Pointer control_handler_;

    aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer video_channel_;
    VideoEventHandler::Pointer video_handler_;
};

} // namespace nemo
