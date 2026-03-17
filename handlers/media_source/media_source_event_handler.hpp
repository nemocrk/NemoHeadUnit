#pragma once

#include <stdexcept>
#include <cstdint>
#include <atomic>
#include <thread>
#include <chrono>
#include <pybind11/pybind11.h>

#include <aasdk/Common/Data.hpp>
#include <aasdk/Channel/Promise.hpp>
#include <aasdk/Channel/MediaSource/IMediaSourceServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSource/MediaSourceService.hpp>
#include <aasdk/Channel/MediaSource/Audio/MicrophoneAudioChannel.hpp>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/media/shared/message/Config.pb.h>
#include <aap_protobuf/service/media/source/message/MicrophoneResponse.pb.h>

#include "handlers/common/event_binding.hpp"
#include "handlers/common/event_handler_utils.hpp"
#include "app/media/av_core.hpp"

namespace app
{

    // Strict pass-through: no parsing, no policy, no send. Python does everything.
    class MediaSourceEventHandler
        : public aasdk::channel::mediasource::IMediaSourceServiceEventHandler,
          public EventHandlerBase<aasdk::channel::mediasource::MediaSourceService::Pointer>,
          public std::enable_shared_from_this<MediaSourceEventHandler>
    {
    public:
        MediaSourceEventHandler(boost::asio::io_service::strand &strand,
                                aasdk::channel::mediasource::MediaSourceService::Pointer channel,
                                std::shared_ptr<app::EventBinding> binding,
                                std::uintptr_t av_core_ptr = 0)
            : EventHandlerBase(strand, std::move(channel)),
              binding_(std::move(binding)),
              av_core_(reinterpret_cast<nemo::AvCore *>(av_core_ptr))
        {
            if (!binding_)
            {
                throw std::runtime_error("MediaSourceEventHandler: null binding");
            }
        }

        ~MediaSourceEventHandler()
        {
            stopMicStreaming();
        }

        using EventHandlerBase::channel;
        using EventHandlerBase::strand;

        void onChannelOpenRequest(
            const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
        {
            CALL_PY(request);
        }

        void onMediaChannelSetupRequest(
            const aap_protobuf::service::media::shared::message::Setup &request) override
        {
            CALL_PY(request);
        }

        void onMediaSourceOpenRequest(
            const aap_protobuf::service::media::source::message::MicrophoneRequest &request) override
        {
            if (av_core_)
            {
                if (request.open())
                {
                    av_core_->setMicActive(true);
                    av_core_->startMicCapture();
                    startMicStreaming();
                }
                else
                {
                    stopMicStreaming();
                    av_core_->stopMicCapture();
                    av_core_->setMicActive(false);
                }
            }
            CALL_PY(request);
        }

        void onMediaChannelAckIndication(
            const aap_protobuf::service::media::source::message::Ack &indication) override
        {
            CALL_PY(indication);
        }

        void onChannelError(const aasdk::error::Error &e) override
        {
            CALL_PY(e.what());
        }

    private:
        void startMicStreaming()
        {
            if (mic_running_.exchange(true))
                return;
            mic_thread_ = std::thread([self = this->shared_from_this()]()
                                      { self->micLoop(); });
        }

        void stopMicStreaming()
        {
            mic_running_.store(false);
            if (mic_thread_.joinable())
                mic_thread_.join();
        }

        void micLoop()
        {
            while (mic_running_.load())
            {
                if (!av_core_)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    continue;
                }
                nemo::AvFrame frame;
                if (!av_core_->popMicFrame(frame, 50))
                    continue;
                sendMicFrame(std::move(frame));
            }
        }

        void sendMicFrame(nemo::AvFrame frame)
        {
            auto self = this->shared_from_this();
            strand_.dispatch([self, frame = std::move(frame)]() mutable
                             {
                                 aasdk::common::Data data(frame.data.begin(), frame.data.end());
                                 with_promise(self->strand_, [&](auto p)
                                              { self->channel_->sendMediaSourceWithTimestampIndication(frame.ts_us, data, std::move(p)); }); });
        }

        std::shared_ptr<app::EventBinding> binding_;
        nemo::AvCore *av_core_ = nullptr;
        std::atomic<bool> mic_running_{false};
        std::thread mic_thread_;
    };

} // namespace app

