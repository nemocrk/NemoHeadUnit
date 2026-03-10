#pragma once

#include <iostream>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkService.hpp>
#include <aasdk/Channel/Promise.hpp>
#include "iorchestrator.hpp"

// CH_VIDEO = 3 (da aasdk/Messenger/ChannelId.hpp: MEDIA_SINK_VIDEO)
static constexpr int kChVideo = 3;

namespace nemo {

class VideoEventHandler
    : public aasdk::channel::mediasink::video::IVideoMediaSinkServiceEventHandler,
      public std::enable_shared_from_this<VideoEventHandler> {
public:
    using Pointer = std::shared_ptr<VideoEventHandler>;

    explicit VideoEventHandler(boost::asio::io_service::strand& strand,
                               aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel,
                               std::shared_ptr<IOrchestrator> orchestrator)
        : strand_(strand),
          channel_(std::move(channel)),
          orchestrator_(std::move(orchestrator)) {}

    aasdk::channel::SendPromise::Pointer makePromise(const char* tag) {
        auto p = aasdk::channel::SendPromise::defer(strand_);
        p->then(
            []() {},
            [tag](const aasdk::error::Error& e) {
                std::cerr << "[" << tag << "] send FAILED: " << e.what() << std::endl;
            }
        );
        return p;
    }

    // -----------------------------------------------------------------------
    // Step 1: AVChannelSetupRequest -> AVChannelSetupResponse (Config proto)
    // Ref: VideoMediaSinkService-14.cpp::onMediaChannelSetupRequest()
    // Prima: mock vuoto senza risposta -> Android bloccato.
    // Ora: chiama orchestrator->onAvChannelSetupRequest(CH_VIDEO, payload)
    //      e invia Config(READY) + VideoFocusIndication(PROJECTED)
    // -----------------------------------------------------------------------

    void onMediaChannelSetupRequest(
        const aap_protobuf::service::media::shared::message::Setup& request) override {
        std::cout << "[Video] AVChannelSetupRequest received." << std::endl;
        if (!orchestrator_) {
            channel_->receive(this->shared_from_this());
            return;
        }

        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_->onAvChannelSetupRequest(kChVideo, req_str);

        // res_str puo' contenere [Config][VideoFocusNotification] concatenati.
        // Separiamo: il primo messaggio e' sempre Config (AVChannelSetupResponse).
        // Il binding Python restituisce i 2 messaggi serializzati in sequenza;
        // dobbiamo parsare il primo come Config e il secondo (se presente) come
        // VideoFocusNotification.
        // Approccio semplice: prova Config, se rimane payload parsalo come VideoFocus.
        aap_protobuf::service::media::shared::message::Config config_resp;
        if (!config_resp.ParseFromString(res_str)) {
            std::cerr << "[Video] AVChannelSetupResponse parse FAILED" << std::endl;
            channel_->receive(this->shared_from_this());
            return;
        }
        channel_->sendChannelSetupResponse(config_resp, makePromise("Video/AVChannelSetupResponse"));

        // Invia VideoFocusIndication(PROJECTED) come fa VideoMediaSinkService-14.cpp
        // dopo sendChannelSetupResponse.
        sendVideoFocusIndication();
        channel_->receive(this->shared_from_this());
    }

    // -----------------------------------------------------------------------
    // Step 2: ChannelOpenRequest -> ChannelOpenResponse(SUCCESS)
    // Ref: VideoMediaSinkService-14.cpp::onChannelOpenRequest()
    // Prima: chiamava solo onVideoChannelOpenRequest senza channel_id.
    // Ora: chiama onChannelOpenRequest(CH_VIDEO, payload) per la risposta,
    //      poi onVideoChannelOpenRequest per il Phase 5 hook.
    // -----------------------------------------------------------------------

    void onChannelOpenRequest(
        const aap_protobuf::service::control::message::ChannelOpenRequest& request) override {
        std::cout << "[Video] ChannelOpenRequest received." << std::endl;
        if (!orchestrator_) {
            channel_->receive(this->shared_from_this());
            return;
        }

        std::string req_str = request.SerializeAsString();

        // Risposta ChannelOpenResponse(SUCCESS)
        std::string open_res = orchestrator_->onChannelOpenRequest(kChVideo, req_str);
        aap_protobuf::service::control::message::ChannelOpenResponse open_resp;
        if (!open_resp.ParseFromString(open_res)) {
            // Fallback: STATUS_SUCCESS = 0
            open_resp.set_status(static_cast<decltype(open_resp.status())>(0));
        }
        channel_->sendChannelOpenResponse(open_resp, makePromise("Video/ChannelOpenResponse"));

        // Invia VideoFocusIndication(PROJECTED) come fa VideoMediaSinkService-14.cpp
        sendVideoFocusIndication();

        // Phase 5 hook: notifica l'orchestrator che il canale video e' aperto
        orchestrator_->onVideoChannelOpenRequest(req_str);

        channel_->receive(this->shared_from_this());
    }

    // -----------------------------------------------------------------------
    // Step 3: VideoFocusRequestNotification -> VideoFocusNotification(PROJECTED)
    // Ref: VideoMediaSinkService-14.cpp::onVideoFocusRequest()
    // Prima: solo log + drop. Ora: chiama orchestrator e invia VideoFocusIndication.
    // -----------------------------------------------------------------------

    void onVideoFocusRequest(
        const aap_protobuf::service::media::video::message::VideoFocusRequestNotification& request) override {
        std::cout << "[Video] VideoFocusRequest received -> rispondo PROJECTED." << std::endl;
        if (!orchestrator_) {
            channel_->receive(this->shared_from_this());
            return;
        }
        std::string req_str = request.SerializeAsString();
        std::string res_str = orchestrator_->onVideoFocusRequest(req_str);

        if (!res_str.empty()) {
            aap_protobuf::service::media::video::message::VideoFocusNotification vf;
            if (vf.ParseFromString(res_str)) {
                channel_->sendVideoFocusIndication(vf, makePromise("Video/VideoFocusIndication"));
            }
        }
        channel_->receive(this->shared_from_this());
    }

    // -----------------------------------------------------------------------
    // Media stream (Phase 5)
    // -----------------------------------------------------------------------

    void onMediaChannelStartIndication(
        const aap_protobuf::service::media::shared::message::Start& indication) override {
        std::cout << "[Video] MediaChannelStart session=" << indication.session_id() << std::endl;
        channel_->receive(this->shared_from_this());
    }

    void onMediaChannelStopIndication(
        const aap_protobuf::service::media::shared::message::Stop& indication) override {
        std::cout << "[Video] MediaChannelStop" << std::endl;
        channel_->receive(this->shared_from_this());
    }

    void onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType ts,
                                        const aasdk::common::DataConstBuffer& buffer) override {
        (void)ts; (void)buffer;
        // Phase 4: drop frame.
        // Phase 5: NAL unit -> GStreamer appsrc / libavcodec (zero-copy, NO GIL).
        channel_->receive(this->shared_from_this());
    }

    void onMediaIndication(const aasdk::common::DataConstBuffer& buffer) override {
        (void)buffer;
        channel_->receive(this->shared_from_this());
    }

    void onChannelError(const aasdk::error::Error& e) override {
        std::cerr << "[Video] Channel Error: " << e.what() << std::endl;
    }

private:
    // Invia VideoFocusNotification(focus=PROJECTED, unsolicited=false)
    // Chiamata sia dopo AVChannelSetupResponse che dopo ChannelOpenResponse.
    // Ref: VideoMediaSinkService-14.cpp::sendVideoFocusIndication()
    void sendVideoFocusIndication() {
        aap_protobuf::service::media::video::message::VideoFocusNotification vf;
        vf.set_focus(
            aap_protobuf::service::media::video::message::VideoFocusMode::VIDEO_FOCUS_PROJECTED);
        vf.set_unsolicited(false);
        channel_->sendVideoFocusIndication(vf, makePromise("Video/VideoFocusIndication"));
    }

    boost::asio::io_service::strand& strand_;
    aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel_;
    std::shared_ptr<IOrchestrator> orchestrator_;
};

} // namespace nemo
