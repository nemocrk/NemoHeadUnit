#pragma once

#include <boost/asio.hpp>
#include <thread>
#include <iostream>

namespace nemo {
    class IoContextRunner {
    public:
        IoContextRunner() : work_guard_(boost::asio::make_work_guard(io_context_)) {}

        ~IoContextRunner() { stop(); }

        void start() {
            if (!thread_.joinable()) {
                std::cout << "[NemoHeadUnit] Avvio Boost.Asio event loop (Worker Thread C++)..." << std::endl;
                thread_ = std::thread([this]() { io_context_.run(); });
            }
        }

        void stop() {
            work_guard_.reset();
            io_context_.stop();
            if (thread_.joinable()) {
                std::cout << "[NemoHeadUnit] Arresto Boost.Asio event loop..." << std::endl;
                thread_.join();
            }
        }

        boost::asio::io_context& get_io_context() { return io_context_; }

    private:
        boost::asio::io_context io_context_;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
        std::thread thread_;
    };
}