#pragma once

#include <stdexcept>
#include <cstdint>
#include <pybind11/pybind11.h>

#include <aasdk/Channel/Promise.hpp>
#include <aasdk/Channel/Bluetooth/IBluetoothServiceEventHandler.hpp>
#include <aasdk/Channel/Bluetooth/BluetoothService.hpp>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothPairingResponse.pb.h>
#include <aap_protobuf/service/bluetooth/message/BluetoothAuthenticationData.pb.h>

#include "handlers/common/event_binding.hpp"
#include "handlers/common/event_handler_utils.hpp"

namespace app
{

    // Strict pass-through: no parsing, no policy, no send. Python does everything.
    class BluetoothEventHandler
        : public aasdk::channel::bluetooth::IBluetoothServiceEventHandler,
          public EventHandlerBase<aasdk::channel::bluetooth::BluetoothService::Pointer>,
          public std::enable_shared_from_this<BluetoothEventHandler>
    {
    public:
        BluetoothEventHandler(boost::asio::io_service::strand &strand,
                              aasdk::channel::bluetooth::BluetoothService::Pointer channel,
                              std::shared_ptr<app::EventBinding> binding)
            : EventHandlerBase(strand, std::move(channel)),
              binding_(std::move(binding))
        {
            if (!binding_)
            {
                throw std::runtime_error("BluetoothEventHandler: null binding");
            }
        }

        using EventHandlerBase::channel;
        using EventHandlerBase::strand;

        void onChannelOpenRequest(
            const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
        {
            CALL_PY(request);
        }

        void onBluetoothPairingRequest(
            const aap_protobuf::service::bluetooth::message::BluetoothPairingRequest &request) override
        {
            CALL_PY(request);
        }

        void onBluetoothAuthenticationResult(
            const aap_protobuf::service::bluetooth::message::BluetoothAuthenticationResult &request) override
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

// Minimal binding defined alongside the handler (opt-in by including this header).
inline void init_bluetooth_event_handler_binding(pybind11::module_ &m)
{

    pybind11::class_<aasdk::channel::bluetooth::BluetoothService,
                     std::shared_ptr<aasdk::channel::bluetooth::BluetoothService>>(m, "BluetoothChannel", pybind11::module_local())
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               aasdk::messenger::IMessenger::Pointer messenger)
                            { return std::make_shared<aasdk::channel::bluetooth::BluetoothService>(
                                  *strand, std::move(messenger)); }),
             pybind11::arg("strand"),
             pybind11::arg("messenger"))
        .def("receive",
             [](aasdk::channel::bluetooth::BluetoothService &ch,
                std::shared_ptr<app::BluetoothEventHandler> handler)
             {
                 ch.receive(handler);
             })
        .def("send_channel_open_response", [](aasdk::channel::bluetooth::BluetoothService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendChannelOpenResponse(require_typed<aap_protobuf::service::control::message::ChannelOpenResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_bluetooth_pairing_response", [](aasdk::channel::bluetooth::BluetoothService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendBluetoothPairingResponse(require_typed<aap_protobuf::service::bluetooth::message::BluetoothPairingResponse>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none())
        .def("send_bluetooth_authentication_data", [](aasdk::channel::bluetooth::BluetoothService &ch, const app::ProtobufMessage &msg, boost::asio::io_service::strand &strand, const std::string &tag, pybind11::object then_cb)
             { with_promise(strand, [&](auto p)
                            { ch.sendBluetoothAuthenticationData(require_typed<aap_protobuf::service::bluetooth::message::BluetoothAuthenticationData>(msg), std::move(p)); }, then_cb); }, pybind11::arg("msg"), pybind11::arg("strand"), pybind11::arg("tag") = "", pybind11::arg("then_cb") = pybind11::none());

    pybind11::class_<app::BluetoothEventHandler, std::shared_ptr<app::BluetoothEventHandler>>(m, "BluetoothEventHandler")
        .def(pybind11::init([](std::shared_ptr<boost::asio::io_service::strand> strand,
                               std::shared_ptr<aasdk::channel::bluetooth::BluetoothService> channel,
                               std::shared_ptr<app::EventBinding> binding)
                            { return std::make_shared<app::BluetoothEventHandler>(*strand, std::move(channel), std::move(binding)); }),
             pybind11::arg("strand"),
             pybind11::arg("channel"),
             pybind11::arg("binding"),
             pybind11::keep_alive<1, 2>(),
             pybind11::keep_alive<1, 3>())
        .def_property_readonly("channel", &app::BluetoothEventHandler::channel)
        .def_property_readonly("strand", &app::BluetoothEventHandler::strand);
}
