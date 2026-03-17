#pragma once

#include <pybind11/pybind11.h>

#include <boost/asio/io_service.hpp>
#include <aasdk/Messenger/Cryptor.hpp>
#include <aasdk/Messenger/IMessageInStream.hpp>
#include <aasdk/Messenger/IMessageOutStream.hpp>
#include <aasdk/Messenger/MessageInStream.hpp>
#include <aasdk/Messenger/MessageOutStream.hpp>
#include <aasdk/Messenger/Messenger.hpp>
#include <aasdk/Messenger/IMessenger.hpp>
#include <aasdk/Transport/ITransport.hpp>
#include <aasdk/Transport/SSLWrapper.hpp>

namespace py = pybind11;

inline void init_messenger(py::module_ &m)
{
    using Cryptor = aasdk::messenger::Cryptor;
    using MessageInStream = aasdk::messenger::MessageInStream;
    using MessageOutStream = aasdk::messenger::MessageOutStream;
    using IMessageInStream = aasdk::messenger::IMessageInStream;
    using IMessageOutStream = aasdk::messenger::IMessageOutStream;
    using IMessenger = aasdk::messenger::IMessenger;
    using Messenger = aasdk::messenger::Messenger;
    using SSLWrapper = aasdk::transport::SSLWrapper;
    using ITransport = aasdk::transport::ITransport;

    py::class_<Cryptor, std::shared_ptr<Cryptor>>(m, "Cryptor")
        .def(py::init([](SSLWrapper::Pointer ssl_wrapper)
                      {
                          return std::make_shared<Cryptor>(std::move(ssl_wrapper));
                      }),
             py::arg("ssl_wrapper"))
        .def("init", &Cryptor::init)
        .def("deinit", &Cryptor::deinit)
        .def("is_active", &Cryptor::isActive)
        .def("do_handshake", &Cryptor::doHandshake)
        .def("write_handshake_buffer",
             [](Cryptor &self, py::bytes data)
             {
                 std::string str = data;
                 aasdk::common::DataConstBuffer buf(
                     reinterpret_cast<const uint8_t *>(str.data()), str.size());
                 self.writeHandshakeBuffer(buf);
             })
        .def("read_handshake_buffer",
             [](Cryptor &self)
             {
                 auto buf = self.readHandshakeBuffer();
                 return py::bytes(
                     reinterpret_cast<const char *>(buf.data()), buf.size());
             });

    py::class_<IMessageInStream, std::shared_ptr<IMessageInStream>>(m, "IMessageInStream");
    py::class_<IMessageOutStream, std::shared_ptr<IMessageOutStream>>(m, "IMessageOutStream");

    py::class_<MessageInStream, IMessageInStream, std::shared_ptr<MessageInStream>>(m, "MessageInStream")
        .def(py::init([](std::uintptr_t io_ctx_ptr,
                         ITransport::Pointer transport,
                         std::shared_ptr<Cryptor> cryptor)
                      {
                          auto *io_ctx = reinterpret_cast<boost::asio::io_service *>(io_ctx_ptr);
                          return std::make_shared<MessageInStream>(*io_ctx, std::move(transport), std::move(cryptor));
                      }),
             py::arg("io_context_ptr"),
             py::arg("transport"),
             py::arg("cryptor"));

    py::class_<MessageOutStream, IMessageOutStream, std::shared_ptr<MessageOutStream>>(m, "MessageOutStream")
        .def(py::init([](std::uintptr_t io_ctx_ptr,
                         ITransport::Pointer transport,
                         std::shared_ptr<Cryptor> cryptor)
                      {
                          auto *io_ctx = reinterpret_cast<boost::asio::io_service *>(io_ctx_ptr);
                          return std::make_shared<MessageOutStream>(*io_ctx, std::move(transport), std::move(cryptor));
                      }),
             py::arg("io_context_ptr"),
             py::arg("transport"),
             py::arg("cryptor"));

    py::class_<IMessenger, std::shared_ptr<IMessenger>>(m, "IMessenger");

    py::class_<Messenger, IMessenger, std::shared_ptr<Messenger>>(m, "Messenger")
        .def(py::init([](std::uintptr_t io_ctx_ptr,
                         IMessageInStream::Pointer in_stream,
                         IMessageOutStream::Pointer out_stream)
                      {
                          auto *io_ctx = reinterpret_cast<boost::asio::io_service *>(io_ctx_ptr);
                          return std::make_shared<Messenger>(*io_ctx, std::move(in_stream), std::move(out_stream));
                      }),
             py::arg("io_context_ptr"),
             py::arg("message_in"),
             py::arg("message_out"))
        .def("stop", &Messenger::stop);
}
