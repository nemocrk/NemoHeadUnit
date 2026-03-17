#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/functional.h>

#include <libusb-1.0/libusb.h>

#include <aasdk/USB/USBWrapper.hpp>
#include <aasdk/USB/AccessoryModeQueryFactory.hpp>
#include <aasdk/USB/AccessoryModeQueryChainFactory.hpp>
#include <aasdk/USB/USBHub.hpp>
#include <aasdk/USB/AOAPDevice.hpp>
#include <aasdk/USB/IUSBHub.hpp>
#include <aasdk/Error/Error.hpp>

namespace py = pybind11;

#include "external_bindings/usb_context.hpp"

namespace app
{
    struct DeviceHandleHolder
    {
        aasdk::usb::DeviceHandle handle;
    };

    inline std::shared_ptr<py::function> make_gil_safe_function(py::function fn)
    {
        return std::shared_ptr<py::function>(
            new py::function(std::move(fn)),
            [](py::function *p)
            {
                py::gil_scoped_acquire acquire;
                delete p;
            });
    }
}

inline void init_aasdk_usb(py::module_ &m)
{
    using UsbWrapper = aasdk::usb::USBWrapper;
    using QueryFactory = aasdk::usb::AccessoryModeQueryFactory;
    using QueryChainFactory = aasdk::usb::AccessoryModeQueryChainFactory;
    using UsbHub = aasdk::usb::USBHub;
    using DeviceHandle = aasdk::usb::DeviceHandle;
    using AOAPDevice = aasdk::usb::AOAPDevice;

    py::class_<app::LibusbContext, std::shared_ptr<app::LibusbContext>>(m, "LibusbContext")
        .def(py::init([](std::uintptr_t io_ctx_ptr)
                      {
                          auto *io_ctx = reinterpret_cast<boost::asio::io_context *>(io_ctx_ptr);
                          return std::make_shared<app::LibusbContext>(*io_ctx); }),
             py::arg("io_context_ptr"))
        .def("initialize", &app::LibusbContext::initialize)
        .def("stop", &app::LibusbContext::stop)
        .def("set_poll_interval_ms", &app::LibusbContext::set_poll_interval_ms)
        .def("is_initialized", &app::LibusbContext::is_initialized)
        .def("get_context_ptr", &app::LibusbContext::get_context_ptr);

    py::class_<app::DeviceHandleHolder, std::shared_ptr<app::DeviceHandleHolder>>(m, "DeviceHandle");

    py::class_<UsbWrapper, std::shared_ptr<UsbWrapper>>(m, "USBWrapper")
        .def(py::init([](std::uintptr_t ctx_ptr)
                      {
                          auto *ctx = reinterpret_cast<libusb_context *>(ctx_ptr);
                          return std::make_shared<UsbWrapper>(ctx); }),
             py::arg("libusb_context_ptr"));

    py::class_<QueryFactory, std::shared_ptr<QueryFactory>>(m, "AccessoryModeQueryFactory")
        .def(py::init([](UsbWrapper &wrapper, std::uintptr_t io_ctx_ptr)
                      {
                          auto *io_ctx = reinterpret_cast<boost::asio::io_service *>(io_ctx_ptr);
                          return std::make_shared<QueryFactory>(wrapper, *io_ctx); }),
             py::arg("usb_wrapper"), py::arg("io_context_ptr"));

    py::class_<QueryChainFactory, std::shared_ptr<QueryChainFactory>>(m, "AccessoryModeQueryChainFactory")
        .def(py::init([](UsbWrapper &wrapper, std::uintptr_t io_ctx_ptr, QueryFactory &factory)
                      {
                          auto *io_ctx = reinterpret_cast<boost::asio::io_service *>(io_ctx_ptr);
                          return std::make_shared<QueryChainFactory>(wrapper, *io_ctx, factory); }),
             py::arg("usb_wrapper"), py::arg("io_context_ptr"), py::arg("query_factory"));

    py::class_<UsbHub, std::shared_ptr<UsbHub>>(m, "USBHub")
        .def(py::init([](UsbWrapper &wrapper, std::uintptr_t io_ctx_ptr, QueryChainFactory &chain_factory)
                      {
                          auto *io_ctx = reinterpret_cast<boost::asio::io_service *>(io_ctx_ptr);
                          return std::make_shared<UsbHub>(wrapper, *io_ctx, chain_factory); }),
             py::arg("usb_wrapper"), py::arg("io_context_ptr"), py::arg("query_chain_factory"))
        .def("start", [](UsbHub &hub, std::uintptr_t io_ctx_ptr, py::function on_ok, py::function on_err)
             {
                 auto on_ok_cb = app::make_gil_safe_function(std::move(on_ok));
                 auto on_err_cb = app::make_gil_safe_function(std::move(on_err));
                 auto *io_ctx = reinterpret_cast<boost::asio::io_service *>(io_ctx_ptr);
                 auto promise = aasdk::usb::IUSBHub::Promise::defer(*io_ctx);
                 promise->then(
                     [on_ok_cb](DeviceHandle handle)
                     {
                         py::gil_scoped_acquire acquire;
                         auto holder = std::make_shared<app::DeviceHandleHolder>();
                         holder->handle = std::move(handle);
                         (*on_ok_cb)(holder);
                     },
                     [on_err_cb](const aasdk::error::Error &e)
                     {
                         py::gil_scoped_acquire acquire;
                         (*on_err_cb)(e.what());
                     });
                 hub.start(std::move(promise)); }, py::arg("io_context_ptr"), py::arg("on_ok"), py::arg("on_err"))
        .def("cancel", &UsbHub::cancel);

    py::class_<aasdk::usb::IAOAPDevice, std::shared_ptr<aasdk::usb::IAOAPDevice>>(m, "IAOAPDevice");

    py::class_<AOAPDevice, aasdk::usb::IAOAPDevice, std::shared_ptr<AOAPDevice>>(m, "AOAPDevice")
        .def_static(
            "create",
            [](UsbWrapper &wrapper, std::uintptr_t io_ctx_ptr, const app::DeviceHandleHolder &holder)
            {
                auto *io_ctx = reinterpret_cast<boost::asio::io_service *>(io_ctx_ptr);
                return AOAPDevice::create(wrapper, *io_ctx, holder.handle);
            },
            py::arg("usb_wrapper"),
            py::arg("io_context_ptr"),
            py::arg("device_handle"));
}
