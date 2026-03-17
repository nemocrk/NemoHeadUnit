#pragma once

#include <stdexcept>
#include <cstdint>
#include <pybind11/pybind11.h>

#include <aasdk/Channel/Promise.hpp>
#include <aasdk/Channel/SensorSource/ISensorSourceServiceEventHandler.hpp>
#include <aasdk/Channel/SensorSource/SensorSourceService.hpp>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorStartResponseMessage.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorBatch.pb.h>

#include "handlers/common/event_binding.hpp"
#include "handlers/common/event_handler_utils.hpp"

namespace app
{

    // Strict pass-through: no parsing, no policy, no send. Python does everything.
    class SensorEventHandler
        : public aasdk::channel::sensorsource::ISensorSourceServiceEventHandler,
          public EventHandlerBase<aasdk::channel::sensorsource::SensorSourceService::Pointer>,
          public std::enable_shared_from_this<SensorEventHandler>
    {
    public:
        SensorEventHandler(boost::asio::io_service::strand &strand,
                           aasdk::channel::sensorsource::SensorSourceService::Pointer channel,
                           std::shared_ptr<app::EventBinding> binding)
            : EventHandlerBase(strand, std::move(channel)),
              binding_(std::move(binding))
        {
            if (!binding_)
            {
                throw std::runtime_error("SensorEventHandler: null binding");
            }
        }

        using EventHandlerBase::channel;
        using EventHandlerBase::strand;

        void onChannelOpenRequest(
            const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
        {
            CALL_PY(request);
        }

        void onSensorStartRequest(
            const aap_protobuf::service::sensorsource::message::SensorRequest &request) override
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
inline void init_sensor_event_handler_binding(pybind11::module_ &m)
{

    pybind11::class_<aasdk::channel::sensorsource::SensorSourceService,
                     std::shared_ptr<aasdk::channel::sensorsource::SensorSourceService>>(m, "SensorChannel", pybind11::module_local())
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               aasdk::messenger::IMessenger::Pointer messenger)
                            { return std::make_shared<aasdk::channel::sensorsource::SensorSourceService>(
                                  *strand, std::move(messenger)); }),
             pybind11::arg("strand"),
             pybind11::arg("messenger"))
        .def("receive",
             [](aasdk::channel::sensorsource::SensorSourceService &ch,
                std::shared_ptr<app::SensorEventHandler> handler)
             {
                 ch.receive(handler);
             })
        .def("send_channel_open_response", [](aasdk::channel::sensorsource::SensorSourceService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendChannelOpenResponse(require_typed<aap_protobuf::service::control::message::ChannelOpenResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_sensor_start_response", [](aasdk::channel::sensorsource::SensorSourceService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendSensorStartResponse(require_typed<aap_protobuf::service::sensorsource::message::SensorStartResponseMessage>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_sensor_event_indication", [](aasdk::channel::sensorsource::SensorSourceService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendSensorEventIndication(require_typed<aap_protobuf::service::sensorsource::message::SensorBatch>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none());

    pybind11::class_<app::SensorEventHandler, std::shared_ptr<app::SensorEventHandler>>(m, "SensorEventHandler")
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               std::shared_ptr<aasdk::channel::sensorsource::SensorSourceService> channel,
                               std::shared_ptr<app::EventBinding> binding)
                            { return std::make_shared<app::SensorEventHandler>(*strand, std::move(channel), std::move(binding)); }),
             pybind11::arg("strand"),
             pybind11::arg("channel"),
             pybind11::arg("binding"),
             pybind11::keep_alive<1, 2>(),
             pybind11::keep_alive<1, 3>())
        .def_property_readonly("channel", &app::SensorEventHandler::channel)
        .def_property_readonly("strand", &app::SensorEventHandler::strand);
}
