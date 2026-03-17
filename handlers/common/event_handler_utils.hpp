#pragma once

#include <string>
#include <stdexcept>
#include <iostream>
#include <memory>
#include <utility>

#include <pybind11/pybind11.h>

#include <aasdk/Channel/Promise.hpp>
#include <boost/asio.hpp>

#include "handlers/common/protobuf_message.hpp"

// Generic helper for C++ → Python pass-through.
#define CALL_PY(msg) (binding_)->call(this, __func__, msg)

// Generic holder for strand + channel (no channel_id).
template <typename ChannelPtr>
class EventHandlerBase
{
public:
    EventHandlerBase(boost::asio::io_service::strand &strand, ChannelPtr channel)
        : strand_(strand),
          channel_(std::move(channel))
    {
    }

    ChannelPtr channel() { return channel_; }
    boost::asio::io_service::strand &strand() { return strand_; }

protected:
    boost::asio::io_service::strand &strand_;
    ChannelPtr channel_;
};

// Generic protobuf type enforcement for bindings.
template <typename T>
inline const T &require_typed(const app::ProtobufMessage &msg)
{
    const auto &full_name = msg.type_name();
    const std::string &expected = T::descriptor()->full_name();
    if (full_name != expected)
    {
        throw std::runtime_error(std::string("Expected ") + expected + ", got: " + full_name);
    }
    auto *typed = dynamic_cast<const T *>(&msg.get());
    if (!typed)
    {
        throw std::runtime_error(std::string("Dynamic cast failed for ") + expected);
    }
    return *typed;
}

// Create a SendPromise bound to strand and execute fn(promise).
// Optionally attach a Python callback to promise->then(success, error).
template <typename Fn>
inline void with_promise(boost::asio::io_service::strand &strand,
                         Fn &&fn,
                         const pybind11::object &py_then)
{
    auto p = aasdk::channel::SendPromise::defer(strand);
    if (!py_then.is_none())
    {
        auto cb = std::shared_ptr<pybind11::object>(
            new pybind11::object(py_then),
            [](pybind11::object *obj)
            {
                pybind11::gil_scoped_acquire gil;
                delete obj;
            });
        p->then(
            [cb]()
            {
                pybind11::gil_scoped_acquire gil;
                try
                {
                    (*cb)(pybind11::none());
                }
                catch (const pybind11::error_already_set &e)
                {
                    std::cerr << "[APP] promise then callback error: " << e.what() << std::endl;
                }
            },
            [cb](const aasdk::error::Error &e)
            {
                pybind11::gil_scoped_acquire gil;
                try
                {
                    (*cb)(pybind11::str(e.what()));
                }
                catch (const pybind11::error_already_set &err)
                {
                    std::cerr << "[APP] promise then callback error: " << err.what() << std::endl;
                }
            });
    }
    fn(std::move(p));
}

// Overload: pure C++ usage (no Python callback, no GIL usage).
template <typename Fn>
inline void with_promise(boost::asio::io_service::strand &strand,
                         Fn &&fn)
{
    auto p = aasdk::channel::SendPromise::defer(strand);
    fn(std::move(p));
}
