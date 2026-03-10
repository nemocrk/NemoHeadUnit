#pragma once

#include <iostream>
#include <fstream>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkService.hpp>
#include <aasdk/Channel/Promise.hpp>
#include "iorchestrator.hpp"
#include "gst/gst_video_sink.hpp"

namespace nemo
{

    class VideoEventHandler
        : public aasdk::channel::mediasink::video::IVideoMediaSinkServiceEventHandler,
          public std::enable_shared_from_this<VideoEventHandler>
    {
    public:
        using Pointer = std::shared_ptr<VideoEventHandler>;

        explicit VideoEventHandler(
            boost::asio::io_service::strand &strand,
            aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel,
            std::shared_ptr<IOrchestrator> orchestrator,
            std::shared_ptr<GstVideoSink> video_sink = nullptr)
            : strand_(strand),
              channel_(std::move(channel)),
              orchestrator_(std::move(orchestrator)),
              video_sink_(std::move(video_sink))
        {
        }

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
            std::string res_str = orchestrator_->onAvChannelSetupRequest(
                aasdk::messenger::ChannelId::MEDIA_SINK_VIDEO, req_str);

            aap_protobuf::service::media::shared::message::Config config_resp;
            if (!config_resp.ParseFromString(res_str))
            {
                std::cerr << "[Video] AVChannelSetupResponse parse FAILED" << std::endl;
                channel_->receive(this->shared_from_this());
                return;
            }

            auto setup_promise = aasdk::channel::SendPromise::defer(strand_);
            auto self = this->shared_from_this();
            setup_promise->then(
                [self]() { self->sendVideoFocusIndication(); },
                [](const aasdk::error::Error &e)
                {
                    std::cerr << "[Video] sendChannelSetupResponse FAILED: " << e.what() << std::endl;
                });
            channel_->sendChannelSetupResponse(config_resp, std::move(setup_promise));
            channel_->receive(this->shared_from_this());
        }

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
            std::string open_res = orchestrator_->onChannelOpenRequest(
                aasdk::messenger::ChannelId::MEDIA_SINK_VIDEO, req_str);

            aap_protobuf::service::control::message::ChannelOpenResponse open_resp;
            if (!open_resp.ParseFromString(open_res))
            {
                open_resp.set_status(static_cast<decltype(open_resp.status())>(0));
            }
            channel_->sendChannelOpenResponse(open_resp, makePromise("Video/ChannelOpenResponse"));
            orchestrator_->onVideoChannelOpenRequest(req_str);
            channel_->receive(this->shared_from_this());
        }

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
                    channel_->sendVideoFocusIndication(vf, makePromise("Video/VideoFocusIndication"));
            }
            channel_->receive(this->shared_from_this());
        }

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
            if (dump_file_.is_open())
            {
                dump_file_.close();
                std::cout << "[Video] Dump file chiuso." << std::endl;
            }
            channel_->receive(this->shared_from_this());
        }

        // -----------------------------------------------------------------------
        // onMediaWithTimestampIndication — NAL units con timestamp (P/B frames)
        // -----------------------------------------------------------------------
        void onMediaWithTimestampIndication(
            aasdk::messenger::Timestamp::ValueType ts,
            const aasdk::common::DataConstBuffer &buffer) override
        {
            processBuffer(ts, buffer);
            channel_->receive(this->shared_from_this());
        }

        // -----------------------------------------------------------------------
        // onMediaIndication — NAL units SENZA timestamp.
        // BUG STORICO: questo callback trasportava il primo pacchetto
        // SPS+PPS+IDR (size ~2508 bytes, ts=0) che veniva scartato con
        // (void)buffer. Fix: instrada su processBuffer() con ts=0,
        // identico a come fa openauto/VideoService::onAVMediaIndication().
        // -----------------------------------------------------------------------
        void onMediaIndication(const aasdk::common::DataConstBuffer &buffer) override
        {
            processBuffer(0, buffer);
            channel_->receive(this->shared_from_this());
        }

        void onChannelError(const aasdk::error::Error &e) override
        {
            std::cerr << "[Video] Channel Error: " << e.what() << std::endl;
        }

        void enableDump(const std::string &path)
        {
            dump_file_.open(path, std::ios::binary | std::ios::trunc);
            if (dump_file_.is_open())
            {
                dump_enabled_ = true;
                dump_bytes_   = 0;
                std::cout << "[Video] Dump H.264 abilitato: " << path << std::endl;
            }
            else
            {
                std::cerr << "[Video] ERRORE: impossibile aprire dump file: " << path << std::endl;
            }
        }

    private:
        // -----------------------------------------------------------------------
        // processBuffer — logica comune a onMediaIndication e
        //                 onMediaWithTimestampIndication.
        // REGOLA GIL: gira nel thread Boost.Asio. NON toccare Python qui.
        // -----------------------------------------------------------------------
        void processBuffer(aasdk::messenger::Timestamp::ValueType ts,
                           const aasdk::common::DataConstBuffer &buffer)
        {
            // Log NAL type per i primi 5 pacchetti (debug)
            if (nal_count_ < 5)
            {
                uint8_t nal_type = (buffer.size > 4) ? (buffer.cdata[4] & 0x1Fu) : 0xFFu;
                std::cout << "[Video] NAL #" << nal_count_
                          << " type=" << static_cast<int>(nal_type)
                          << " (7=SPS,8=PPS,5=IDR,1=slice)"
                          << " ts=" << ts
                          << " size=" << buffer.size
                          << " dump_open=" << dump_file_.is_open()
                          << std::endl;
                ++nal_count_;
            }
            else
            {
                std::cout << "[Video] NAL unit ts=" << ts
                          << " size=" << buffer.size << " bytes" << std::endl;
            }

            // ── Dump H.264 ────────────────────────────────────────────────
            if (dump_enabled_ && dump_file_.is_open() && dump_bytes_ < DUMP_LIMIT_)
            {
                dump_file_.write(
                    reinterpret_cast<const char *>(buffer.cdata),
                    static_cast<std::streamsize>(buffer.size));
                dump_bytes_ += buffer.size;

                if (dump_bytes_ >= DUMP_LIMIT_)
                {
                    dump_file_.close();
                    dump_enabled_ = false;
                    std::cout << "[Video] Dump completato ("
                              << DUMP_LIMIT_ / 1024 << " KB). "
                              << "Verifica: vlc --demux h264 video_dump.h264"
                              << std::endl;
                }
            }

            // ── Push a GStreamer (zero-copy, NO GIL) ──────────────────────
            if (video_sink_ && video_sink_->isRunning())
            {
                video_sink_->pushBuffer(
                    static_cast<uint64_t>(ts),
                    buffer.cdata,
                    buffer.size);
            }
        }

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
        std::shared_ptr<GstVideoSink> video_sink_;

        // ── Dump H.264 ────────────────────────────────────────────────────
        std::ofstream dump_file_;
        bool          dump_enabled_ = false;
        std::size_t   dump_bytes_   = 0;
        std::size_t   nal_count_    = 0;
        static constexpr std::size_t DUMP_LIMIT_ = 5UL * 1024UL * 1024UL; // 5 MB
    };

} // namespace nemo
