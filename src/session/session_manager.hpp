#pragma once

#include <memory>
#include <boost/asio.hpp>
#include <aasdk/Messenger/IMessenger.hpp>
#include <aasdk/Channel/Control/ControlServiceChannel.hpp>
#include <aasdk/Channel/MediaSink/Video/VideoMediaSinkService.hpp>
#include <aasdk/Channel/MediaSink/Audio/AudioMediaSinkService.hpp>
#include <aasdk/Channel/SensorSource/SensorSourceService.hpp>
#include <aasdk/Channel/InputSource/InputSourceService.hpp>
#include <aasdk/Channel/NavigationStatus/NavigationStatusService.hpp>
#include <aasdk/Channel/Promise.hpp>
#include "control_event_handler.hpp"
#include "video_event_handler.hpp"
#include "audio_event_handler.hpp"
#include "sensor_event_handler.hpp"
#include "input_event_handler.hpp"
#include "navigation_event_handler.hpp"
#include "iorchestrator.hpp"

// Channel IDs da aasdk/Messenger/ChannelId.hpp (enum posizionale)
// CONTROL=0, SENSOR=1, MEDIA_SINK=2, MEDIA_SINK_VIDEO=3,
// MEDIA_SINK_MEDIA_AUDIO=4, MEDIA_SINK_GUIDANCE_AUDIO=5,
// MEDIA_SINK_SYSTEM_AUDIO=6, MEDIA_SINK_TELEPHONY_AUDIO=7,
// INPUT_SOURCE=8, MEDIA_SOURCE_MICROPHONE=9,
// BLUETOOTH=10, RADIO=11, NAVIGATION_STATUS=12, ...

namespace nemo
{

    class SessionManager : public std::enable_shared_from_this<SessionManager>
    {
    public:
        using Pointer = std::shared_ptr<SessionManager>;

        SessionManager(boost::asio::io_context &io_ctx,
                       aasdk::messenger::IMessenger::Pointer messenger,
                       aasdk::messenger::ICryptor::Pointer cryptor,
                       std::shared_ptr<IOrchestrator> orchestrator)
            : strand_(io_ctx),
              messenger_(std::move(messenger)),
              cryptor_(std::move(cryptor)),
              orchestrator_(std::move(orchestrator)) {}

        aasdk::channel::SendPromise::Pointer makePromise(const char *tag)
        {
            auto p = aasdk::channel::SendPromise::defer(strand_);
            p->then(
                []() {},
                [tag](const aasdk::error::Error &e)
                {
                    std::cerr << "[" << tag << "] send FAILED: " << e.what() << std::endl;
                });
            return p;
        }

