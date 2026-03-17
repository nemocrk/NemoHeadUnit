#pragma once

#include <stdexcept>
#include <cstdint>
#include <pybind11/pybind11.h>

#include <aasdk/Channel/Promise.hpp>
#include <aasdk/Channel/NavigationStatus/INavigationStatusServiceEventHandler.hpp>
#include <aasdk/Channel/NavigationStatus/NavigationStatusService.hpp>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>

#include "handlers/common/event_binding.hpp"
#include "handlers/common/event_handler_utils.hpp"

namespace app
{

    // Strict pass-through: no parsing, no policy, no send. Python does everything.
    class NavigationEventHandler
        : public aasdk::channel::navigationstatus::INavigationStatusServiceEventHandler,
          public EventHandlerBase<aasdk::channel::navigationstatus::NavigationStatusService::Pointer>,
          public std::enable_shared_from_this<NavigationEventHandler>
    {
    public:
        NavigationEventHandler(boost::asio::io_service::strand &strand,
                               aasdk::channel::navigationstatus::NavigationStatusService::Pointer channel,
                               std::shared_ptr<app::EventBinding> binding)
            : EventHandlerBase(strand, std::move(channel)),
              binding_(std::move(binding))
        {
            if (!binding_)
            {
                throw std::runtime_error("NavigationEventHandler: null binding");
            }
        }

        using EventHandlerBase::channel;
        using EventHandlerBase::strand;

        void onChannelOpenRequest(
            const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
        {
            CALL_PY(request);
        }

        void onStatusUpdate(
            const aap_protobuf::service::navigationstatus::message::NavigationStatus &navStatus) override
        {
            CALL_PY(navStatus);
        }

        void onTurnEvent(
            const aap_protobuf::service::navigationstatus::message::NavigationNextTurnEvent &turnEvent) override
        {
            CALL_PY(turnEvent);
        }

        void onDistanceEvent(
            const aap_protobuf::service::navigationstatus::message::NavigationNextTurnDistanceEvent &distanceEvent) override
        {
            CALL_PY(distanceEvent);
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
inline void init_navigation_event_handler_binding(pybind11::module_ &m)
{

    pybind11::class_<aasdk::channel::navigationstatus::NavigationStatusService,
                     std::shared_ptr<aasdk::channel::navigationstatus::NavigationStatusService>>(m, "NavigationChannel", pybind11::module_local())
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               aasdk::messenger::IMessenger::Pointer messenger)
                            { return std::make_shared<aasdk::channel::navigationstatus::NavigationStatusService>(
                                  *strand, std::move(messenger)); }),
             pybind11::arg("strand"),
             pybind11::arg("messenger"))
        .def("receive",
             [](aasdk::channel::navigationstatus::NavigationStatusService &ch,
                std::shared_ptr<app::NavigationEventHandler> handler)
             {
                 ch.receive(handler);
             })
        .def("send_channel_open_response", [](aasdk::channel::navigationstatus::NavigationStatusService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendChannelOpenResponse(require_typed<aap_protobuf::service::control::message::ChannelOpenResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none());

    pybind11::class_<app::NavigationEventHandler, std::shared_ptr<app::NavigationEventHandler>>(m, "NavigationEventHandler")
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               std::shared_ptr<aasdk::channel::navigationstatus::NavigationStatusService> channel,
                               std::shared_ptr<app::EventBinding> binding)
                            { return std::make_shared<app::NavigationEventHandler>(*strand, std::move(channel), std::move(binding)); }),
             pybind11::arg("strand"),
             pybind11::arg("channel"),
             pybind11::arg("binding"),
             pybind11::keep_alive<1, 2>(),
             pybind11::keep_alive<1, 3>())
        .def_property_readonly("channel", &app::NavigationEventHandler::channel)
        .def_property_readonly("strand", &app::NavigationEventHandler::strand);
}
