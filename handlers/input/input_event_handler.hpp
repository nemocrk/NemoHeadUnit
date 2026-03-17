#pragma once

#include <stdexcept>
#include <cstdint>
#include <pybind11/pybind11.h>

#include <aasdk/Channel/Promise.hpp>
#include <aasdk/Channel/InputSource/IInputSourceServiceEventHandler.hpp>
#include <aasdk/Channel/InputSource/InputSourceService.hpp>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/media/sink/message/KeyBindingResponse.pb.h>
#include <aap_protobuf/service/inputsource/message/InputReport.pb.h>

#include "handlers/common/event_binding.hpp"
#include "handlers/common/event_handler_utils.hpp"

namespace app
{

    // Strict pass-through: no parsing, no policy, no send. Python does everything.
    class InputEventHandler
        : public aasdk::channel::inputsource::IInputSourceServiceEventHandler,
          public EventHandlerBase<aasdk::channel::inputsource::InputSourceService::Pointer>,
          public std::enable_shared_from_this<InputEventHandler>
    {
    public:
        InputEventHandler(boost::asio::io_service::strand &strand,
                          aasdk::channel::inputsource::InputSourceService::Pointer channel,
                          std::shared_ptr<app::EventBinding> binding)
            : EventHandlerBase(strand, std::move(channel)),
              binding_(std::move(binding))
        {
            if (!binding_)
            {
                throw std::runtime_error("InputEventHandler: null binding");
            }
        }

        using EventHandlerBase::channel;
        using EventHandlerBase::strand;

        void onChannelOpenRequest(
            const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
        {
            CALL_PY(request);
        }

        void onKeyBindingRequest(
            const aap_protobuf::service::media::sink::message::KeyBindingRequest &request) override
        {
            CALL_PY(request);
        }

        void onChannelError(const aasdk::error::Error &e) override
        {
            CALL_PY(e.what());
        }

    private:
        std::shared_ptr<app::EventBinding> binding_;
    };

} // namespace app

// Minimal binding defined alongside thehandler (opt-in by including this header).
inline void init_input_event_handler_binding(pybind11::module_ &m)
{

    pybind11::class_<aasdk::channel::inputsource::InputSourceService,
                     std::shared_ptr<aasdk::channel::inputsource::InputSourceService>>(m, "InputChannel", pybind11::module_local())
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               aasdk::messenger::IMessenger::Pointer messenger)
                            { return std::make_shared<aasdk::channel::inputsource::InputSourceService>(
                                  *strand, std::move(messenger)); }),
             pybind11::arg("strand"),
             pybind11::arg("messenger"))
        .def("receive",
             [](aasdk::channel::inputsource::InputSourceService &ch,
                std::shared_ptr<app::InputEventHandler> handler)
             {
                 ch.receive(handler);
             })
        .def("send_channel_open_response", [](aasdk::channel::inputsource::InputSourceService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendChannelOpenResponse(require_typed<aap_protobuf::service::control::message::ChannelOpenResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_input_report", [](aasdk::channel::inputsource::InputSourceService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendInputReport(require_typed<aap_protobuf::service::inputsource::message::InputReport>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_key_binding_response", [](aasdk::channel::inputsource::InputSourceService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendKeyBindingResponse(require_typed<aap_protobuf::service::media::sink::message::KeyBindingResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none());

    pybind11::class_<app::InputEventHandler, std::shared_ptr<app::InputEventHandler>>(m, "InputEventHandler")
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               std::shared_ptr<aasdk::channel::inputsource::InputSourceService> channel,
                               std::shared_ptr<app::EventBinding> binding)
                            { return std::make_shared<app::InputEventHandler>(*strand, std::move(channel), std::move(binding)); }),
             pybind11::arg("strand"),
             pybind11::arg("channel"),
             pybind11::arg("binding"),
             pybind11::keep_alive<1, 2>(),
             pybind11::keep_alive<1, 3>())
        .def_property_readonly("channel", &app::InputEventHandler::channel)
        .def_property_readonly("strand", &app::InputEventHandler::strand);
}
