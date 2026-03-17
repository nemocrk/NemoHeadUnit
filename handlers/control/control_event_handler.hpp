#pragma once

#include <cstdint>
#include <string>
#include <pybind11/pybind11.h>

#include <aasdk/Common/Data.hpp>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <aap_protobuf/service/control/message/ByeByeRequest.pb.h>
#include <aasdk/Channel/Control/IControlServiceChannelEventHandler.hpp>
#include <aasdk/Channel/Control/ControlServiceChannel.hpp>

#include "handlers/common/event_binding.hpp"
#include "handlers/common/event_handler_utils.hpp"

namespace app
{

    class ControlEventHandler
        : public aasdk::channel::control::IControlServiceChannelEventHandler,
          public EventHandlerBase<aasdk::channel::control::ControlServiceChannel::Pointer>,
          public std::enable_shared_from_this<ControlEventHandler>
    {
    public:
        using Pointer = std::shared_ptr<ControlEventHandler>;

        ControlEventHandler(boost::asio::io_service::strand &strand,
                            aasdk::channel::control::ControlServiceChannel::Pointer channel,
                            std::shared_ptr<app::EventBinding> binding)
            : EventHandlerBase(strand, std::move(channel)),
              binding_(std::move(binding))
        {
            if (!binding_)
            {
                throw std::runtime_error("ControlEventHandler: null binding");
            }
        }

        using EventHandlerBase::channel;
        using EventHandlerBase::strand;

        void onVersionResponse(uint16_t majorCode, uint16_t minorCode,
                               aap_protobuf::shared::MessageStatus status) override
        {
            const std::string payload =
                std::to_string(majorCode) + "|" + std::to_string(minorCode) + "|" + std::to_string(static_cast<int>(status));
            CALL_PY(payload);
        }

        void onHandshake(const aasdk::common::DataConstBuffer &payload) override
        {
            const std::string data(reinterpret_cast<const char *>(payload.cdata), payload.size);
            CALL_PY(data);
        }

        void onServiceDiscoveryRequest(
            const aap_protobuf::service::control::message::ServiceDiscoveryRequest &request) override
        {
            CALL_PY(request);
        }

        void onAudioFocusRequest(
            const aap_protobuf::service::control::message::AudioFocusRequest &request) override
        {
            CALL_PY(request);
        }

        void onByeByeRequest(
            const aap_protobuf::service::control::message::ByeByeRequest &request) override
        {
            CALL_PY(request);
        }

        void onByeByeResponse(
            const aap_protobuf::service::control::message::ByeByeResponse &response) override
        {
            CALL_PY(response);
        }

        void onBatteryStatusNotification(
            const aap_protobuf::service::control::message::BatteryStatusNotification &notification) override
        {
            CALL_PY(notification);
        }

        void onNavigationFocusRequest(
            const aap_protobuf::service::control::message::NavFocusRequestNotification &request) override
        {
            CALL_PY(request);
        }

        void onVoiceSessionRequest(
            const aap_protobuf::service::control::message::VoiceSessionNotification &request) override
        {
            CALL_PY(request);
        }

        void onPingRequest(
            const aap_protobuf::service::control::message::PingRequest &request) override
        {
            CALL_PY(request);
        }

        void onPingResponse(
            const aap_protobuf::service::control::message::PingResponse &response) override
        {
            CALL_PY(response);
        }

        void onChannelError(const aasdk::error::Error &e) override
        {
            CALL_PY(e.what());
        }

    private:
        std::shared_ptr<app::EventBinding> binding_;
    };

} // namespace app

inline void init_control_event_handler_binding(pybind11::module_ &m)
{
    using Channel = aasdk::channel::control::ControlServiceChannel;

    pybind11::class_<Channel, std::shared_ptr<Channel>>(m, "ControlChannel")
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               aasdk::messenger::IMessenger::Pointer messenger)
                            { return std::make_shared<Channel>(*strand, std::move(messenger)); }),
             pybind11::arg("strand"),
             pybind11::arg("messenger"))
        .def("get_id",
             [](Channel &ch)
             {
                 return static_cast<int>(ch.getId());
             })
        .def("receive",
             [](Channel &ch, std::shared_ptr<app::ControlEventHandler> handler)
             {
                 ch.receive(handler);
             })
        .def("send_version_request", [](Channel &ch, boost::asio::io_service::strand &strand, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendVersionRequest(std::move(p)); }, then_cb); }, pybind11::arg("strand"), pybind11::arg("then_cb") = pybind11::none())
        .def("send_handshake", [](Channel &ch, pybind11::bytes data, boost::asio::io_service::strand &strand, pybind11::object then_cb)
             {
                 std::string s = data;
                 aasdk::common::Data buf(s.begin(), s.end());
                 with_promise(strand, [&](auto p)
                              { ch.sendHandshake(std::move(buf), std::move(p)); }, then_cb); }, pybind11::arg("data"), pybind11::arg("strand"), pybind11::arg("then_cb") = pybind11::none())
        .def("send_auth_complete", [](Channel &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendAuthComplete(require_typed<aap_protobuf::service::control::message::AuthResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("then_cb") = pybind11::none())
        .def("send_service_discovery_response", [](Channel &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendServiceDiscoveryResponse(require_typed<aap_protobuf::service::control::message::ServiceDiscoveryResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("then_cb") = pybind11::none())
        .def("send_audio_focus_response", [](Channel &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendAudioFocusResponse(require_typed<aap_protobuf::service::control::message::AudioFocusNotification>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("then_cb") = pybind11::none())
        .def("send_shutdown_request", [](Channel &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendShutdownRequest(require_typed<aap_protobuf::service::control::message::ByeByeRequest>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("then_cb") = pybind11::none())
        .def("send_shutdown_response", [](Channel &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendShutdownResponse(require_typed<aap_protobuf::service::control::message::ByeByeResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("then_cb") = pybind11::none())
        .def("send_navigation_focus_response", [](Channel &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendNavigationFocusResponse(require_typed<aap_protobuf::service::control::message::NavFocusNotification>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("then_cb") = pybind11::none())
        .def("send_voice_session_focus_response", [](Channel &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendVoiceSessionFocusResponse(require_typed<aap_protobuf::service::control::message::VoiceSessionNotification>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("then_cb") = pybind11::none())
        .def("send_ping_request", [](Channel &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendPingRequest(require_typed<aap_protobuf::service::control::message::PingRequest>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("then_cb") = pybind11::none())
        .def("send_ping_response", [](Channel &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendPingResponse(require_typed<aap_protobuf::service::control::message::PingResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("then_cb") = pybind11::none());

    pybind11::class_<app::ControlEventHandler, std::shared_ptr<app::ControlEventHandler>>(m, "ControlEventHandler")
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               std::shared_ptr<Channel> channel,
                               std::shared_ptr<app::EventBinding> binding)
                            { return std::make_shared<app::ControlEventHandler>(*strand, std::move(channel), std::move(binding)); }),
             pybind11::arg("strand"),
             pybind11::arg("channel"),
             pybind11::arg("binding"),
             pybind11::keep_alive<1, 2>(),
             pybind11::keep_alive<1, 3>())
        .def_property_readonly("channel", &app::ControlEventHandler::channel)
        .def_property_readonly("strand", &app::ControlEventHandler::strand);
}
