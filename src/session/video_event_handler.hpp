#pragma once

#include <iostream>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkService.hpp>
#include <aasdk/Channel/Promise.hpp>
#include "iorchestrator.hpp"

namespace nemo
{

    class VideoEventHandler
        : public aasdk::channel::mediasink::video::IVideoMediaSinkServiceEventHandler,
          public std::enable_shared_from_this<VideoEventHandler>
    {
    public:
        using Pointer = std::shared_ptr<VideoEventHandler>;

        explicit VideoEventHandler(boost::asio::io_service::strand &strand,
                                   aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel,
                                   std::shared_ptr<IOrchestrator> orchestrator)
            : strand_(strand),
              channel_(std::move(channel)),
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

        // -----------------------------------------------------------------------
        // Step 1: AVChannelSetupRequest -> AVChannelSetupResponse (Config proto)
        //         -> VideoFocusIndication(PROJECTED) nel promise->then
        // Ref: VideoMediaSinkService.cpp::onMediaChannelSetupRequest()
        //   promise->then(sendVideoFocusIndication)
        //   channel_->sendChannelSetupResponse(response, promise)
        //   channel_->receive()
        // Python restituisce SOLO il Config serializzato (no concatenazione).
        // -----------------------------------------------------------------------
        void onMediaChannelSetupRequest(
            const aap_protobuf::service::media::shared::message::Setup &request) override
        {
            std::cout << "[Video] AVChannelSetupRequest received." << std::endl;
            if (!orchestrator_)
            {
                channel_->receive(this->shared_from_this());
                return;
            }

            std::string req_str = request.SerializeAsString();
            // Python restituisce SOLO Config serializzato (nessuna concatenazione).
            std::string res_str = orchestrator_->onAvChannelSetupRequest(
                aasdk::messenger::ChannelId::MEDIA_SINK_VIDEO, req_str);

            aap_protobuf::service::media::shared::message::Config config_resp;
            if (!config_resp.ParseFromString(res_str))
            {
                std::cerr << "[Video] AVChannelSetupResponse parse FAILED" << std::endl;
                channel_->receive(this->shared_from_this());
                return;
            }

            // Ref: VideoMediaSinkService.cpp: promise->then(sendVideoFocusIndication)
            // Usiamo la strand per garantire che VideoFocusIndication parta
            // solo dopo il completamento dell'invio di Config.
            auto setup_promise = aasdk::channel::SendPromise::defer(strand_);
            auto self = this->shared_from_this();
            setup_promise->then(
                [self]()
                { self->sendVideoFocusIndication(); },
                [](const aasdk::error::Error &e)
                {
                    std::cerr << "[Video] sendChannelSetupResponse FAILED: " << e.what() << std::endl;
                });
            channel_->sendChannelSetupResponse(config_resp, std::move(setup_promise));
            channel_->receive(this->shared_from_this());
        }

        // -----------------------------------------------------------------------
        // Step 2: ChannelOpenRequest -> ChannelOpenResponse(SUCCESS)
        // Ref: VideoMediaSinkService.cpp::onChannelOpenRequest()
        //   sendChannelOpenResponse -> channel_->receive()  [NO VideoFocusIndication qui]
        //   Il VideoFocus e' gia' stato inviato nello step 1 (promise->then).
        // -----------------------------------------------------------------------
        void onChannelOpenRequest(
            const aap_protobuf::service::control::message::ChannelOpenRequest &request) override
        {
            std::cout << "[Video] ChannelOpenRequest received." << std::endl;
            if (!orchestrator_)
            {
                channel_->receive(this->shared_from_this());
                return;
            }

            std::string req_str = request.SerializeAsString();

            // Python restituisce solo ChannelOpenResponse serializzato.
            std::string open_res = orchestrator_->onChannelOpenRequest(
                aasdk::messenger::ChannelId::MEDIA_SINK_VIDEO, req_str);
            aap_protobuf::service::control::message::ChannelOpenResponse open_resp;
            if (!open_resp.ParseFromString(open_res))
            {
                // Fallback: STATUS_SUCCESS = 0
                open_resp.set_status(static_cast<decltype(open_resp.status())>(0));
            }
            channel_->sendChannelOpenResponse(open_resp, makePromise("Video/ChannelOpenResponse"));

            // NOTA: nessun sendVideoFocusIndication() qui.
            // Ref: VideoMediaSinkService.cpp::onChannelOpenRequest() -> solo receive().
            // Il VideoFocus e' inviato nello step 1 (dopo Config, via promise->then).

            // Phase 5 hook: notifica l'orchestrator che il canale video e' aperto
            orchestrator_->onVideoChannelOpenRequest(req_str);

            channel_->receive(this->shared_from_this());
        }

        // -----------------------------------------------------------------------
        // Step 3: VideoFocusRequestNotification -> VideoFocusNotification(PROJECTED)
        // Ref: VideoMediaSinkService.cpp::onVideoFocusRequest()
        // Questo e' il gate finale che sblocca lo stream H.264.
        // -----------------------------------------------------------------------
        void onVideoFocusRequest(
            const aap_protobuf::service::media::video::message::VideoFocusRequestNotification &request) override
        {
            std::cout << "[Video] VideoFocusRequest received -> rispondo PROJECTED." << std::endl;
            if (!orchestrator_)
            {
                channel_->receive(this->shared_from_this());
                return;
            }
            std::string req_str = request.SerializeAsString();
            std::string res_str = orchestrator_->onVideoFocusRequest(req_str);

            if (!res_str.empty())
            {
                aap_protobuf::service::media::video::message::VideoFocusNotification vf;
                if (vf.ParseFromString(res_str))
                {
                    channel_->sendVideoFocusIndication(vf, makePromise("Video/VideoFocusIndication"));
                }
            }
            channel_->receive(this->shared_from_this());
        }

        // -----------------------------------------------------------------------
        // Media stream (Phase 5)
        // -----------------------------------------------------------------------

        void onMediaChannelStartIndication(
            const aap_protobuf::service::media::shared::message::Start &indication) override
        {
            std::cout << "[Video] MediaChannelStart session=" << indication.session_id() << std::endl;
            channel_->receive(this->shared_from_this());
        }

        void onMediaChannelStopIndication(
            const aap_protobuf::service::media::shared::message::Stop &indication) override
        {
            std::cout << "[Video] MediaChannelStop" << std::endl;
            channel_->receive(this->shared_from_this());
        }

        void onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType ts,
                                            const aasdk::common::DataConstBuffer &buffer) override
        {
            //(void)ts;
            //(void)buffer;
            std::cout << "[Video] NAL unit ts=" << ts
                      << " size=" << buffer.size << " bytes" << std::endl;
            // Phase 4: drop frame (no Ack necessario su video, solo receive).
            // Phase 5: NAL unit -> GStreamer appsrc / libavcodec (zero-copy, NO GIL).
            channel_->receive(this->shared_from_this());
        }

        void onMediaIndication(const aasdk::common::DataConstBuffer &buffer) override
        {
            (void)buffer;
            channel_->receive(this->shared_from_this());
        }

        void onChannelError(const aasdk::error::Error &e) override
        {
            std::cerr << "[Video] Channel Error: " << e.what() << std::endl;
        }

    private:
        // Invia VideoFocusNotification(focus=PROJECTED, unsolicited=false).
        // Chiamata SOLO nel promise->then di sendChannelSetupResponse (step 1).
        // Ref: VideoMediaSinkService.cpp::sendVideoFocusIndication()
        void sendVideoFocusIndication()
        {
            std::cout << "[Video] sendVideoFocusIndication() -> PROJECTED" << std::endl;
            aap_protobuf::service::media::video::message::VideoFocusNotification vf;
            vf.set_focus(
                aap_protobuf::service::media::video::message::VideoFocusMode::VIDEO_FOCUS_PROJECTED);
            vf.set_unsolicited(false);
            channel_->sendVideoFocusIndication(vf, makePromise("Video/VideoFocusIndication"));
        }

        boost::asio::io_service::strand &strand_;
        aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel_;
        std::shared_ptr<IOrchestrator> orchestrator_;
    };

} // namespace nemo