// Minimal binding defined alongside thehandler (opt-in by including this header).
inline void init_media_source_event_handler_binding(pybind11::module_ &m)
{

    pybind11::class_<aasdk::channel::mediasource::MediaSourceService,
                     std::shared_ptr<aasdk::channel::mediasource::MediaSourceService>>(m, "MediaSourceChannel", pybind11::module_local())
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               aasdk::messenger::IMessenger::Pointer messenger)
                            { return std::make_shared<aasdk::channel::mediasource::audio::MicrophoneAudioChannel>(
                                  *strand, std::move(messenger)); }),
             pybind11::arg("strand"),
             pybind11::arg("messenger"))
        .def("get_id",
             [](aasdk::channel::mediasource::MediaSourceService &ch)
             {
                 return static_cast<int>(ch.getId());
             })
        .def("receive",
             [](aasdk::channel::mediasource::MediaSourceService &ch,
                std::shared_ptr<app::MediaSourceEventHandler> handler)
             {
                 ch.receive(handler);
             })
        .def("send_channel_open_response", [](aasdk::channel::mediasource::MediaSourceService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendChannelOpenResponse(require_typed<aap_protobuf::service::control::message::ChannelOpenResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_channel_setup_response", [](aasdk::channel::mediasource::MediaSourceService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendChannelSetupResponse(require_typed<aap_protobuf::service::media::shared::message::Config>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_microphone_open_response", [](aasdk::channel::mediasource::MediaSourceService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendMicrophoneOpenResponse(require_typed<aap_protobuf::service::media::source::message::MicrophoneResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_media_source_with_timestamp", [](aasdk::channel::mediasource::MediaSourceService &ch, std::uint64_t timestamp, pybind11::bytes data, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             {
                 std::string raw = data;
                 aasdk::common::Data buf(raw.begin(), raw.end());
                 with_promise(strand, [&](auto p)
                              { ch.sendMediaSourceWithTimestampIndication(timestamp, buf, std::move(p)); }, then_cb); }, pybind11::arg("timestamp"), pybind11::arg("data"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none());

    pybind11::class_<app::MediaSourceEventHandler, std::shared_ptr<app::MediaSourceEventHandler>>(m, "MediaSourceEventHandler")
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               std::shared_ptr<aasdk::channel::mediasource::MediaSourceService> channel,
                               std::shared_ptr<app::EventBinding> binding,
                               std::uintptr_t av_core_ptr)
                            { return std::make_shared<app::MediaSourceEventHandler>(*strand, std::move(channel), std::move(binding), av_core_ptr); }),
             pybind11::arg("strand"),
             pybind11::arg("channel"),
             pybind11::arg("binding"),
             pybind11::arg("av_core_ptr") = 0,
             pybind11::keep_alive<1, 2>(),
             pybind11::keep_alive<1, 3>())
        .def_property_readonly(
            "channel",
            [](app::MediaSourceEventHandler &self)
            {
                // Ensure Python gets the concrete MediaSourceService binding, not the interface pointer.
                return std::static_pointer_cast<aasdk::channel::mediasource::MediaSourceService>(self.channel());
            })
        .def_property_readonly("strand", &app::MediaSourceEventHandler::strand)
        .def_property_readonly("channel_ptr",
                               [](app::MediaSourceEventHandler &self)
                               {
                                   return reinterpret_cast<std::uintptr_t>(self.channel().get());
                               })
        .def_property_readonly("strand_ptr",
                               [](app::MediaSourceEventHandler &self)
                               {
                                   return reinterpret_cast<std::uintptr_t>(&self.strand());
                               });
}
