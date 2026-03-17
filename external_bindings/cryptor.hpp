#pragma once

#include <pybind11/pybind11.h>
#include <aasdk/Messenger/ICryptor.hpp>
#include <aasdk/Common/Data.hpp>

namespace py = pybind11;

inline void init_cryptor(py::module_ &m)
{
    py::class_<aasdk::messenger::ICryptor, std::shared_ptr<aasdk::messenger::ICryptor>>(m, "ICryptor")
        .def("do_handshake", &aasdk::messenger::ICryptor::doHandshake)
        .def("write_handshake_buffer",
             [](aasdk::messenger::ICryptor &self, py::bytes data)
             {
                 std::string str = data;
                 aasdk::common::DataConstBuffer buf(
                     reinterpret_cast<const uint8_t *>(str.data()), str.size());
                 self.writeHandshakeBuffer(buf);
             })
        .def("read_handshake_buffer",
             [](aasdk::messenger::ICryptor &self)
             {
                 auto buf = self.readHandshakeBuffer();
                 return py::bytes(
                     reinterpret_cast<const char *>(buf.data()), buf.size());
             });
}
