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
#include "gst/gst_video_sink.hpp"

namespace nemo
{

    class SessionManager : public std::enable_shared_from_this<SessionManager>
    {
    public:
        using Pointer = std::shared_ptr<SessionManager>;

        SessionManager(boost::asio::io_context &io_ctx,
                       aasdk::messenger::IMessenger::Pointer messenger,
                       aasdk::messenger::ICryptor::Pointer cryptor,
                       std::shared_ptr<IOrchestrator> orchestrator,
                       std::shared_ptr<GstVideoSink>  video_sink = nullptr)
            : strand_(io_ctx),
              messenger_(std::move(messenger)),
              cryptor_(std::move(cryptor)),
              orchestrator_(std::move(orchestrator)),
              video_sink_(std::move(video_sink)) {}

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

        // ── enableVideoDump ───────────────────────────────────────────────
        // Apre il file di dump H.264 su VideoEventHandler.
        // Thread-safe: il dispatch viene eseguito sullo strand interno.
        // Chiamare prima di start() o subito dopo; sicuro chiamarlo
        // dal thread Python prima che arrivi il primo NAL unit.
        // -----------------------------------------------------------------
        void enableVideoDump(const std::string &path)
        {
            strand_.dispatch([this, self = shared_from_this(), path]()
            {
                if (video_handler_)
                {
                    video_handler_->enableDump(path);
                }
                else
                {
                    // Salva il path: video_handler_ potrebbe non essere
                    // ancora costruito se chiamato prima di start().
                    pending_dump_path_ = path;
                }
            });
        }

        void start()
        {
            strand_.dispatch([this, self = shared_from_this()]()
                             {
            std::cout << "[SessionManager] Starting channels..." << std::endl;

            control_channel_ = std::make_shared<aasdk::channel::control::ControlServiceChannel>(
                strand_, messenger_);
            control_handler_ = std::make_shared<ControlEventHandler>(
                strand_, control_channel_, cryptor_, orchestrator_);
            control_channel_->receive(control_handler_);
            control_channel_->sendVersionRequest(makePromise("Control/VersionRequest"));

            sensor_channel_ = std::make_shared<aasdk::channel::sensorsource::SensorSourceService>(
                strand_, messenger_);
            sensor_handler_ = std::make_shared<SensorEventHandler>(
                strand_, sensor_channel_, orchestrator_);
            sensor_channel_->receive(sensor_handler_);

            video_channel_ = std::make_shared<aasdk::channel::mediasink::video::VideoMediaSinkService>(
                strand_, messenger_, aasdk::messenger::ChannelId::MEDIA_SINK_VIDEO);
            video_handler_ = std::make_shared<VideoEventHandler>(
                strand_, video_channel_, orchestrator_, video_sink_);
            video_channel_->receive(video_handler_);

            // Applica dump path pendente (se enableVideoDump() chiamato prima di start())
            if (!pending_dump_path_.empty())
            {
                video_handler_->enableDump(pending_dump_path_);
                pending_dump_path_.clear();
            }

            media_audio_channel_ = std::make_shared<aasdk::channel::mediasink::audio::AudioMediaSinkService>(
                strand_, messenger_, aasdk::messenger::ChannelId::MEDIA_SINK_MEDIA_AUDIO);
            media_audio_handler_ = std::make_shared<AudioEventHandler>(
                strand_, media_audio_channel_, orchestrator_, aasdk::messenger::ChannelId::MEDIA_SINK_MEDIA_AUDIO);
            media_audio_channel_->receive(media_audio_handler_);

            speech_audio_channel_ = std::make_shared<aasdk::channel::mediasink::audio::AudioMediaSinkService>(
                strand_, messenger_, aasdk::messenger::ChannelId::MEDIA_SINK_GUIDANCE_AUDIO);
            speech_audio_handler_ = std::make_shared<AudioEventHandler>(
                strand_, speech_audio_channel_, orchestrator_, aasdk::messenger::ChannelId::MEDIA_SINK_GUIDANCE_AUDIO);
            speech_audio_channel_->receive(speech_audio_handler_);

            system_audio_channel_ = std::make_shared<aasdk::channel::mediasink::audio::AudioMediaSinkService>(
                strand_, messenger_, aasdk::messenger::ChannelId::MEDIA_SINK_SYSTEM_AUDIO);
            system_audio_handler_ = std::make_shared<AudioEventHandler>(
                strand_, system_audio_channel_, orchestrator_, aasdk::messenger::ChannelId::MEDIA_SINK_SYSTEM_AUDIO);
            system_audio_channel_->receive(system_audio_handler_);

            input_channel_ = std::make_shared<aasdk::channel::inputsource::InputSourceService>(
                strand_, messenger_);
            input_handler_ = std::make_shared<InputEventHandler>(
                strand_, input_channel_, orchestrator_);
            input_channel_->receive(input_handler_);

            mic_channel_ = std::make_shared<aasdk::channel::mediasink::audio::AudioMediaSinkService>(
                strand_, messenger_, aasdk::messenger::ChannelId::MEDIA_SOURCE_MICROPHONE);
            mic_handler_ = std::make_shared<AudioEventHandler>(
                strand_, mic_channel_, orchestrator_, aasdk::messenger::ChannelId::MEDIA_SOURCE_MICROPHONE);
            mic_channel_->receive(mic_handler_);

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
        std::shared_ptr<GstVideoSink>  video_sink_;
        std::string pending_dump_path_;  // per enableVideoDump() prima di start()

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
