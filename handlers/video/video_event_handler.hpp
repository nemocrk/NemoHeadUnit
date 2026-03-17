#pragma once

#include <stdexcept>
#include <cstdint>
#include <pybind11/pybind11.h>

#include <aasdk/Common/Data.hpp>
#include <aasdk/Channel/Promise.hpp>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Video/VideoMediaSinkService.hpp>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/media/shared/message/Config.pb.h>
#include <aap_protobuf/service/media/source/message/Ack.pb.h>
#include <aap_protobuf/service/media/video/message/VideoFocusNotification.pb.h>

#include "handlers/common/event_binding.hpp"
#include "handlers/common/event_handler_utils.hpp"
#include "app/media/av_core.hpp"

namespace app
{

    // Strict pass-through: no parsing, no policy, no send. Python does everything.
    class VideoEventHandler
        : public aasdk::channel::mediasink::video::IVideoMediaSinkServiceEventHandler,
          public EventHandlerBase<aasdk::channel::mediasink::video::VideoMediaSinkService::Pointer>,
          public std::enable_shared_from_this<VideoEventHandler>
    {
    public:
        VideoEventHandler(boost::asio::io_service::strand &strand,
                          aasdk::channel::mediasink::video::VideoMediaSinkService::Pointer channel,
                          std::shared_ptr<app::EventBinding> binding,
                          std::uintptr_t av_core_ptr = 0)
            : EventHandlerBase(strand, std::move(channel)),
              binding_(std::move(binding)),
              av_core_(reinterpret_cast<nemo::AvCore *>(av_core_ptr))
        {
            if (!binding_)
            {
                throw std::runtime_error("VideoEventHandler: null binding");
            }
        }

        using EventHandlerBase::channel;
        using EventHandlerBase::strand;

        void onMediaChannelSetupRequest(
            const aap_protobuf::service::media::shared::message::Setup &request) override
        {
            CALL_PY(request);
        }

        void onChannelOpenRequest(
            const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
        {
            CALL_PY(request);
        }

        void onVideoFocusRequest(
            const aap_protobuf::service::media::video::message::VideoFocusRequestNotification &request) override
        {
            CALL_PY(request);
        }

        void onMediaChannelStartIndication(
            const aap_protobuf::service::media::shared::message::Start &indication) override
        {
            CALL_PY(indication);
        }

        void onMediaChannelStopIndication(
            const aap_protobuf::service::media::shared::message::Stop &indication) override
        {
            CALL_PY(indication);
        }

        void onMediaWithTimestampIndication(
            aasdk::messenger::Timestamp::ValueType ts, const aasdk::common::DataConstBuffer &buffer) override
        {
            if (av_core_)
            {
                av_core_->pushVideo(static_cast<uint64_t>(ts), buffer.cdata, buffer.size);
            }
            CALL_PY("");
        }

        void onMediaIndication(const aasdk::common::DataConstBuffer &buffer) override
        {
            onMediaWithTimestampIndication(0, buffer);
        }

        void onChannelError(const aasdk::error::Error &e) override
        {
            CALL_PY(e.what());
        }

    private:
        std::shared_ptr<app::EventBinding> binding_;
        nemo::AvCore *av_core_ = nullptr;
    };

} // namespace app

// Minimal binding defined alongside thehandler (opt-in by including this header).
inline void init_video_event_handler_binding(pybind11::module_ &m)
{

    pybind11::class_<aasdk::channel::mediasink::video::VideoMediaSinkService,
                     std::shared_ptr<aasdk::channel::mediasink::video::VideoMediaSinkService>>(
        m, "VideoChannel", pybind11::module_local())
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               aasdk::messenger::IMessenger::Pointer messenger,
                               aasdk::messenger::ChannelId channel_id)
                            { return std::make_shared<aasdk::channel::mediasink::video::VideoMediaSinkService>(
                                  *strand, std::move(messenger), channel_id); }),
             pybind11::arg("strand"),
             pybind11::arg("messenger"),
             pybind11::arg("channel_id"))
        .def("get_id",
             [](aasdk::channel::mediasink::video::VideoMediaSinkService &ch)
             {
                 return static_cast<int>(ch.getId());
             })
        .def("receive",
             [](aasdk::channel::mediasink::video::VideoMediaSinkService &ch,
                std::shared_ptr<app::VideoEventHandler> handler)
             {
                 ch.receive(handler);
             })
        .def("send_channel_setup_response", [](aasdk::channel::mediasink::video::VideoMediaSinkService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendChannelSetupResponse(require_typed<aap_protobuf::service::media::shared::message::Config>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_channel_open_response", [](aasdk::channel::mediasink::video::VideoMediaSinkService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendChannelOpenResponse(require_typed<aap_protobuf::service::control::message::ChannelOpenResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_media_ack", [](aasdk::channel::mediasink::video::VideoMediaSinkService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendMediaAckIndication(require_typed<aap_protobuf::service::media::source::message::Ack>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_video_focus_indication", [](aasdk::channel::mediasink::video::VideoMediaSinkService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendVideoFocusIndication(require_typed<aap_protobuf::service::media::video::message::VideoFocusNotification>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none());

    pybind11::class_<app::VideoEventHandler, std::shared_ptr<app::VideoEventHandler>>(m, "VideoEventHandler")
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               std::shared_ptr<aasdk::channel::mediasink::video::VideoMediaSinkService> channel,
                               std::shared_ptr<app::EventBinding> binding,
                               std::uintptr_t av_core_ptr)
                            { return std::make_shared<app::VideoEventHandler>(*strand, std::move(channel), std::move(binding), av_core_ptr); }),
             pybind11::arg("strand"),
             pybind11::arg("channel"),
             pybind11::arg("binding"),
             pybind11::arg("av_core_ptr") = 0,
             pybind11::keep_alive<1, 2>(),
             pybind11::keep_alive<1, 3>())
        .def_property_readonly("channel", &app::VideoEventHandler::channel)
        .def_property_readonly("strand", &app::VideoEventHandler::strand)
        .def_property_readonly("channel_ptr",
                               [](app::VideoEventHandler &self)
                               {
                                   return reinterpret_cast<std::uintptr_t>(self.channel().get());
                               })
        .def_property_readonly("strand_ptr",
                               [](app::VideoEventHandler &self)
                               {
                                   return reinterpret_cast<std::uintptr_t>(&self.strand());
                               });
}
