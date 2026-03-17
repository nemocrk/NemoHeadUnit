#pragma once

#include <boost/asio.hpp>
#include <thread>
#include <iostream>
#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace nemo
{
    class IoContextRunner
    {
    public:
        IoContextRunner() : work_guard_(boost::asio::make_work_guard(io_context_)) {}

        ~IoContextRunner() { stop(); }

        void start()
        {
            if (!thread_.joinable())
            {
                std::cout << "[APP] Avvio Boost.Asio event loop (Worker Thread C++)..." << std::endl;
                running_ = true;
                thread_ = std::thread([this]()
                                      { io_context_.run(); });
            }
        }

        void stop()
        {
            work_guard_.reset();
            io_context_.stop();
            if (thread_.joinable())
            {
                std::cout << "[APP] Arresto Boost.Asio event loop..." << std::endl;
                thread_.join();
            }
            running_ = false;
        }

        boost::asio::io_context &get_io_context() { return io_context_; }
        std::uintptr_t get_io_context_ptr() { return reinterpret_cast<std::uintptr_t>(&io_context_); }
        bool is_running() const { return running_; }

    private:
        boost::asio::io_context io_context_;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
        std::thread thread_;
        bool running_ = false;
    };
}
// ── IoContextRunner ──────────────────────────────────────────────────

void init_context_runner(py::module_ &m)
{
    py::class_<nemo::IoContextRunner, std::shared_ptr<nemo::IoContextRunner>>(m, "IoContextRunner")
        .def(py::init<>())
        .def("start", &nemo::IoContextRunner::start, py::call_guard<py::gil_scoped_release>())
        .def("stop", &nemo::IoContextRunner::stop, py::call_guard<py::gil_scoped_release>())
        .def("is_running", &nemo::IoContextRunner::is_running)
        .def("get_io_context_ptr", &nemo::IoContextRunner::get_io_context_ptr);
}
