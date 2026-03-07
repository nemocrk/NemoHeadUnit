#include "libusb_context.hpp"

namespace nemo {

LibusbContext::LibusbContext(boost::asio::io_context& io_context) 
    : io_context_(io_context), 
      usb_context_(nullptr),
      timer_(io_context) {}

LibusbContext::~LibusbContext() {
    timer_.cancel();
    if (usb_context_) {
        libusb_exit(usb_context_);
        usb_context_ = nullptr;
    }
}

bool LibusbContext::initialize() {
    int result = libusb_init(&usb_context_);
    if (result != LIBUSB_SUCCESS) {
        std::cerr << "[LibusbContext] Errore inizializzazione libusb: " << result << std::endl;
        return false;
    }
    
    // Impostiamo il timer per chiamare libusb_handle_events_timeout periodicamente
    // così da non bloccare il loop asincrono di asio.
    do_poll();
    return true;
}

void LibusbContext::do_poll() {
    timer_.expires_after(std::chrono::milliseconds(10));
    timer_.async_wait([this](const boost::system::error_code& error) {
        if (!error) {
            struct timeval tv = {0, 0};
            libusb_handle_events_timeout(usb_context_, &tv);
            do_poll();
        }
    });
}

}