        void start()
        {
            strand_.dispatch([this, self = shared_from_this()]()
                             {
            std::cout << "[SessionManager] Starting channels..." << std::endl;

            // ---------------------------------------------------------------
            // CH 0: Control Service
            // ---------------------------------------------------------------
            control_channel_ = std::make_shared<aasdk::channel::control::ControlServiceChannel>(
                strand_, messenger_);
            control_handler_ = std::make_shared<ControlEventHandler>(
                strand_, control_channel_, cryptor_, orchestrator_);
            control_channel_->receive(control_handler_);
            control_channel_->sendVersionRequest(makePromise("Control/VersionRequest"));

            // ---------------------------------------------------------------
            // CH 1: Sensor Source
            // GATE CRITICO: Android attende SensorStartResponse(DRIVING_STATUS)
            // + DrivingStatus UNRESTRICTED prima di avviare lo stream H.264.
            // Ref: SensorService.cpp::onSensorStartRequest()
            // ---------------------------------------------------------------
            sensor_channel_ = std::make_shared<aasdk::channel::sensorsource::SensorSourceService>(
                strand_, messenger_);
            sensor_handler_ = std::make_shared<SensorEventHandler>(
                strand_, sensor_channel_, orchestrator_);
            sensor_channel_->receive(sensor_handler_);

            // ---------------------------------------------------------------
            // CH 3: Video Sink
            // ---------------------------------------------------------------
            video_channel_ = std::make_shared<aasdk::channel::mediasink::video::VideoMediaSinkService>(
                strand_, messenger_, aasdk::messenger::ChannelId::MEDIA_SINK_VIDEO);
            video_handler_ = std::make_shared<VideoEventHandler>(
                strand_, video_channel_, orchestrator_);
            video_channel_->receive(video_handler_);

            // ---------------------------------------------------------------
            // CH 4: Media Audio Sink
            // ---------------------------------------------------------------
            media_audio_channel_ = std::make_shared<aasdk::channel::mediasink::audio::AudioMediaSinkService>(
                strand_, messenger_, aasdk::messenger::ChannelId::MEDIA_SINK_MEDIA_AUDIO);
            media_audio_handler_ = std::make_shared<AudioEventHandler>(
                strand_, media_audio_channel_, orchestrator_, aasdk::messenger::ChannelId::MEDIA_SINK_MEDIA_AUDIO);
            media_audio_channel_->receive(media_audio_handler_);

            // ---------------------------------------------------------------
            // CH 5: Speech Audio Sink (Guidance)
            // ---------------------------------------------------------------
            speech_audio_channel_ = std::make_shared<aasdk::channel::mediasink::audio::AudioMediaSinkService>(
                strand_, messenger_, aasdk::messenger::ChannelId::MEDIA_SINK_GUIDANCE_AUDIO);
            speech_audio_handler_ = std::make_shared<AudioEventHandler>(
                strand_, speech_audio_channel_, orchestrator_, aasdk::messenger::ChannelId::MEDIA_SINK_GUIDANCE_AUDIO);
            speech_audio_channel_->receive(speech_audio_handler_);

            // ---------------------------------------------------------------
            // CH 6: System Audio Sink
            // ---------------------------------------------------------------
            system_audio_channel_ = std::make_shared<aasdk::channel::mediasink::audio::AudioMediaSinkService>(
                strand_, messenger_, aasdk::messenger::ChannelId::MEDIA_SINK_SYSTEM_AUDIO);
            system_audio_handler_ = std::make_shared<AudioEventHandler>(
                strand_, system_audio_channel_, orchestrator_, aasdk::messenger::ChannelId::MEDIA_SINK_SYSTEM_AUDIO);
            system_audio_channel_->receive(system_audio_handler_);

            // ---------------------------------------------------------------
            // CH 8: Input Source
            // Android invia touch/knob events via KeyBindingRequest/InputReport.
            // GATE: senza questo handler Android Auto non si avvia visivamente.
            // Ref: InputSourceService.cpp (openauto)
            // ---------------------------------------------------------------
            input_channel_ = std::make_shared<aasdk::channel::inputsource::InputSourceService>(
                strand_, messenger_);
            input_handler_ = std::make_shared<InputEventHandler>(
                strand_, input_channel_, orchestrator_);
            input_channel_->receive(input_handler_);

            // ---------------------------------------------------------------
            // CH 9: Mic (MediaSource)
            // Phase 5: sostituire con MediaSourceService per TX verso Android.
            // ---------------------------------------------------------------
            mic_channel_ = std::make_shared<aasdk::channel::mediasink::audio::AudioMediaSinkService>(
                strand_, messenger_, aasdk::messenger::ChannelId::MEDIA_SOURCE_MICROPHONE);
            mic_handler_ = std::make_shared<AudioEventHandler>(
                strand_, mic_channel_, orchestrator_, aasdk::messenger::ChannelId::MEDIA_SOURCE_MICROPHONE);
            mic_channel_->receive(mic_handler_);

            // ---------------------------------------------------------------
            // CH 12: Navigation Status
            // Android invia status/turn/distance events di navigazione.
            // Sink silente in Phase 4; Phase 5 esporrà i dati a Python.
            // Ref: NavigationStatusService.cpp (openauto)
            // ---------------------------------------------------------------
            navigation_channel_ = std::make_shared<aasdk::channel::navigationstatus::NavigationStatusService>(
                strand_, messenger_);
            navigation_handler_ = std::make_shared<NavigationEventHandler>(
                strand_, navigation_channel_, orchestrator_);
            navigation_channel_->receive(navigation_handler_); });
        }

        void stop()
        {
            strand_.dispatch([this, self = shared_from_this()]()
                             {
            control_channel_.reset();      control_handler_.reset();
            sensor_channel_.reset();       sensor_handler_.reset();
            video_channel_.reset();        video_handler_.reset();
            media_audio_channel_.reset();  media_audio_handler_.reset();
            speech_audio_channel_.reset(); speech_audio_handler_.reset();
            system_audio_channel_.reset(); system_audio_handler_.reset();
            input_channel_.reset();        input_handler_.reset();
            mic_channel_.reset();          mic_handler_.reset();
            navigation_channel_.reset();   navigation_handler_.reset(); });
        }

    private:
        boost::asio::io_service::strand strand_;
        aasdk::messenger::IMessenger::Pointer messenger_;
        aasdk::messenger::ICryptor::Pointer cryptor_;
        std::shared_ptr<IOrchestrator> orchestrator_;

        // CH 0
        aasdk::channel::control::IControlServiceChannel::Pointer control_channel_;
        ControlEventHandler::Pointer control_handler_;

        // CH 1
        aasdk::channel::sensorsource::ISensorSourceService::Pointer sensor_channel_;
        SensorEventHandler::Pointer sensor_handler_;

        // CH 3
        aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer video_channel_;
        VideoEventHandler::Pointer video_handler_;

        // CH 4, 5, 6, 9
        aasdk::channel::mediasink::audio::IAudioMediaSinkService::Pointer media_audio_channel_;
        aasdk::channel::mediasink::audio::IAudioMediaSinkService::Pointer speech_audio_channel_;
        aasdk::channel::mediasink::audio::IAudioMediaSinkService::Pointer system_audio_channel_;
        aasdk::channel::mediasink::audio::IAudioMediaSinkService::Pointer mic_channel_;
        std::shared_ptr<AudioEventHandler> media_audio_handler_;
        std::shared_ptr<AudioEventHandler> speech_audio_handler_;
        std::shared_ptr<AudioEventHandler> system_audio_handler_;
        std::shared_ptr<AudioEventHandler> mic_handler_;

        // CH 8
        aasdk::channel::inputsource::IInputSourceService::Pointer input_channel_;
        InputEventHandler::Pointer input_handler_;

        // CH 12
        aasdk::channel::navigationstatus::INavigationStatusService::Pointer navigation_channel_;
        NavigationEventHandler::Pointer navigation_handler_;
    };

} // namespace nemo
