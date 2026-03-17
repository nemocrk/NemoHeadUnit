#pragma once

#include <string>
#include <stdexcept>
#include <type_traits>
#include <pybind11/pybind11.h>
#include <google/protobuf/message_lite.h>
#include <boost/asio.hpp>

namespace app
{

    // Generalized Python binding wrapper for event handlers.
    class EventBinding
    {
    public:
        explicit EventBinding(pybind11::object py_impl)
            : py_impl_(std::move(py_impl))
        {
            if (py_impl_.is_none())
            {
                throw std::runtime_error("EventBinding: null python object");
            }
        }

        template <typename THandler, typename TData>
        void call(THandler *self, const char *method, const TData &data)
        {
            pybind11::gil_scoped_acquire acquire;
            const std::string py_method = camel_to_snake(method);

            if (!pybind11::hasattr(py_impl_, py_method.c_str()))
            {
                throw std::runtime_error("Missing python method: " + py_method);
            }

            // Gestione unificata del payload:
            // Se TData è MessageLite, serializza. Se è già stringa, usa direttamente.
            std::string payload;
            if constexpr (std::is_base_of_v<google::protobuf::MessageLite, TData>)
            {
                data.SerializeToString(&payload);
            }
            else
            {
                payload = data; // Assume TData sia std::string o convertibile
            }

            // Esecuzione chiamata Python
            py_impl_.attr(py_method.c_str())(pybind11::cast(self), pybind11::bytes(payload));
        }

    private:
        static std::string camel_to_snake(const std::string &in)
        {
            std::string out;
            out.reserve(in.size() + 8);
            for (size_t i = 0; i < in.size(); ++i)
            {
                const unsigned char c = static_cast<unsigned char>(in[i]);
                if (c >= 'A' && c <= 'Z')
                {
                    if (i != 0)
                        out.push_back('_');
                    out.push_back(static_cast<char>(c + 32));
                }
                else
                {
                    out.push_back(static_cast<char>(c));
                }
            }
            return out;
        }

        pybind11::object py_impl_;
    };

    inline void init_event_binding(pybind11::module_ &m)
    {
        pybind11::class_<EventBinding, std::shared_ptr<EventBinding>>(m, "EventBinding")
            .def(pybind11::init<pybind11::object>());

        pybind11::class_<boost::asio::io_service::strand, std::shared_ptr<boost::asio::io_service::strand>>(m, "Strand")
            .def(pybind11::init([](std::uintptr_t io_ctx_ptr)
                                {
                                    auto *io_ctx = reinterpret_cast<boost::asio::io_service *>(io_ctx_ptr);
                                    return std::make_shared<boost::asio::io_service::strand>(*io_ctx); }),
                 pybind11::arg("io_context_ptr"))
            .def("dispatch",
                 [](boost::asio::io_service::strand &s, pybind11::function fn)
                 {
                     s.dispatch([fn = std::move(fn)]() mutable
                                {
                     pybind11::gil_scoped_acquire acquire;
                     fn(); });
                 });
    }

} // namespace app

// Global wrapper for CMake-generated module entrypoints.
inline void init_event_binding(pybind11::module_ &m)
{
    app::init_event_binding(m);
}
