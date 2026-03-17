#pragma once

#include <pybind11/pybind11.h>

#include <boost/asio/io_service.hpp>
#include <aasdk/Messenger/ChannelId.hpp>
#include <aasdk/Messenger/IMessenger.hpp>
#include <aasdk/Channel/Control/ControlServiceChannel.hpp>
#include <aasdk/Channel/MediaSink/Audio/AudioMediaSinkService.hpp>
#include <aasdk/Channel/MediaSink/Video/VideoMediaSinkService.hpp>
#include <aasdk/Channel/SensorSource/SensorSourceService.hpp>
#include <aasdk/Channel/InputSource/InputSourceService.hpp>
#include <aasdk/Channel/NavigationStatus/NavigationStatusService.hpp>

namespace py = pybind11;

inline void init_channels(py::module_ &m)
{
    using ChannelId = aasdk::messenger::ChannelId;

    py::enum_<ChannelId>(m, "ChannelId")
        .value("CONTROL", ChannelId::CONTROL)
        .value("SENSOR", ChannelId::SENSOR)
        .value("MEDIA_SINK", ChannelId::MEDIA_SINK)
        .value("MEDIA_SINK_VIDEO", ChannelId::MEDIA_SINK_VIDEO)
        .value("MEDIA_SINK_MEDIA_AUDIO", ChannelId::MEDIA_SINK_MEDIA_AUDIO)
        .value("MEDIA_SINK_GUIDANCE_AUDIO", ChannelId::MEDIA_SINK_GUIDANCE_AUDIO)
        .value("MEDIA_SINK_SYSTEM_AUDIO", ChannelId::MEDIA_SINK_SYSTEM_AUDIO)
        .value("MEDIA_SINK_TELEPHONY_AUDIO", ChannelId::MEDIA_SINK_TELEPHONY_AUDIO)
        .value("INPUT_SOURCE", ChannelId::INPUT_SOURCE)
        .value("MEDIA_SOURCE_MICROPHONE", ChannelId::MEDIA_SOURCE_MICROPHONE)
        .value("BLUETOOTH", ChannelId::BLUETOOTH)
        .value("RADIO", ChannelId::RADIO)
        .value("NAVIGATION_STATUS", ChannelId::NAVIGATION_STATUS)
        .value("MEDIA_PLAYBACK_STATUS", ChannelId::MEDIA_PLAYBACK_STATUS)
        .value("PHONE_STATUS", ChannelId::PHONE_STATUS)
        .value("MEDIA_BROWSER", ChannelId::MEDIA_BROWSER)
        .value("VENDOR_EXTENSION", ChannelId::VENDOR_EXTENSION)
        .value("GENERIC_NOTIFICATION", ChannelId::GENERIC_NOTIFICATION)
        .value("WIFI_PROJECTION", ChannelId::WIFI_PROJECTION)
        .value("NONE", ChannelId::NONE);

    using Messenger = aasdk::messenger::IMessenger;
    using StrandPtr = std::shared_ptr<boost::asio::io_service::strand>;

    py::class_<aasdk::channel::mediasink::video::VideoMediaSinkService,
               std::shared_ptr<aasdk::channel::mediasink::video::VideoMediaSinkService>>(m, "VideoMediaSinkService")
        .def(py::init([](StrandPtr strand, Messenger::Pointer messenger, ChannelId channel_id)
                      {
                          return std::make_shared<aasdk::channel::mediasink::video::VideoMediaSinkService>(
                              *strand, std::move(messenger), channel_id);
                      }),
             py::arg("strand"),
             py::arg("messenger"),
             py::arg("channel_id"));

    py::class_<aasdk::channel::sensorsource::SensorSourceService,
               std::shared_ptr<aasdk::channel::sensorsource::SensorSourceService>>(m, "SensorSourceService")
        .def(py::init([](StrandPtr strand, Messenger::Pointer messenger)
                      {
                          return std::make_shared<aasdk::channel::sensorsource::SensorSourceService>(*strand, std::move(messenger));
                      }),
             py::arg("strand"),
             py::arg("messenger"));

    py::class_<aasdk::channel::inputsource::InputSourceService,
               std::shared_ptr<aasdk::channel::inputsource::InputSourceService>>(m, "InputSourceService")
        .def(py::init([](StrandPtr strand, Messenger::Pointer messenger)
                      {
                          return std::make_shared<aasdk::channel::inputsource::InputSourceService>(*strand, std::move(messenger));
                      }),
             py::arg("strand"),
             py::arg("messenger"));

    py::class_<aasdk::channel::navigationstatus::NavigationStatusService,
               std::shared_ptr<aasdk::channel::navigationstatus::NavigationStatusService>>(m, "NavigationStatusService")
        .def(py::init([](StrandPtr strand, Messenger::Pointer messenger)
                      {
                          return std::make_shared<aasdk::channel::navigationstatus::NavigationStatusService>(*strand, std::move(messenger));
                      }),
             py::arg("strand"),
             py::arg("messenger"));
}
