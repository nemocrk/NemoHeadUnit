#pragma once

#include <pybind11/pybind11.h>
#include <iostream>
#include "session/iorchestrator.hpp"

namespace nemo {

class PyOrchestrator : public IOrchestrator {
public:
    PyOrchestrator(pybind11::object obj) : obj_(std::move(obj)) {}

    ~PyOrchestrator() override {
        if (!obj_.is_none()) {
            pybind11::gil_scoped_acquire acquire;
            obj_ = pybind11::none();
        }
    }

    std::string onServiceDiscoveryRequest(const std::string& request_bytes) override {
        return callPythonMethod("on_service_discovery_request", request_bytes);
    }

    std::string onPingRequest(const std::string& request_bytes) override {
        return callPythonMethod("on_ping_request", request_bytes);
    }

    std::string onAudioFocusRequest(const std::string& request_bytes) override {
        return callPythonMethod("on_audio_focus_request", request_bytes);
    }

    std::string onVideoChannelOpenRequest(const std::string& request_bytes) override {
        return callPythonMethod("on_video_channel_open_request", request_bytes);
    }

private:
    std::string callPythonMethod(const char* method_name, const std::string& payload) {
        pybind11::gil_scoped_acquire acquire;
        if (!obj_.is_none() && pybind11::hasattr(obj_, method_name)) {
            try {
                pybind11::object res = obj_.attr(method_name)(pybind11::bytes(payload));
                if (!res.is_none()) {
                    return std::string(pybind11::cast<pybind11::bytes>(res));
                }
            } catch (const std::exception& e) {
                std::cerr << "[PyOrchestrator] Errore esecuzione " << method_name << ": " << e.what() << std::endl;
            }
        }
        return "";
    }

    pybind11::object obj_;
};

} // namespace nemo
