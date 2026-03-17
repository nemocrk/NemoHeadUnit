#pragma once

#include <stdexcept>
#include <cstdint>
#include <pybind11/pybind11.h>

#include <aasdk/Channel/Promise.hpp>
#include <aasdk/Channel/WifiProjection/IWifiProjectionServiceEventHandler.hpp>
#include <aasdk/Channel/WifiProjection/WifiProjectionService.hpp>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/wifiprojection/message/WifiCredentialsResponse.pb.h>

#include "handlers/common/event_binding.hpp"
#include "handlers/common/event_handler_utils.hpp"

namespace app
{

    // Strict pass-through: no parsing, no policy, no send. Python does everything.
    class WifiProjectionEventHandler
        : public aasdk::channel::wifiprojection::IWifiProjectionServiceEventHandler,
          public EventHandlerBase<aasdk::channel::wifiprojection::WifiProjectionService::Pointer>,
          public std::enable_shared_from_this<WifiProjectionEventHandler>
    {
    public:
        WifiProjectionEventHandler(boost::asio::io_service::strand &strand,
                                   aasdk::channel::wifiprojection::WifiProjectionService::Pointer channel,
                                   std::shared_ptr<app::EventBinding> binding)
            : EventHandlerBase(strand, std::move(channel)),
              binding_(std::move(binding))
        {
            if (!binding_)
            {
                throw std::runtime_error("WifiProjectionEventHandler: null binding");
            }
        }

        using EventHandlerBase::channel;
        using EventHandlerBase::strand;

        void onWifiCredentialsRequest(
            const aap_protobuf::service::wifiprojection::message::WifiCredentialsRequest &request) override
        {
            CALL_PY(request);
        }

        void onChannelOpenRequest(
            const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
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
inline void init_wifi_projection_event_handler_binding(pybind11::module_ &m)
{

    pybind11::class_<aasdk::channel::wifiprojection::WifiProjectionService,
                     std::shared_ptr<aasdk::channel::wifiprojection::WifiProjectionService>>(m, "WifiProjectionChannel", pybind11::module_local())
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               aasdk::messenger::IMessenger::Pointer messenger)
                            { return std::make_shared<aasdk::channel::wifiprojection::WifiProjectionService>(
                                  *strand, std::move(messenger)); }),
             pybind11::arg("strand"),
             pybind11::arg("messenger"))
        .def("receive",
             [](aasdk::channel::wifiprojection::WifiProjectionService &ch,
                std::shared_ptr<app::WifiProjectionEventHandler> handler)
             {
                 ch.receive(handler);
             })
        .def("send_channel_open_response", [](aasdk::channel::wifiprojection::WifiProjectionService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendChannelOpenResponse(require_typed<aap_protobuf::service::control::message::ChannelOpenResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_wifi_credentials_response", [](aasdk::channel::wifiprojection::WifiProjectionService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendWifiCredentialsResponse(require_typed<aap_protobuf::service::wifiprojection::message::WifiCredentialsResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none());

    pybind11::class_<app::WifiProjectionEventHandler, std::shared_ptr<app::WifiProjectionEventHandler>>(m, "WifiProjectionEventHandler")
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               std::shared_ptr<aasdk::channel::wifiprojection::WifiProjectionService> channel,
                               std::shared_ptr<app::EventBinding> binding)
                            { return std::make_shared<app::WifiProjectionEventHandler>(*strand, std::move(channel), std::move(binding)); }),
             pybind11::arg("strand"),
             pybind11::arg("channel"),
             pybind11::arg("binding"),
             pybind11::keep_alive<1, 2>(),
             pybind11::keep_alive<1, 3>())
        .def_property_readonly("channel", &app::WifiProjectionEventHandler::channel)
        .def_property_readonly("strand", &app::WifiProjectionEventHandler::strand);
}
