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

    void setCryptor(std::shared_ptr<aasdk::messenger::ICryptor> cryptor) override {
        pybind11::gil_scoped_acquire acquire;
        if (!obj_.is_none() && pybind11::hasattr(obj_, "set_cryptor")) {
            obj_.attr("set_cryptor")(cryptor);
        } else {
            throw std::runtime_error("Python orchestrator missing method: set_cryptor");
        }
    }

    std::string onVersionStatus(int major, int minor, int status) override {
        pybind11::gil_scoped_acquire acquire;
        if (!obj_.is_none() && pybind11::hasattr(obj_, "on_version_status")) {
            pybind11::object res = obj_.attr("on_version_status")(major, minor, status);
            if (!res.is_none()) {
                return std::string(pybind11::cast<pybind11::bytes>(res));
            }
        }
        throw std::runtime_error("Python orchestrator missing method or returned None: on_version_status");
    }

    std::string onHandshake(const std::string& payload_bytes) override {
        return callPythonMethod("on_handshake", payload_bytes);
    }

    std::string getAuthCompleteResponse() override {
        pybind11::gil_scoped_acquire acquire;
        if (!obj_.is_none() && pybind11::hasattr(obj_, "get_auth_complete_response")) {
            pybind11::object res = obj_.attr("get_auth_complete_response")();
            if (!res.is_none()) {
                return std::string(pybind11::cast<pybind11::bytes>(res));
            }
        }
        throw std::runtime_error("Python orchestrator missing method or returned None: get_auth_complete_response");
    }

    std::string onServiceDiscoveryRequest(const std::string& request_bytes) override {
        return callPythonMethod("on_service_discovery_request", request_bytes);
    }

    // Chiamato da ControlEventHandler per ogni ChannelOpenRequest inviata dallo smartphone
    std::string onChannelOpenRequest(const std::string& request_bytes) override {
        return callPythonMethod("on_channel_open_request", request_bytes);
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
            pybind11::object res = obj_.attr(method_name)(pybind11::bytes(payload));
            if (!res.is_none()) {
                return std::string(pybind11::cast<pybind11::bytes>(res));
            }
        }
        throw std::runtime_error(std::string("Python orchestrator missing method or returned None: ") + method_name);
    }

    pybind11::object obj_;
};

} // namespace nemo
