#pragma once

#include <pybind11/pybind11.h>
#include <iostream>
#include "session/iorchestrator.hpp"

namespace nemo
{

    class PyOrchestrator : public IOrchestrator
    {
    public:
        PyOrchestrator(pybind11::object obj) : obj_(std::move(obj)) {}

        ~PyOrchestrator() override
        {
            if (!obj_.is_none())
            {
                pybind11::gil_scoped_acquire acquire;
                obj_ = pybind11::none();
            }
        }

        // -----------------------------------------------------------------------
        // Cryptor
        // -----------------------------------------------------------------------

        void setCryptor(std::shared_ptr<aasdk::messenger::ICryptor> cryptor) override
        {
            pybind11::gil_scoped_acquire acquire;
            if (!obj_.is_none() && pybind11::hasattr(obj_, "set_cryptor"))
            {
                obj_.attr("set_cryptor")(cryptor);
            }
            else
            {
                throw std::runtime_error("Python orchestrator missing method: set_cryptor");
            }
        }

        // -----------------------------------------------------------------------
        // Handshake TLS
        // -----------------------------------------------------------------------

        std::string onVersionStatus(int major, int minor, int status) override
        {
            pybind11::gil_scoped_acquire acquire;
            if (!obj_.is_none() && pybind11::hasattr(obj_, "on_version_status"))
            {
                pybind11::object res = obj_.attr("on_version_status")(major, minor, status);
                if (!res.is_none())
                {
                    return std::string(pybind11::cast<pybind11::bytes>(res));
                }
            }
            throw std::runtime_error("Python orchestrator missing method or returned None: on_version_status");
        }

        std::string onHandshake(const std::string &payload_bytes) override
        {
            return callPythonMethod("on_handshake", payload_bytes);
        }

        std::string getAuthCompleteResponse() override
        {
            pybind11::gil_scoped_acquire acquire;
            if (!obj_.is_none() && pybind11::hasattr(obj_, "get_auth_complete_response"))
            {
                pybind11::object res = obj_.attr("get_auth_complete_response")();
                if (!res.is_none())
                {
                    return std::string(pybind11::cast<pybind11::bytes>(res));
                }
            }
            throw std::runtime_error("Python orchestrator missing method or returned None: get_auth_complete_response");
        }

        // -----------------------------------------------------------------------
        // Control Channel
        // -----------------------------------------------------------------------

        std::string onServiceDiscoveryRequest(const std::string &request_bytes) override
        {
            return callPythonMethod("on_service_discovery_request", request_bytes);
        }

        std::string onPingRequest(const std::string &request_bytes) override
        {
            return callPythonMethod("on_ping_request", request_bytes);
        }

        std::string onAudioFocusRequest(const std::string &request_bytes) override
        {
            return callPythonMethod("on_audio_focus_request", request_bytes);
        }

        std::string onNavigationFocusRequest(const std::string &request_bytes) override
        {
            return callPythonMethod("on_navigation_focus_request", request_bytes);
        }

        // Sink silente: il C++ non si aspetta risposta, ma non deve lanciare eccezione
        std::string onVoiceSessionRequest(const std::string &request_bytes) override
        {
            return callPythonMethodSilent("on_voice_session_request", request_bytes);
        }

        // Sink silente: il C++ non si aspetta risposta, ma non deve lanciare eccezione
        std::string onBatteryStatusNotification(const std::string &request_bytes) override
        {
            return callPythonMethodSilent("on_battery_status_notification", request_bytes);
        }

        // -----------------------------------------------------------------------
        // Media Channels — handshake a 3 step
        // -----------------------------------------------------------------------

        // Step 1: AVChannelSetupRequest
        // Chiama Python: on_av_channel_setup_request(channel_id: int, payload: bytes) -> bytes
        std::string onAvChannelSetupRequest(aasdk::messenger::ChannelId channel_id, const std::string &request_bytes) override
        {
            return callPythonMethodWithChannel("on_av_channel_setup_request", static_cast<int>(channel_id), request_bytes);
        }

        // Step 2: ChannelOpenRequest
        // Chiama Python: on_channel_open_request(channel_id: int, payload: bytes) -> bytes
        std::string onChannelOpenRequest(aasdk::messenger::ChannelId channel_id, const std::string &request_bytes) override
        {
            return callPythonMethodWithChannel("on_channel_open_request", static_cast<int>(channel_id), request_bytes);
        }

        // Step 3 (solo CH_VIDEO): VideoFocusRequestNotification
        std::string onVideoFocusRequest(const std::string &request_bytes) override
        {
            return callPythonMethod("on_video_focus_request", request_bytes);
        }

        // Phase 5: video stream
        std::string onVideoChannelOpenRequest(const std::string &request_bytes) override
        {
            return callPythonMethod("on_video_channel_open_request", request_bytes);
        }

    private:
        // -----------------------------------------------------------------------
        // Helper: chiamata standard (payload only) — lancia se mancante o None
        // -----------------------------------------------------------------------
        std::string callPythonMethod(const char *method_name, const std::string &payload)
        {
            pybind11::gil_scoped_acquire acquire;
            if (!obj_.is_none() && pybind11::hasattr(obj_, method_name))
            {
                pybind11::object res = obj_.attr(method_name)(pybind11::bytes(payload));
                if (!res.is_none())
                {
                    return std::string(pybind11::cast<pybind11::bytes>(res));
                }
            }
            throw std::runtime_error(
                std::string("Python orchestrator missing method or returned None: ") + method_name);
        }

        // -----------------------------------------------------------------------
        // Helper: chiamata con channel_id (int) + payload
        // Usata per on_av_channel_setup_request e on_channel_open_request
        // Firma Python attesa: method(channel_id: int, payload: bytes) -> bytes
        // -----------------------------------------------------------------------
        std::string callPythonMethodWithChannel(const char *method_name,
                                                int channel_id,
                                                const std::string &payload)
        {
            pybind11::gil_scoped_acquire acquire;
            if (!obj_.is_none() && pybind11::hasattr(obj_, method_name))
            {
                pybind11::object res = obj_.attr(method_name)(
                    pybind11::int_(channel_id),
                    pybind11::bytes(payload));
                if (!res.is_none())
                {
                    return std::string(pybind11::cast<pybind11::bytes>(res));
                }
            }
            throw std::runtime_error(
                std::string("Python orchestrator missing method or returned None: ") + method_name);
        }

        // -----------------------------------------------------------------------
        // Helper: sink silente — non lancia se il metodo ritorna b"" o None
        // Usata per on_voice_session_request e on_battery_status_notification
        // Il C++ non si aspetta payload di risposta da questi messaggi
        // -----------------------------------------------------------------------
        std::string callPythonMethodSilent(const char *method_name, const std::string &payload)
        {
            pybind11::gil_scoped_acquire acquire;
            if (!obj_.is_none() && pybind11::hasattr(obj_, method_name))
            {
                try
                {
                    pybind11::object res = obj_.attr(method_name)(pybind11::bytes(payload));
                    if (!res.is_none())
                    {
                        std::string result = std::string(pybind11::cast<pybind11::bytes>(res));
                        // Ritorna stringa vuota se il Python ha restituito b""
                        return result;
                    }
                }
                catch (const pybind11::error_already_set &e)
                {
                    std::cerr << "[PyOrchestrator] Warning in " << method_name
                              << ": " << e.what() << std::endl;
                }
            }
            // Metodo assente o None -> silenzio accettabile per sink
            return "";
        }

        pybind11::object obj_;
    };

} // namespace nemo
