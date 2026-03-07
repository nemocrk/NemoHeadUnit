#pragma once

#include <libusb-1.0/libusb.h>
#include <boost/asio.hpp>
#include <memory>
#include <iostream>

namespace nemo {

    class LibusbContext {
    public:
        LibusbContext(boost::asio::io_context& io_context);
        ~LibusbContext();

        bool initialize();
        libusb_context* get_context() const { return usb_context_; }

    private:
        void do_poll();

        boost::asio::io_context& io_context_;
        libusb_context* usb_context_;
        boost::asio::steady_timer timer_;
    };

}