#pragma once

#include <libusb-1.0/libusb.h>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <chrono>
#include <cstdint>
#include <memory>

namespace app
{
    class LibusbContext : public std::enable_shared_from_this<LibusbContext>
    {
    public:
        explicit LibusbContext(boost::asio::io_context &io_context)
            : io_context_(io_context),
              timer_(io_context)
        {
        }

        ~LibusbContext()
        {
            stop();
        }

        bool initialize()
        {
            if (ctx_)
                return true;
            if (libusb_init(&ctx_) != LIBUSB_SUCCESS)
            {
                return false;
            }
            do_poll();
            return true;
        }

        void stop()
        {
            timer_.cancel();
            if (ctx_)
            {
                libusb_exit(ctx_);
                ctx_ = nullptr;
            }
        }

        void set_poll_interval_ms(int interval_ms)
        {
            if (interval_ms < 0)
                interval_ms = 0;
            poll_interval_ = std::chrono::milliseconds(interval_ms);
        }

        bool is_initialized() const { return ctx_ != nullptr; }
        std::uintptr_t get_context_ptr() const { return reinterpret_cast<std::uintptr_t>(ctx_); }

    private:
        void do_poll()
        {
            auto self = shared_from_this();
            timer_.expires_after(poll_interval_);
            timer_.async_wait([self](const boost::system::error_code &error)
                              {
                                  if (error || !self->ctx_) return;
                                  struct timeval tv = {0, 0};
                                  libusb_handle_events_timeout(self->ctx_, &tv);
                                  self->do_poll(); });
        }

        boost::asio::io_context &io_context_;
        boost::asio::steady_timer timer_;
        std::chrono::milliseconds poll_interval_{10};
        libusb_context *ctx_ = nullptr;
    };
}
