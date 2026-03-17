#pragma once

#include <stdexcept>
#include <pybind11/pybind11.h>
#include <cstdint>
#include <aasdk/Channel/Promise.hpp>
#include <aasdk/Channel/MediaSink/Audio/IAudioMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Audio/AudioMediaSinkService.hpp>
#include <aap_protobuf/service/media/source/message/Ack.pb.h>
#include "app/media/av_core.hpp"
#include "handlers/common/event_binding.hpp"
#include "handlers/common/event_handler_utils.hpp"

namespace app
{

    // Strict pass-through: no parsing, no policy, no send. Python does everything.
    class AudioEventHandler
        : public aasdk::channel::mediasink::audio::IAudioMediaSinkServiceEventHandler,
          public EventHandlerBase<aasdk::channel::mediasink::audio::AudioMediaSinkService::Pointer>,
          public std::enable_shared_from_this<AudioEventHandler>
    {
    public:
        AudioEventHandler(boost::asio::io_service::strand &strand,
                          aasdk::channel::mediasink::audio::AudioMediaSinkService::Pointer channel,
                          std::shared_ptr<app::EventBinding> binding,
                          std::uintptr_t av_core_ptr = 0)
            : EventHandlerBase(strand, std::move(channel)),
              binding_(std::move(binding)),
              av_core_(reinterpret_cast<nemo::AvCore *>(av_core_ptr))
        {
            if (!binding_)
            {
                throw std::runtime_error("AudioEventHandler: null binding");
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
                av_core_->pushAudio(static_cast<int>(channel_->getId()), static_cast<uint64_t>(ts), buffer.cdata, buffer.size);
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

// Minimal binding defined alongside the handler (opt-in by including this header).
inline void init_audio_event_handler_binding(pybind11::module_ &m)
{

    pybind11::class_<aasdk::channel::mediasink::audio::AudioMediaSinkService,
                     std::shared_ptr<aasdk::channel::mediasink::audio::AudioMediaSinkService>>(
        m, "AudioChannel")
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               aasdk::messenger::IMessenger::Pointer messenger,
                               aasdk::messenger::ChannelId channel_id)
                            { return std::make_shared<aasdk::channel::mediasink::audio::AudioMediaSinkService>(
                                  *strand, std::move(messenger), channel_id); }),
             pybind11::arg("strand"),
             pybind11::arg("messenger"),
             pybind11::arg("channel_id"))
        .def("get_id",
             [](aasdk::channel::mediasink::audio::AudioMediaSinkService &ch)
             {
                 return static_cast<int>(ch.getId());
             })
        .def("receive",
             [](aasdk::channel::mediasink::audio::AudioMediaSinkService &ch,
                std::shared_ptr<app::AudioEventHandler> handler)
             {
                 ch.receive(handler);
             })
        .def("send_channel_setup_response", [](aasdk::channel::mediasink::audio::AudioMediaSinkService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendChannelSetupResponse(require_typed<aap_protobuf::service::media::shared::message::Config>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_channel_open_response", [](aasdk::channel::mediasink::audio::AudioMediaSinkService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendChannelOpenResponse(require_typed<aap_protobuf::service::control::message::ChannelOpenResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_media_ack", [](aasdk::channel::mediasink::audio::AudioMediaSinkService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendMediaAckIndication(require_typed<aap_protobuf::service::media::source::message::Ack>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none());

    pybind11::class_<app::AudioEventHandler, std::shared_ptr<app::AudioEventHandler>>(m, "AudioEventHandler")
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               std::shared_ptr<aasdk::channel::mediasink::audio::AudioMediaSinkService> channel,
                               std::shared_ptr<app::EventBinding> binding,
                               std::uintptr_t av_core_ptr)
                            { return std::make_shared<app::AudioEventHandler>(*strand, std::move(channel), std::move(binding), av_core_ptr); }),
             pybind11::arg("strand"),
             pybind11::arg("channel"),
             pybind11::arg("binding"),
             pybind11::arg("av_core_ptr") = 0,
             pybind11::keep_alive<1, 2>(),
             pybind11::keep_alive<1, 3>())
        .def_property_readonly("channel", &app::AudioEventHandler::channel)
        .def_property_readonly("strand", &app::AudioEventHandler::strand)
        .def_property_readonly("channel_ptr",
                               [](app::AudioEventHandler &self)
                               {
                                   return reinterpret_cast<std::uintptr_t>(self.channel().get());
                               })
        .def_property_readonly("strand_ptr",
                               [](app::AudioEventHandler &self)
                               {
                                   return reinterpret_cast<std::uintptr_t>(&self.strand());
                               });
}
