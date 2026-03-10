#pragma once

#include <iostream>
#include <aasdk/Channel/MediaSink/Audio/IAudioMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Audio/IAudioMediaSinkService.hpp>
#include <aasdk/Channel/Promise.hpp>
#include "iorchestrator.hpp"

namespace nemo
{

    /**
     * AudioEventHandler
     *
     * Gestisce i 4 canali audio sink (CH 4, 5, 6, 9).
     * Allineato ad AudioMediaSinkService-19.cpp (openauto).
     *
     * Flusso per ogni canale:
     *   Android -> HU : AVChannelSetupRequest  -> Config(READY)
     *   Android -> HU : ChannelOpenRequest     -> ChannelOpenResponse(SUCCESS)
     *   Android -> HU : MediaChannelStart      -> audioOutput->start()
     *   Android -> HU : MediaWithTimestamp     -> drop + Ack (Phase 4)
     *                                          -> JitterBuffer C++ (Phase 5)
     *   Android -> HU : MediaChannelStop       -> audioOutput->suspend()
     */
    class AudioEventHandler
        : public aasdk::channel::mediasink::audio::IAudioMediaSinkServiceEventHandler,
          public std::enable_shared_from_this<AudioEventHandler>
    {
    public:
        using Pointer = std::shared_ptr<AudioEventHandler>;

        AudioEventHandler(boost::asio::io_service::strand &strand,
                          aasdk::channel::mediasink::audio::IAudioMediaSinkService::Pointer channel,
                          std::shared_ptr<IOrchestrator> orchestrator,
                          aasdk::messenger::ChannelId channel_id)
            : strand_(strand),
              channel_(std::move(channel)),
              orchestrator_(std::move(orchestrator)),
              channel_id_(channel_id) {}

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

        // -----------------------------------------------------------------------
        // Step 1: AVChannelSetupRequest -> Config(READY)
        // Ref: AudioMediaSinkService-19.cpp::onMediaChannelSetupRequest()
        // -----------------------------------------------------------------------
        void onMediaChannelSetupRequest(
            const aap_protobuf::service::media::shared::message::Setup &request) override
        {
            std::cout << "[Audio CH" << static_cast<int>(channel_id_) << "] AVChannelSetupRequest received." << std::endl;
            if (!orchestrator_)
            {
                channel_->receive(this->shared_from_this());
                return;
            }
            std::string req_str = request.SerializeAsString();
            std::string res_str = orchestrator_->onAvChannelSetupRequest(channel_id_, req_str);

            aap_protobuf::service::media::shared::message::Config config_resp;
            if (!config_resp.ParseFromString(res_str))
            {
                std::cerr << "[Audio CH" << static_cast<int>(channel_id_) << "] Config parse FAILED" << std::endl;
                channel_->receive(this->shared_from_this());
                return;
            }
            channel_->sendChannelSetupResponse(config_resp,
                                               makePromise(("Audio/AVChannelSetupResponse_CH" + std::to_string(static_cast<int>(channel_id_))).c_str()));
            channel_->receive(this->shared_from_this());
        }

        // -----------------------------------------------------------------------
        // Step 2: ChannelOpenRequest -> ChannelOpenResponse(SUCCESS)
        // Ref: AudioMediaSinkService-19.cpp::onChannelOpenRequest()
        // -----------------------------------------------------------------------
        void onChannelOpenRequest(
            const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
        {
            std::cout << "[Audio CH" << static_cast<int>(channel_id_) << "] ChannelOpenRequest received." << std::endl;
            if (!orchestrator_)
            {
                channel_->receive(this->shared_from_this());
                return;
            }
            std::string req_str = request.SerializeAsString();
            std::string res_str = orchestrator_->onChannelOpenRequest(channel_id_, req_str);

            aap_protobuf::service::control::message::ChannelOpenResponse open_resp;
            if (!open_resp.ParseFromString(res_str))
            {
                // Fallback: STATUS_SUCCESS = 0
                open_resp.set_status(static_cast<decltype(open_resp.status())>(0));
            }
            channel_->sendChannelOpenResponse(open_resp,
                                              makePromise(("Audio/ChannelOpenResponse_CH" + std::to_string(static_cast<int>(channel_id_))).c_str()));
            channel_->receive(this->shared_from_this());
        }

        // -----------------------------------------------------------------------
        // Step 3: MediaChannelStart -> avvia sink audio
        // Ref: AudioMediaSinkService-19.cpp::onMediaChannelStartIndication()
        // Phase 4: solo log. Phase 5: avvia PCM sink (ALSA/PulseAudio, no GIL).
        // -----------------------------------------------------------------------
        void onMediaChannelStartIndication(
            const aap_protobuf::service::media::shared::message::Start &indication) override
        {
            std::cout << "[Audio CH" << static_cast<int>(channel_id_) << "] MediaChannelStart session="
                      << indication.session_id() << std::endl;
            // Phase 5: audioOutput_->start()
            channel_->receive(this->shared_from_this());
        }

        // -----------------------------------------------------------------------
        // MediaChannelStop
        // Ref: AudioMediaSinkService-19.cpp::onMediaChannelStopIndication()
        // -----------------------------------------------------------------------
        void onMediaChannelStopIndication(
            const aap_protobuf::service::media::shared::message::Stop &indication) override
        {
            std::cout << "[Audio CH" << static_cast<int>(channel_id_) << "] MediaChannelStop" << std::endl;
            // Phase 5: audioOutput_->suspend()
            channel_->receive(this->shared_from_this());
        }

        // -----------------------------------------------------------------------
        // PCM frame con timestamp
        // Ref: AudioMediaSinkService-19.cpp::onMediaWithTimestampIndication()
        // Phase 4: drop + Ack
        // Phase 5: push nel JitterBuffer C++ lock-free (NO GIL, NO Python)
        // -----------------------------------------------------------------------
        void onMediaWithTimestampIndication(
            aasdk::messenger::Timestamp::ValueType ts,
            const aasdk::common::DataConstBuffer &buffer) override
        {
            (void)ts;
            (void)buffer;
            // TODO Phase 5: jitter_buffer_->push(ts, buffer);
            // Invia Ack
            aap_protobuf::service::media::source::message::Ack ack;
            ack.set_session_id(session_id_);
            ack.set_ack(1);
            channel_->sendMediaAckIndication(ack,
                                             makePromise(("Audio/MediaAck_CH" + std::to_string(static_cast<int>(channel_id_))).c_str()));
            channel_->receive(this->shared_from_this());
        }

        // -----------------------------------------------------------------------
        // PCM frame senza timestamp (delegate)
        // Ref: AudioMediaSinkService-19.cpp::onMediaIndication()
        // -----------------------------------------------------------------------
        void onMediaIndication(const aasdk::common::DataConstBuffer &buffer) override
        {
            this->onMediaWithTimestampIndication(0, buffer);
        }

        void onChannelError(const aasdk::error::Error &e) override
        {
            std::cerr << "[Audio CH" << static_cast<int>(channel_id_) << "] Channel Error: " << e.what() << std::endl;
        }

    private:
        boost::asio::io_service::strand &strand_;
        aasdk::channel::mediasink::audio::IAudioMediaSinkService::Pointer channel_;
        std::shared_ptr<IOrchestrator> orchestrator_;
        aasdk::messenger::ChannelId channel_id_;
        int32_t session_id_ = -1; // aggiornato da onMediaChannelStartIndication in Phase 5
    };

} // namespace nemo
