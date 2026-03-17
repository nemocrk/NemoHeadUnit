#pragma once

#include <pybind11/pybind11.h>

#include <boost/asio/io_service.hpp>
#include <aasdk/Transport/ITransport.hpp>
#include <aasdk/Transport/ISSLWrapper.hpp>
#include <aasdk/Transport/USBTransport.hpp>
#include <aasdk/Transport/SSLWrapper.hpp>
#include <aasdk/USB/IAOAPDevice.hpp>

namespace py = pybind11;

inline void init_transport(py::module_ &m)
{
    using ITransport = aasdk::transport::ITransport;
    using ISSLWrapper = aasdk::transport::ISSLWrapper;
    using USBTransport = aasdk::transport::USBTransport;
    using SSLWrapper = aasdk::transport::SSLWrapper;

    py::class_<ITransport, std::shared_ptr<ITransport>>(m, "ITransport");

    py::class_<ISSLWrapper, std::shared_ptr<ISSLWrapper>>(m, "ISSLWrapper");

    py::class_<SSLWrapper, ISSLWrapper, std::shared_ptr<SSLWrapper>>(m, "SSLWrapper")
        .def(py::init<>());

    py::class_<USBTransport, ITransport, std::shared_ptr<USBTransport>>(m, "USBTransport")
        .def(py::init([](std::uintptr_t io_ctx_ptr, aasdk::usb::IAOAPDevice::Pointer aoap_device)
                      {
                          auto *io_ctx = reinterpret_cast<boost::asio::io_service *>(io_ctx_ptr);
                          return std::make_shared<USBTransport>(*io_ctx, std::move(aoap_device));
                      }),
             py::arg("io_context_ptr"),
             py::arg("aoap_device"))
        .def("stop", &USBTransport::stop);
}
