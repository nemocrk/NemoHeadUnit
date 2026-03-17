#pragma once

// Questo header NON include header GStreamer direttamente.
// Dipende SOLO da av_types.hpp (via av_core.hpp) e da pybind11.
// Regola architetturale: il GIL Python non deve mai bloccare path C++ real-time.

#include "av_core.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <stdexcept>
#include <string>
#include <vector>

inline void init_av_core_binding(pybind11::module_ &m)
{
    namespace py = pybind11;

    // ------------------------------------------------------------------ enums
    py::enum_<nemo::OverrunPolicy>(m, "OverrunPolicy")
        .value("DROP_OLD",           nemo::OverrunPolicy::DROP_OLD)
        .value("DROP_NEW",           nemo::OverrunPolicy::DROP_NEW)
        .value("PROGRESSIVE_DISCARD",nemo::OverrunPolicy::PROGRESSIVE_DISCARD);

    py::enum_<nemo::UnderrunPolicy>(m, "UnderrunPolicy")
        .value("WAIT",    nemo::UnderrunPolicy::WAIT)
        .value("SILENCE", nemo::UnderrunPolicy::SILENCE);

    // --------------------------------------------------------------- AvCoreConfig
    py::class_<nemo::AvCoreConfig>(m, "AvCoreConfig")
        .def(py::init<>())
        .def_readwrite("jitter_buffer_ms",  &nemo::AvCoreConfig::jitter_buffer_ms)
        .def_readwrite("max_queue_frames",  &nemo::AvCoreConfig::max_queue_frames)
        .def_readwrite("audio_frame_ms",    &nemo::AvCoreConfig::audio_frame_ms)
        .def_readwrite("max_av_lead_ms",    &nemo::AvCoreConfig::max_av_lead_ms)
        .def_readwrite("mic_frame_ms",      &nemo::AvCoreConfig::mic_frame_ms)
        .def_readwrite("mic_batch_ms",      &nemo::AvCoreConfig::mic_batch_ms)
        .def_readwrite("audio_prebuffer_ms",&nemo::AvCoreConfig::audio_prebuffer_ms)
        .def_readwrite("overrun_policy",    &nemo::AvCoreConfig::overrun_policy)
        .def_readwrite("underrun_policy",   &nemo::AvCoreConfig::underrun_policy);

    // ----------------------------------------------------------------- AvCore
    py::class_<nemo::AvCore, std::shared_ptr<nemo::AvCore>>(m, "AvCore")
        .def(py::init<nemo::AvCoreConfig>(), py::arg("config") = nemo::AvCoreConfig())
        // video
        .def("configure_video", &nemo::AvCore::configureVideo, py::arg("width"), py::arg("height"))
        .def("set_window_id",   &nemo::AvCore::setWindowId,    py::arg("window_id"))
        .def("start_video",     &nemo::AvCore::startVideo)
        .def("stop_video",      &nemo::AvCore::stopVideo)
        // audio
        .def("configure_audio", &nemo::AvCore::configureAudio,
             py::arg("sample_rate"), py::arg("channels"), py::arg("bits"), py::arg("codec") = "PCM")
        .def("configure_audio_stream", &nemo::AvCore::configureAudioStream,
             py::arg("stream_id"), py::arg("sample_rate"),
             py::arg("channels"),  py::arg("bits"), py::arg("codec") = "PCM")
        .def("start_audio",     &nemo::AvCore::startAudio)
        .def("stop_audio",      &nemo::AvCore::stopAudio)
        // mic
        .def("configure_mic",   &nemo::AvCore::configureMic,
             py::arg("sample_rate"), py::arg("channels"), py::arg("bits"))
        .def("start_mic",       &nemo::AvCore::startMicCapture)
        .def("stop_mic",        &nemo::AvCore::stopMicCapture)
        .def("is_mic_active",   &nemo::AvCore::isMicActive)
        // global
        .def("stop",            &nemo::AvCore::stop)
        // Fix #10: Master Audio Clock esposto a Python (read-only, lock-free).
        .def_property_readonly("audio_clock_us", &nemo::AvCore::getAudioClockUs)
        // Routing audio: accetta sia lista piatta [int] (priority order)
        // sia lista di tuple (group: channels, priority[, ducking, hold_ms, gains]).
        .def("set_audio_priority",
             [](nemo::AvCore &self, py::object obj)
             {
                 if (obj.is_none()) { self.setAudioPriority({}); return; }
                 if (!py::isinstance<py::sequence>(obj) || py::isinstance<py::str>(obj))
                     throw std::runtime_error("set_audio_priority expects a list/tuple");
                 py::sequence seq = obj;
                 if (seq.size() == 0) { self.setAudioPriority({}); return; }
                 py::object first = seq[0];
                 if (py::isinstance<py::int_>(first))
                 {
                     std::vector<int> order;
                     order.reserve(seq.size());
                     for (auto item : seq) order.push_back(py::cast<int>(item));
                     self.setAudioPriority(order);
                     return;
                 }
                 std::vector<nemo::AudioGroup> groups;
                 groups.reserve(seq.size());
                 for (auto item : seq)
                 {
                     if (!py::isinstance<py::sequence>(item) || py::isinstance<py::str>(item))
                         throw std::runtime_error("group entries must be tuples/lists");
                     py::sequence tup = item;
                     if (tup.size() < 2)
                         throw std::runtime_error("group entry must be (channels, priority[, ducking])");
                     py::sequence chs = tup[0];
                     std::vector<int> channels;
                     channels.reserve(chs.size());
                     for (auto ch : chs) channels.push_back(py::cast<int>(ch));
                     int priority = py::cast<int>(tup[1]);
                     int ducking  = 100, hold_ms = 100;
                     std::vector<float> gains;
                     if (tup.size() >= 3) ducking  = py::cast<int>(tup[2]);
                     if (tup.size() >= 4) hold_ms  = py::cast<int>(tup[3]);
                     if (tup.size() >= 5)
                     {
                         py::object gains_obj = tup[4];
                         if (py::isinstance<py::sequence>(gains_obj) &&
                             !py::isinstance<py::str>(gains_obj))
                         {
                             py::sequence gseq = gains_obj;
                             gains.reserve(gseq.size());
                             for (auto gval : gseq)
                             {
                                 float gv = py::cast<float>(gval);
                                 if (gv > 2.0f) gv = gv / 100.0f;
                                 gains.push_back(gv);
                             }
                         }
                     }
                     nemo::AudioGroup g;
                     g.channels      = std::move(channels);
                     g.priority      = priority;
                     g.ducking       = ducking;
                     g.hold_ms       = hold_ms;
                     g.channel_gains = std::move(gains);
                     groups.push_back(std::move(g));
                 }
                 self.setAudioGroups(groups);
             })
        .def("set_policies",         &nemo::AvCore::setPolicies)
        .def("set_jitter_buffer_ms", &nemo::AvCore::setJitterBufferMs)
        .def("set_max_queue_frames", &nemo::AvCore::setMaxQueueFrames)
        .def("set_audio_frame_ms",   &nemo::AvCore::setAudioFrameMs)
        .def("set_max_av_lead_ms",   &nemo::AvCore::setMaxAvLeadMs)
        .def("ptr",                  &nemo::AvCore::ptr);
}
