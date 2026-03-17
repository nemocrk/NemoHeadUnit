#pragma once

#include <memory>
#include <string>
#include <stdexcept>

#include <pybind11/pybind11.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

namespace app
{

    class ProtobufMessage
    {
    public:
        explicit ProtobufMessage(std::unique_ptr<google::protobuf::Message> msg)
            : msg_(std::move(msg))
        {
            if (!msg_)
            {
                throw std::runtime_error("ProtobufMessage: null message");
            }
        }

        const std::string &type_name() const
        {
            return msg_->GetDescriptor()->full_name();
        }

        bool parse_from_string(const std::string &bytes)
        {
            return msg_->ParseFromString(bytes);
        }

        std::string serialize_to_string() const
        {
            std::string out;
            msg_->SerializeToString(&out);
            return out;
        }

        google::protobuf::Message &get() { return *msg_; }
        const google::protobuf::Message &get() const { return *msg_; }

    private:
        std::unique_ptr<google::protobuf::Message> msg_;
    };

} // namespace app

namespace
{

    inline std::shared_ptr<app::ProtobufMessage> get_protobuf_by_name(const std::string &type_name)
    {
        const auto *desc = google::protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(type_name);
        if (!desc)
        {
            throw std::runtime_error("Unknown protobuf type: " + type_name);
        }

        const auto *prototype = google::protobuf::MessageFactory::generated_factory()->GetPrototype(desc);
        if (!prototype)
        {
            throw std::runtime_error("No prototype for protobuf type: " + type_name);
        }

        std::unique_ptr<google::protobuf::Message> msg(prototype->New());
        return std::make_shared<app::ProtobufMessage>(std::move(msg));
    }

} // namespace

inline void init_protobuf_bindings(pybind11::module_ &m)
{
    pybind11::class_<app::ProtobufMessage, std::shared_ptr<app::ProtobufMessage>>(m, "ProtobufMessage")
        .def("type_name", &app::ProtobufMessage::type_name)
        .def("parse_from_string",
             [](app::ProtobufMessage &self, pybind11::bytes data)
             {
                 std::string s = data;
                 if (!self.parse_from_string(s))
                 {
                     throw std::runtime_error("ParseFromString failed for " + self.type_name());
                 }
             })
        .def("serialize_to_string",
             [](const app::ProtobufMessage &self)
             {
                 return pybind11::bytes(self.serialize_to_string());
             });

    m.def("GetProtobuf", &get_protobuf_by_name,
          pybind11::arg("type_name"),
          "Create a C++ protobuf message by full type name.");
}
