#pragma once

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>
#include <gst/video/videooverlay.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include "app/core/logging.hpp"

namespace nemo
{

    enum class OverrunPolicy
    {
        DROP_OLD,
        DROP_NEW,
        PROGRESSIVE_DISCARD
    };

    enum class UnderrunPolicy
    {
        WAIT,
        SILENCE
    };

    struct AvCoreConfig
    {
        int jitter_buffer_ms = 120;
        int max_queue_frames = 64;
        int audio_frame_ms = 20;
        int max_av_lead_ms = 80;
        int mic_frame_ms = 20;
        int mic_batch_ms = 100;
        int audio_prebuffer_ms = 100;
        OverrunPolicy overrun_policy = OverrunPolicy::DROP_OLD;
        UnderrunPolicy underrun_policy = UnderrunPolicy::SILENCE;
    };

    struct AvFrame
    {
        uint64_t ts_us = 0;
        std::vector<uint8_t> data;
    };

    struct AudioGroup
    {
        std::vector<int> channels;
        int priority = 0;
        int ducking = 100; // percent (100 = full volume)
        int hold_ms = 100;
        std::vector<float> channel_gains; // optional per-channel gain (1.0 = 100%)
    };

    struct AudioStreamFormat
    {
        int sample_rate = 16000;
        int channels = 1;
        int bits = 16;
        std::string codec = "PCM";
    };

    inline std::string normalizeCodecName(const std::string &codec)
    {
        std::string out;
        out.reserve(codec.size());
        for (char c : codec)
        {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        // Normalize common variants
        if (out == "AAC" || out == "AACLC" || out == "AAC_LC")
            return "AAC_LC";
        if (out == "OPUS")
            return "OPUS";
        if (out == "PCM" || out == "PCM16" || out == "PCM_S16LE")
            return "PCM";
        return out;
    }

    inline bool isPcmCodec(const std::string &codec)
    {
        return normalizeCodecName(codec) == "PCM";
    }

    class GstVideoPipeline
    {
    public:
        void init(uintptr_t window_id, int width, int height)
        {
            init_gst_once();

            const char *env_dec = std::getenv("NEMO_VIDEO_DECODER");
            std::string decoder = (env_dec && *env_dec) ? std::string(env_dec) : "avdec_h264 max-threads=2";

            const char *env_sink = std::getenv("NEMO_VIDEO_SINK");
            std::string sink_elem = (env_sink && *env_sink)
                                        ? std::string(env_sink)
                                        : (window_id ? "xvimagesink" : "autovideosink");

            std::string pipe_desc =
                "appsrc name=src format=time is-live=true "
                "caps=video/x-h264,stream-format=byte-stream,alignment=au,framerate=30/1,"
                "width=" +
                std::to_string(width) + ",height=" + std::to_string(height) + " "
                                                                              "! queue name=q max-size-buffers=8 leaky=downstream "
                                                                              "! h264parse config-interval=-1 "
                                                                              "! " +
                decoder + " "
                          "! videoconvert "
                          "! " +
                sink_elem + " name=sink sync=false";

            GError *err = nullptr;
            pipeline_ = gst_parse_launch(pipe_desc.c_str(), &err);
            if (!pipeline_ || err)
            {
                std::string msg = err ? err->message : "unknown";
                if (err)
                    g_error_free(err);
                throw std::runtime_error("[AvCore] gst_parse_launch video failed: " + msg);
            }

            appsrc_ = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline_), "src"));
            if (!appsrc_)
                throw std::runtime_error("[AvCore] video appsrc not found");

            if (window_id)
            {
                GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
                if (sink)
                {
                    if (GST_IS_VIDEO_OVERLAY(sink))
                    {
                        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sink), window_id);
                    }
                    gst_object_unref(sink);
                }
            }

            GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
            if (ret == GST_STATE_CHANGE_FAILURE)
            {
                stop();
                throw std::runtime_error("[AvCore] video pipeline start failed");
            }
            running_.store(true);
        }

        void push(uint64_t ts_us, const uint8_t *data, std::size_t size)
        {
            if (!running_.load() || !appsrc_)
                return;
            GstBuffer *buf = gst_buffer_new_memdup(data, static_cast<gsize>(size));
            GST_BUFFER_PTS(buf) = static_cast<GstClockTime>(ts_us) * GST_USECOND;
            GST_BUFFER_DTS(buf) = GST_BUFFER_PTS(buf);
            GstFlowReturn ret = gst_app_src_push_buffer(appsrc_, buf);
            if (ret != GST_FLOW_OK)
            {
                APP_LOG_WARN("app.av_core.audio")
                    << "appsrc push returned " << static_cast<int>(ret);
            }
        }

        void stop()
        {
            running_.store(false);
            if (appsrc_)
            {
                gst_app_src_end_of_stream(appsrc_);
                gst_object_unref(GST_OBJECT(appsrc_));
                appsrc_ = nullptr;
            }
            if (pipeline_)
            {
                gst_element_set_state(pipeline_, GST_STATE_NULL);
                gst_object_unref(pipeline_);
                pipeline_ = nullptr;
            }
        }

        bool isRunning() const { return running_.load(); }

    private:
        static void init_gst_once()
        {
            static std::once_flag flag;
            std::call_once(flag, []()
                           { gst_init(nullptr, nullptr); });
        }

        GstElement *pipeline_ = nullptr;
        GstAppSrc *appsrc_ = nullptr;
        std::atomic<bool> running_{false};
    };

    class GstAudioPipeline
    {
    public:
        void init(int sample_rate, int channels, int bits, const std::string &codec)
        {
            init_gst_once();

            const char *env_sink = std::getenv("NEMO_AUDIO_SINK");
            std::string sink_elem = (env_sink && *env_sink) ? std::string(env_sink) : "autoaudiosink";
            int buffer_ms = 100;
            if (const char *env_buf = std::getenv("NEMO_AUDIO_BUFFER_MS"))
            {
                try
                {
                    buffer_ms = std::max(0, std::stoi(env_buf));
                }
                catch (...)
                {
                    buffer_ms = 100;
                }
            }

            const std::string codec_norm = normalizeCodecName(codec);
            const bool pcm = (codec_norm == "PCM");
            sample_rate_ = sample_rate;
            channels_ = channels;
            bits_ = bits;
            codec_norm_ = codec_norm;

            if (bits != 16)
            {
                APP_LOG_WARN("app.av_core.audio")
                    << "only 16-bit PCM supported in mixer; forcing S16LE";
            }
            std::string format = "S16LE";
            std::string caps;
            std::string decoder;

            if (pcm)
            {
                caps = "audio/x-raw,format=" + format + ",channels=" + std::to_string(channels) +
                       ",rate=" + std::to_string(sample_rate);
            }
            else if (codec_norm == "AAC_LC")
            {
                caps = "audio/mpeg,mpegversion=4,stream-format=raw,framed=true,channels=" + std::to_string(channels) +
                       ",rate=" + std::to_string(sample_rate);
                decoder = "avdec_aac";
            }
            else if (codec_norm == "OPUS")
            {
                caps = "audio/x-opus,channels=" + std::to_string(channels) +
                       ",rate=" + std::to_string(sample_rate);
                decoder = "opusparse ! opusdec";
            }
            else
            {
                // Fallback: try decodebin with the provided caps
                caps = "audio/x-raw,format=" + format + ",channels=" + std::to_string(channels) +
                       ",rate=" + std::to_string(sample_rate);
                decoder = "decodebin";
            }
            caps_base_ = caps;

            const char *env_dec = std::getenv("NEMO_AUDIO_DECODER");
            if (env_dec && *env_dec)
            {
                decoder = std::string(env_dec);
            }

            std::string caps_full = buildCapsString();
            std::string queue_desc = "queue name=q max-size-buffers=32 leaky=downstream";
            if (buffer_ms > 0)
            {
                const int64_t max_time_ns = static_cast<int64_t>(buffer_ms) * 1000 * 1000;
                queue_desc += " max-size-time=" + std::to_string(max_time_ns) + " max-size-bytes=0";
            }

            std::string pipe_desc =
                "appsrc name=src format=time is-live=true "
                "caps=" +
                caps_full + " "
                            "! " +
                queue_desc + " ";
            if (!decoder.empty() && !pcm)
            {
                pipe_desc += "! " + decoder + " ";
            }
            pipe_desc += "! audioconvert ! audioresample ! " + sink_elem + " sync=false";

            APP_LOG_INFO("app.av_core.audio") << "Gst pipeline: " << pipe_desc;
            APP_LOG_INFO("app.av_core.audio")
                << "codec=" << codec_norm << " pcm=" << (pcm ? "yes" : "no")
                << " caps=" << caps_full << " decoder=" << (decoder.empty() ? "<none>" : decoder)
                << " sink=" << sink_elem;

            GError *err = nullptr;
            pipeline_ = gst_parse_launch(pipe_desc.c_str(), &err);
            if (!pipeline_ || err)
            {
                std::string msg = err ? err->message : "unknown";
                if (err)
                    g_error_free(err);
                throw std::runtime_error("[AvCore] gst_parse_launch audio failed: " + msg);
            }

            appsrc_ = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline_), "src"));
            if (!appsrc_)
                throw std::runtime_error("[AvCore] audio appsrc not found");
            applyCapsLocked();
            g_object_set(G_OBJECT(appsrc_), "block", FALSE, "max-bytes", 0, NULL);

            GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
            if (ret == GST_STATE_CHANGE_FAILURE)
            {
                stop();
                throw std::runtime_error("[AvCore] audio pipeline start failed");
            }
            logPipelineState();
            logNegotiatedCaps();
            running_.store(true);
        }

        void push(uint64_t ts_us, const uint8_t *data, std::size_t size)
        {
            if (!running_.load() || !appsrc_)
                return;
            std::lock_guard<std::mutex> lock(mutex_);
            if (caps_dirty_.exchange(false))
            {
                applyCapsLocked();
                APP_LOG_INFO("app.av_core.audio") << "updated appsrc caps=" << buildCapsString();
            }
            if (size == 0)
            {
                APP_LOG_WARN("app.av_core.audio") << "push called with size=0";
                return;
            }
            static int push_count = 0;
            ++push_count;
            if (push_count % 1000 == 0)
            {
                APP_LOG_TRACE("app.av_core.audio")
                    << "push frame #" << push_count
                    << " ts=" << ts_us << " size=" << size;
            }
            GstBuffer *buf = gst_buffer_new_memdup(data, static_cast<gsize>(size));
            GST_BUFFER_PTS(buf) = static_cast<GstClockTime>(ts_us) * GST_USECOND;
            GST_BUFFER_DTS(buf) = GST_BUFFER_PTS(buf);
            gst_app_src_push_buffer(appsrc_, buf);
        }

        void stop()
        {
            running_.store(false);
            std::lock_guard<std::mutex> lock(mutex_);
            if (appsrc_)
            {
                gst_app_src_end_of_stream(appsrc_);
                gst_object_unref(GST_OBJECT(appsrc_));
                appsrc_ = nullptr;
            }
            if (pipeline_)
            {
                gst_element_set_state(pipeline_, GST_STATE_NULL);
                gst_object_unref(pipeline_);
                pipeline_ = nullptr;
            }
        }

        bool isRunning() const { return running_.load(); }

        void setCodecData(const std::vector<uint8_t> &codec_data)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            codec_data_ = codec_data;
            caps_dirty_.store(true);
        }

    private:
        std::string buildCapsString() const
        {
            std::string caps = caps_base_;
            if (!codec_data_.empty())
            {
                caps += ",codec_data=(buffer)" + bytesToHex(codec_data_);
            }
            return caps;
        }

        void applyCapsLocked()
        {
            if (!appsrc_ || caps_base_.empty())
                return;
            GstCaps *caps = gst_caps_from_string(buildCapsString().c_str());
            if (caps)
            {
                gst_app_src_set_caps(appsrc_, caps);
                gst_caps_unref(caps);
            }
        }

        static std::string bytesToHex(const std::vector<uint8_t> &data)
        {
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (uint8_t byte : data)
            {
                oss << std::setw(2) << static_cast<int>(byte);
            }
            return oss.str();
        }

        void logPipelineState()
        {
            if (!pipeline_)
                return;
            GstState current = GST_STATE_NULL;
            GstState pending = GST_STATE_NULL;
            GstStateChangeReturn ret = gst_element_get_state(
                pipeline_, &current, &pending, 2 * GST_SECOND);
            APP_LOG_INFO("app.av_core.audio")
                << "pipeline state ret=" << static_cast<int>(ret)
                << " current=" << gst_element_state_get_name(current)
                << " pending=" << gst_element_state_get_name(pending);
        }

        static std::string capsToString(GstCaps *caps)
        {
            if (!caps)
                return "<null>";
            gchar *caps_str = gst_caps_to_string(caps);
            std::string out = caps_str ? caps_str : "<null>";
            if (caps_str)
                g_free(caps_str);
            return out;
        }

        void logNegotiatedCaps()
        {
            if (!pipeline_)
                return;
            GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
            if (appsrc_)
            {
                GstCaps *caps = gst_app_src_get_caps(appsrc_);
                APP_LOG_INFO("app.av_core.audio")
                    << "appsrc caps=" << capsToString(caps);
                if (caps)
                    gst_caps_unref(caps);
            }
            if (sink)
            {
                GstPad *pad = gst_element_get_static_pad(sink, "sink");
                if (pad)
                {
                    GstCaps *caps = gst_pad_get_current_caps(pad);
                    APP_LOG_INFO("app.av_core.audio")
                        << "sink caps=" << capsToString(caps);
                    if (caps)
                        gst_caps_unref(caps);
                    gst_object_unref(pad);
                }
                gst_object_unref(sink);
            }
        }

        static void init_gst_once()
        {
            static std::once_flag flag;
            std::call_once(flag, []()
                           { gst_init(nullptr, nullptr); });
        }

        GstElement *pipeline_ = nullptr;
        GstAppSrc *appsrc_ = nullptr;
        std::atomic<bool> running_{false};
        std::mutex mutex_;
        int sample_rate_{0};
        int channels_{0};
        int bits_{0};
        std::string codec_norm_;
        std::string caps_base_;
        std::vector<uint8_t> codec_data_;
        std::atomic<bool> caps_dirty_{false};
    };

    class GstMicCapture
    {
    public:
        void init(int sample_rate, int channels, int bits, int frame_ms)
        {
            init_gst_once();

            const char *env_src = std::getenv("NEMO_MIC_SRC");
            std::string src_elem = (env_src && *env_src) ? std::string(env_src) : "autoaudiosrc";
            std::string format = (bits == 16) ? "S16LE" : "S16LE";
            std::string base_desc =
                src_elem + " "
                           "! audioconvert ! audioresample "
                           "! audio/x-raw,format=" +
                format + ",channels=" + std::to_string(channels) + ",rate=" + std::to_string(sample_rate) + " ";

            auto build_desc = [&](bool with_split) -> std::string
            {
                std::string desc = base_desc;
                if (with_split && frame_ms > 0)
                {
                    // Force timestamps for autoaudiosrc too (it doesn't expose do-timestamp).
                    desc += "! identity do-timestamp=true ";
                    desc += "! audiobuffersplit output-buffer-duration=" + std::to_string(frame_ms * 1000000LL) + " ";
                    desc += "! queue max-size-buffers=8 leaky=downstream ";
                }
                desc += "! appsink name=sink sync=false max-buffers=4 drop=true";
                return desc;
            };

            std::string pipe_desc = build_desc(true);
            APP_LOG_INFO("app.av_core.audio")
                << "Mic pipeline: " << pipe_desc;

            GError *err = nullptr;
            pipeline_ = gst_parse_launch(pipe_desc.c_str(), &err);
            if (!pipeline_ || err)
            {
                std::string msg = err ? err->message : "unknown";
                if (err)
                    g_error_free(err);
                if (pipeline_)
                {
                    gst_object_unref(pipeline_);
                    pipeline_ = nullptr;
                }
                if (frame_ms > 0)
                {
                    std::string fallback = build_desc(false);
                    APP_LOG_WARN("app.av_core.audio")
                        << "Mic pipeline failed with splitter, retrying without: " << msg;
                    APP_LOG_INFO("app.av_core.audio")
                        << "Mic pipeline (fallback): " << fallback;
                    err = nullptr;
                    pipeline_ = gst_parse_launch(fallback.c_str(), &err);
                    if (!pipeline_ || err)
                    {
                        std::string msg2 = err ? err->message : "unknown";
                        if (err)
                            g_error_free(err);
                        if (pipeline_)
                        {
                            gst_object_unref(pipeline_);
                            pipeline_ = nullptr;
                        }
                        throw std::runtime_error("[AvCore] gst_parse_launch mic failed: " + msg2);
                    }
                }
                else
                {
                    throw std::runtime_error("[AvCore] gst_parse_launch mic failed: " + msg);
                }
            }

            appsink_ = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(pipeline_), "sink"));
            if (!appsink_)
                throw std::runtime_error("[AvCore] mic appsink not found");

            gst_app_sink_set_emit_signals(appsink_, FALSE);
            gst_app_sink_set_drop(appsink_, TRUE);
            gst_app_sink_set_max_buffers(appsink_, 4);

            GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
            if (ret == GST_STATE_CHANGE_FAILURE)
            {
                stop();
                throw std::runtime_error("[AvCore] mic pipeline start failed");
            }
            running_.store(true);
        }

        bool isRunning() const { return running_.load(); }

        bool pull(AvFrame &out, int timeout_ms)
        {
            if (!running_.load() || !appsink_)
                return false;

            GstSample *sample = gst_app_sink_try_pull_sample(appsink_, timeout_ms * GST_MSECOND);
            if (!sample)
                return false;

            GstBuffer *buffer = gst_sample_get_buffer(sample);
            GstMapInfo map;
            if (!buffer || !gst_buffer_map(buffer, &map, GST_MAP_READ))
            {
                gst_sample_unref(sample);
                return false;
            }

            out.data.assign(map.data, map.data + map.size);

            if (GST_BUFFER_PTS_IS_VALID(buffer))
            {
                out.ts_us = static_cast<uint64_t>(GST_BUFFER_PTS(buffer) / GST_USECOND);
            }

            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            return true;
        }

        void stop()
        {
            running_.store(false);
            if (appsink_)
            {
                gst_object_unref(GST_OBJECT(appsink_));
                appsink_ = nullptr;
            }
            if (pipeline_)
            {
                gst_element_set_state(pipeline_, GST_STATE_NULL);
                gst_object_unref(pipeline_);
                pipeline_ = nullptr;
            }
        }

    private:
        static void init_gst_once()
        {
            static std::once_flag flag;
            std::call_once(flag, []()
                           { gst_init(nullptr, nullptr); });
        }

        GstElement *pipeline_ = nullptr;
        GstAppSink *appsink_ = nullptr;
        std::atomic<bool> running_{false};
    };

    class AvCore
    {
    public:
        explicit AvCore(AvCoreConfig cfg = {})
            : cfg_(cfg)
        {
        }

        ~AvCore()
        {
            stop();
        }

        void configureVideo(int width, int height)
        {
            video_width_ = width;
            video_height_ = height;
        }

        void setWindowId(uintptr_t wid)
        {
            window_id_ = wid;
        }

        void startVideo()
        {
            if (video_running_.load())
                return;
            video_pipeline_.init(window_id_, video_width_, video_height_);
            video_running_.store(true);
            video_thread_ = std::thread([this]()
                                        { videoLoop(); });
        }

        void stopVideo()
        {
            video_running_.store(false);
            video_cv_.notify_all();
            if (video_thread_.joinable())
                video_thread_.join();
            video_pipeline_.stop();
            clearQueue(video_queue_, video_mutex_);
        }

        void configureAudio(int sample_rate, int channels, int bits, const std::string &codec = "PCM")
        {
            audio_sample_rate_ = sample_rate;
            audio_channels_ = channels;
            audio_bits_ = bits;
            audio_codec_ = normalizeCodecName(codec);
            APP_LOG_INFO("app.av_core.audio")
                << "configureAudio: " << sample_rate << "Hz " << channels << "ch "
                << bits << "bit codec=" << audio_codec_;
        }

        void configureAudioStream(int stream_id, int sample_rate, int channels, int bits, const std::string &codec = "PCM")
        {
            AudioStreamFormat fmt;
            fmt.sample_rate = sample_rate;
            fmt.channels = channels;
            fmt.bits = bits;
            fmt.codec = normalizeCodecName(codec);
            {
                std::lock_guard<std::mutex> lock(audio_mutex_);
                audio_stream_formats_[stream_id] = fmt;
            }
            APP_LOG_INFO("app.av_core.audio")
                << "configureAudioStream: stream=" << stream_id
                << " " << sample_rate << "Hz " << channels << "ch "
                << bits << "bit codec=" << fmt.codec;
        }

        void startAudio()
        {
            if (audio_running_.load())
                return;

            // Optional recording of the incoming stream (useful to verify incoming format)
            const char *dump_path = std::getenv("NEMO_AUDIO_DUMP");
            {
                std::lock_guard<std::mutex> lock(audio_dump_mutex_);
                closeAudioDumpFilesLocked();
                audio_dump_path_.clear();
                audio_dump_single_ = false;
                if (dump_path && *dump_path)
                {
                    audio_dump_path_ = dump_path;
                    const char *dump_single = std::getenv("NEMO_AUDIO_DUMP_SINGLE");
                    if (dump_single && *dump_single)
                    {
                        audio_dump_single_ = envTruthy(dump_single);
                    }
                    else
                    {
                        // Auto-detect: file path -> single; directory or {stream} -> per-stream
                        std::string path = audio_dump_path_;
                        if (path.find("{stream}") != std::string::npos)
                        {
                            audio_dump_single_ = false;
                        }
                        else
                        {
                            std::error_code ec;
                            if (std::filesystem::is_directory(path, ec))
                            {
                                audio_dump_single_ = false;
                            }
                            else if (!path.empty() && (path.back() == '/' || path.back() == '\\'))
                            {
                                audio_dump_single_ = false;
                            }
                            else
                            {
                                audio_dump_single_ = true;
                            }
                        }
                    }
                    APP_LOG_INFO("app.av_core.audio")
                        << "dumping incoming audio stream to "
                        << audio_dump_path_
                        << (audio_dump_single_ ? " (single file)" : " (per stream)");
                }
            }

            APP_LOG_INFO("app.av_core.audio")
                << "startAudio: sample_rate=" << audio_sample_rate_
                << " channels=" << audio_channels_ << " bits=" << audio_bits_
                << " codec=" << audio_codec_;
            audio_pipeline_.init(audio_sample_rate_, audio_channels_, audio_bits_, audio_codec_);
            if (!audio_codec_data_.empty())
            {
                audio_pipeline_.setCodecData(audio_codec_data_);
            }
            {
                std::lock_guard<std::mutex> lock(audio_mutex_);
                last_audio_ts_.store(0);
                last_audio_ts_per_stream_.clear();
            }
            audio_running_.store(true);
            audio_thread_ = std::thread([this]()
                                        { audioLoop(); });
        }

        void stopAudio()
        {
            audio_running_.store(false);
            audio_cv_.notify_all();
            if (audio_thread_.joinable())
                audio_thread_.join();
            audio_pipeline_.stop();
            clearAudioQueues();
            audio_codec_data_.clear();
            {
                std::lock_guard<std::mutex> lock(audio_dump_mutex_);
                closeAudioDumpFilesLocked();
                audio_dump_path_.clear();
                audio_dump_single_ = false;
            }
        }

        void startMicCapture()
        {
            if (mic_running_.load())
                return;
            int frame_ms = cfg_.mic_frame_ms > 0 ? cfg_.mic_frame_ms : 0;
            APP_LOG_INFO("app.av_core.audio")
                << "startMicCapture: rate=" << mic_sample_rate_
                << " ch=" << mic_channels_
                << " bits=" << mic_bits_
                << " frame_ms=" << frame_ms
                << " batch_ms=" << cfg_.mic_batch_ms;
            mic_capture_.init(mic_sample_rate_, mic_channels_, mic_bits_, frame_ms);
            mic_running_.store(true);
            mic_thread_ = std::thread([this]()
                                      { micLoop(); });
        }

        void stopMicCapture()
        {
            mic_running_.store(false);
            mic_cv_.notify_all();
            if (mic_thread_.joinable())
                mic_thread_.join();
            mic_capture_.stop();
            clearQueue(mic_queue_, mic_mutex_);
        }

        void configureMic(int sample_rate, int channels, int bits)
        {
            mic_sample_rate_ = sample_rate;
            mic_channels_ = channels;
            mic_bits_ = bits;
        }

        void setMicActive(bool active)
        {
            mic_active_.store(active);
        }

        bool isMicActive() const
        {
            return mic_active_.load();
        }

        void stop()
        {
            stopVideo();
            stopAudio();
            stopMicCapture();
        }

        void pushVideo(uint64_t ts_us, const uint8_t *data, std::size_t size)
        {
            if (!video_running_.load())
                return;
            AvFrame f;
            f.ts_us = ts_us;
            f.data.assign(data, data + size);
            {
                std::lock_guard<std::mutex> lock(video_mutex_);
                if (!applyOverrunPolicy(video_queue_))
                    return;
                video_queue_.push_back(std::move(f));
            }
            video_cv_.notify_one();
        }

        void pushAudio(int stream_id, uint64_t ts_us, const uint8_t *data, std::size_t size)
        {
            if (!audio_running_.load())
                return;
            static int in_log_count = 0;
            if (ts_us == 0 || ++in_log_count % 200 == 0)
            {
                APP_LOG_DEBUG("app.av_core.audio")
                    << "incoming stream=" << stream_id
                    << " ts=" << ts_us
                    << " size=" << size
                    << " head=" << bytesToHex(data, size, 16);
            }
            if (data && size > 0)
            {
                writeAudioDump(stream_id, data, size);
            }
            AvFrame f;
            f.ts_us = ts_us;
            f.data.assign(data, data + size);

            // Normalize PCM format per stream before mixing (if configured).
            AudioStreamFormat fmt = getStreamFormat(stream_id);
            const bool stream_pcm = isPcmCodec(fmt.codec);
            const bool output_pcm = isPcmCodec(audio_codec_);
            if (!output_pcm && !stream_pcm && size == 2 && normalizeCodecName(fmt.codec) == "AAC_LC")
            {
                std::vector<uint8_t> codec_data(data, data + size);
                if (codec_data != audio_codec_data_)
                {
                    audio_codec_data_ = codec_data;
                    audio_pipeline_.setCodecData(audio_codec_data_);
                    APP_LOG_INFO("app.av_core.audio")
                        << "captured AAC codec_data=" << bytesToHex(data, size, size)
                        << " stream=" << stream_id;
                }
                return;
            }
            if (output_pcm)
            {
                if (!stream_pcm)
                {
                    static int codec_mismatch_count = 0;
                    if (++codec_mismatch_count % 200 == 0)
                    {
                        APP_LOG_WARN("app.av_core.audio")
                            << "codec mismatch: stream=" << fmt.codec
                            << " output=" << audio_codec_ << " (dropping frames)";
                    }
                    return;
                }
                if (!normalizePcmFrame(f, fmt))
                {
                    return;
                }
            }
            {
                std::lock_guard<std::mutex> lock(audio_mutex_);
                auto &q = audio_queues_[stream_id];
                if (!applyOverrunPolicy(q))
                    return;
                q.push_back(std::move(f));

                static int queue_log_count = 0;
                if (++queue_log_count % 500 == 0)
                {
                    APP_LOG_DEBUG("app.av_core.audio")
                        << "queue status: stream=" << stream_id
                        << " size=" << q.size() << " total_streams=" << audio_queues_.size();
                }
            }
            audio_cv_.notify_one();
        }

        bool popMicFrame(AvFrame &out, int timeout_ms)
        {
            std::unique_lock<std::mutex> lock(mic_mutex_);
            if (!mic_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]()
                                  { return !mic_queue_.empty() || !mic_running_.load(); }))
            {
                return false;
            }
            if (mic_queue_.empty())
                return false;
            out = std::move(mic_queue_.front());
            mic_queue_.pop_front();
            return true;
        }

        uintptr_t ptr() const
        {
            return reinterpret_cast<uintptr_t>(this);
        }

        void setAudioPriority(const std::vector<int> &priority)
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            audio_priority_ = priority;
            audio_groups_.clear();
        }

        void setAudioGroups(const std::vector<AudioGroup> &groups)
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            audio_groups_ = groups;
            audio_priority_.clear();
        }

        void setPolicies(OverrunPolicy overrun, UnderrunPolicy underrun)
        {
            cfg_.overrun_policy = overrun;
            cfg_.underrun_policy = underrun;
        }

        void setJitterBufferMs(int ms) { cfg_.jitter_buffer_ms = ms; }
        void setMaxQueueFrames(int frames) { cfg_.max_queue_frames = frames; }
        void setAudioFrameMs(int ms) { cfg_.audio_frame_ms = ms; }
        void setMaxAvLeadMs(int ms) { cfg_.max_av_lead_ms = ms; }

    private:
        static bool envTruthy(const char *value)
        {
            if (!value || !*value)
                return false;
            std::string s(value);
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c)
                           { return static_cast<char>(std::tolower(c)); });
            return (s == "1" || s == "true" || s == "yes" || s == "on");
        }

        AudioStreamFormat getStreamFormat(int stream_id)
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            auto it = audio_stream_formats_.find(stream_id);
            if (it != audio_stream_formats_.end())
                return it->second;
            AudioStreamFormat fmt;
            fmt.sample_rate = audio_sample_rate_;
            fmt.channels = audio_channels_;
            fmt.bits = audio_bits_;
            fmt.codec = audio_codec_;
            return fmt;
        }

        AudioStreamFormat getStreamFormatLocked(int stream_id) const
        {
            auto it = audio_stream_formats_.find(stream_id);
            if (it != audio_stream_formats_.end())
                return it->second;
            AudioStreamFormat fmt;
            fmt.sample_rate = audio_sample_rate_;
            fmt.channels = audio_channels_;
            fmt.bits = audio_bits_;
            fmt.codec = audio_codec_;
            return fmt;
        }

        bool normalizePcmFrame(AvFrame &frame, const AudioStreamFormat &fmt)
        {
            if (frame.data.empty())
                return false;
            if (fmt.bits != 16 || audio_bits_ != 16)
            {
                // Only 16-bit PCM is supported in the software mixer for now.
                return true;
            }
            if (audio_sample_rate_ <= 0 || audio_channels_ <= 0 || fmt.sample_rate <= 0)
            {
                return true;
            }
            if (fmt.sample_rate == audio_sample_rate_ && fmt.channels == audio_channels_)
                return true;

            const int16_t *src = reinterpret_cast<const int16_t *>(frame.data.data());
            std::size_t total_samples = frame.data.size() / sizeof(int16_t);
            if (total_samples == 0 || fmt.channels <= 0)
                return false;

            std::size_t in_frames = total_samples / static_cast<std::size_t>(fmt.channels);
            if (in_frames == 0)
                return false;

            // Channel conversion
            std::vector<int16_t> ch_conv;
            if (fmt.channels == audio_channels_)
            {
                ch_conv.assign(src, src + total_samples);
            }
            else if (fmt.channels == 1 && audio_channels_ == 2)
            {
                ch_conv.resize(in_frames * 2);
                for (std::size_t i = 0; i < in_frames; ++i)
                {
                    int16_t v = src[i];
                    ch_conv[i * 2] = v;
                    ch_conv[i * 2 + 1] = v;
                }
            }
            else if (fmt.channels == 2 && audio_channels_ == 1)
            {
                ch_conv.resize(in_frames);
                for (std::size_t i = 0; i < in_frames; ++i)
                {
                    int32_t v = static_cast<int32_t>(src[i * 2]) + static_cast<int32_t>(src[i * 2 + 1]);
                    ch_conv[i] = static_cast<int16_t>(v / 2);
                }
            }
            else
            {
                static int channel_warn_count = 0;
                if (++channel_warn_count % 200 == 0)
                {
                    APP_LOG_ERROR("app.av_core.audio")
                        << "unsupported channel conversion: "
                        << fmt.channels << " -> " << audio_channels_;
                }
                return false;
            }

            std::size_t conv_frames = ch_conv.size() / static_cast<std::size_t>(audio_channels_);

            // Resample if needed (linear interpolation).
            std::vector<int16_t> resampled;
            if (fmt.sample_rate != audio_sample_rate_ && conv_frames > 0)
            {
                const double ratio = static_cast<double>(fmt.sample_rate) / static_cast<double>(audio_sample_rate_);
                std::size_t out_frames = static_cast<std::size_t>(static_cast<double>(conv_frames) / ratio);
                out_frames = std::max<std::size_t>(1, out_frames);
                resampled.resize(out_frames * static_cast<std::size_t>(audio_channels_));

                for (std::size_t of = 0; of < out_frames; ++of)
                {
                    double src_pos = static_cast<double>(of) * ratio;
                    std::size_t idx = static_cast<std::size_t>(src_pos);
                    double frac = src_pos - static_cast<double>(idx);
                    if (idx >= conv_frames)
                        idx = conv_frames - 1;
                    std::size_t idx2 = std::min(idx + 1, conv_frames - 1);

                    for (int ch = 0; ch < audio_channels_; ++ch)
                    {
                        int16_t s0 = ch_conv[idx * audio_channels_ + ch];
                        int16_t s1 = ch_conv[idx2 * audio_channels_ + ch];
                        double v = static_cast<double>(s0) + (static_cast<double>(s1 - s0) * frac);
                        resampled[of * audio_channels_ + ch] = static_cast<int16_t>(v);
                    }
                }
            }
            else
            {
                resampled = std::move(ch_conv);
            }

            // Pad/trim to expected frame size if needed.
            const int frame_bytes = audioBytesPerFrame();
            if (frame_bytes > 0)
            {
                std::size_t expected_samples = frame_bytes / sizeof(int16_t);
                if (resampled.size() < expected_samples)
                {
                    resampled.resize(expected_samples, 0);
                }
                else if (resampled.size() > expected_samples)
                {
                    resampled.resize(expected_samples);
                }
            }

            frame.data.resize(resampled.size() * sizeof(int16_t));
            std::memcpy(frame.data.data(), resampled.data(), frame.data.size());
            return true;
        }

        void writeAudioDump(int stream_id, const uint8_t *data, std::size_t size)
        {
            if (!data || size == 0)
                return;
            std::lock_guard<std::mutex> lock(audio_dump_mutex_);
            if (audio_dump_path_.empty())
                return;

            int file_id = audio_dump_single_ ? 0 : stream_id;
            auto &file = audio_dump_files_[file_id];
            if (!file.is_open())
            {
                const std::string path = resolveDumpPathLocked(stream_id);
                if (!path.empty())
                {
                    file.open(path, std::ios::binary | std::ios::app);
                    if (file.is_open())
                    {
                        APP_LOG_INFO("app.av_core.audio")
                            << "dump stream=" << stream_id
                            << " -> " << path;
                    }
                    else
                    {
                        APP_LOG_ERROR("app.av_core.audio")
                            << "failed to open dump file: " << path;
                    }
                }
            }
            if (file.is_open())
            {
                file.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
            }
        }

        std::string resolveDumpPathLocked(int stream_id) const
        {
            if (audio_dump_path_.empty())
                return "";
            if (audio_dump_single_)
                return audio_dump_path_;

            std::string path = audio_dump_path_;
            const std::string token = "{stream}";
            auto pos = path.find(token);
            if (pos != std::string::npos)
            {
                path.replace(pos, token.size(), std::to_string(stream_id));
                return path;
            }

            std::error_code ec;
            if (std::filesystem::is_directory(path, ec))
            {
                return path + "/audio_stream_" + std::to_string(stream_id) + ".dump";
            }
            if (!path.empty() && (path.back() == '/' || path.back() == '\\'))
            {
                return path + "audio_stream_" + std::to_string(stream_id) + ".dump";
            }

            return path + ".stream_" + std::to_string(stream_id);
        }

        void closeAudioDumpFilesLocked()
        {
            for (auto &kv : audio_dump_files_)
            {
                if (kv.second.is_open())
                    kv.second.close();
            }
            audio_dump_files_.clear();
        }

        bool applyOverrunPolicy(std::deque<AvFrame> &q)
        {
            if (static_cast<int>(q.size()) < cfg_.max_queue_frames)
                return true;

            if (cfg_.overrun_policy == OverrunPolicy::DROP_NEW)
            {
                static int drop_new_count = 0;
                if (++drop_new_count % 500 == 0)
                {
                    APP_LOG_WARN("app.av_core.audio")
                        << "overrun: dropping new frame (queue=" << q.size() << ")";
                }
                return false;
            }

            if (cfg_.overrun_policy == OverrunPolicy::DROP_OLD)
            {
                static int drop_old_count = 0;
                if (++drop_old_count % 500 == 0)
                {
                    APP_LOG_WARN("app.av_core.audio")
                        << "overrun: dropping old frame (queue=" << q.size() << ")";
                }
                q.pop_front();
                return true;
            }

            while (static_cast<int>(q.size()) >= cfg_.max_queue_frames / 2 && !q.empty())
            {
                q.pop_front();
            }
            return true;
        }

        void clearQueue(std::deque<AvFrame> &q, std::mutex &m)
        {
            std::lock_guard<std::mutex> lock(m);
            q.clear();
        }

        void clearAudioQueues()
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            audio_queues_.clear();
        }

        void clearAudioQueuesLocked()
        {
            audio_queues_.clear();
        }

        void videoLoop()
        {
            const int min_frames = std::max(1, cfg_.jitter_buffer_ms / 33);
            while (video_running_.load())
            {
                AvFrame frame;
                {
                    std::unique_lock<std::mutex> lock(video_mutex_);
                    video_cv_.wait(lock, [this]()
                                   { return !video_queue_.empty() || !video_running_.load(); });
                    if (!video_running_.load())
                        break;
                    if (static_cast<int>(video_queue_.size()) < min_frames)
                        continue;
                    frame = std::move(video_queue_.front());
                    video_queue_.pop_front();
                }

                if (audio_running_.load())
                {
                    uint64_t audio_ts = last_audio_ts_.load();
                    if (audio_ts && frame.ts_us > audio_ts + static_cast<uint64_t>(cfg_.max_av_lead_ms) * 1000)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                }
                video_pipeline_.push(frame.ts_us, frame.data.data(), frame.data.size());
            }
        }

        int pickAudioStream()
        {
            if (!audio_priority_.empty())
            {
                for (int id : audio_priority_)
                {
                    auto it = audio_queues_.find(id);
                    if (it != audio_queues_.end() && !it->second.empty())
                    {
                        AudioStreamFormat fmt = getStreamFormatLocked(id);
                        if (normalizeCodecName(fmt.codec) != normalizeCodecName(audio_codec_))
                            continue;
                        return id;
                    }
                }
            }
            for (auto &kv : audio_queues_)
            {
                AudioStreamFormat fmt = getStreamFormatLocked(kv.first);
                if (normalizeCodecName(fmt.codec) != normalizeCodecName(audio_codec_))
                    continue;
                if (!kv.second.empty())
                    return kv.first;
            }
            return -1;
        }

        void audioLoop()
        {
            const bool pcm_output = isPcmCodec(audio_codec_);
            const int frame_bytes = pcm_output ? audioBytesPerFrame() : 0;
            while (audio_running_.load())
            {
                int stream_id = -1;
                AvFrame frame;
                bool has_frame = false;
                std::vector<int16_t> mix;
                bool mixed = false;
                uint64_t now_ms = nowMs();
                static uint64_t last_info_ms = 0;
                bool do_info_log = false;
                if (now_ms >= last_info_ms + 1000)
                {
                    last_info_ms = now_ms;
                    do_info_log = true;
                }
                bool mic_blocked = false;
                std::string info_streams;
                std::string prebuffer_info;
                size_t info_total = 0;

                {
                    std::unique_lock<std::mutex> lock(audio_mutex_);
                    audio_cv_.wait_for(lock, std::chrono::milliseconds(cfg_.audio_frame_ms), [this]()
                                       { return hasAudioDataLocked() || !audio_running_.load(); });
                    if (!audio_running_.load())
                        break;

                    if (mic_active_.load())
                    {
                        clearAudioQueuesLocked();
                        mic_blocked = true;
                    }

                    if (do_info_log)
                    {
                        std::ostringstream oss;
                        bool first = true;
                        for (auto &kv : audio_queues_)
                        {
                            if (!first)
                                oss << " ";
                            first = false;
                            oss << kv.first << "=" << kv.second.size();
                            info_total += kv.second.size();
                        }
                        info_streams = oss.str();
                    }

                    if (!pcm_output)
                    {
                        // Encoded path: no software mixing. Pick a single stream by priority.
                        stream_id = pickAudioStream();
                        if (stream_id >= 0)
                        {
                            auto &q = audio_queues_[stream_id];
                            int frame_ms = encodedFrameDurationMsLocked(stream_id);
                            int queued_frames = static_cast<int>(q.size());
                            int queued_ms = frame_ms * queued_frames;
                            int max_frames = std::max(1, cfg_.max_queue_frames);
                            int prebuffer_frames = std::max(1, (cfg_.audio_prebuffer_ms + frame_ms - 1) / frame_ms);
                            if (prebuffer_frames >= max_frames)
                            {
                                prebuffer_frames = std::max(1, max_frames - 1);
                            }
                            if (do_info_log)
                            {
                                std::ostringstream oss;
                                oss << "prebuffer stream=" << stream_id
                                    << " queued_frames=" << queued_frames
                                    << " prebuffer_frames=" << prebuffer_frames
                                    << " max_frames=" << max_frames
                                    << " queued_ms=" << queued_ms
                                    << " target_ms=" << cfg_.audio_prebuffer_ms;
                                prebuffer_info = oss.str();
                            }
                            if (cfg_.audio_prebuffer_ms > 0 && queued_frames < prebuffer_frames)
                            {
                                // Not enough buffered audio yet: wait and keep accumulating.
                            }
                            else
                            {
                                frame = std::move(q.front());
                                q.pop_front();
                                has_frame = true;
                                static int enc_pop_count = 0;
                                if (++enc_pop_count <= 5 || enc_pop_count % 200 == 0)
                                {
                                    APP_LOG_DEBUG("app.av_core.audio")
                                        << "pop encoded stream=" << stream_id
                                        << " size=" << frame.data.size();
                                }
                            }
                        }
                    }
                    else
                    {
                        if (!audio_groups_.empty())
                        {
                            int top_idx = pickTopGroupLocked(now_ms);
                            if (top_idx >= 0)
                            {
                                mix.resize(frame_bytes / 2, 0);
                                bool top_mixed = mixGroupLocked(top_idx, mix, 1.0f);
                                mixed = top_mixed;
                                if (top_mixed)
                                {
                                    active_group_ = top_idx;
                                    active_until_ms_ = now_ms + static_cast<uint64_t>(audio_groups_[top_idx].hold_ms);
                                }

                                int top_priority = (top_idx >= 0) ? audio_groups_[top_idx].priority : -999999;
                                for (std::size_t i = 0; i < audio_groups_.size(); ++i)
                                {
                                    if (static_cast<int>(i) == top_idx)
                                        continue;
                                    if (!groupHasDataLocked(audio_groups_[i]))
                                        continue;
                                    float gain = 1.0f;
                                    if (audio_groups_[i].priority < top_priority)
                                    {
                                        gain = static_cast<float>(audio_groups_[i].ducking) / 100.0f;
                                    }
                                    mixGroupLocked(static_cast<int>(i), mix, gain);
                                    mixed = true;
                                }
                                has_frame = mixed;
                            }
                        }
                        else
                        {
                            stream_id = pickAudioStream();
                            if (stream_id >= 0)
                            {
                                auto &q = audio_queues_[stream_id];
                                frame = std::move(q.front());
                                q.pop_front();
                                has_frame = true;

                                static int pop_log_count = 0;
                                if (++pop_log_count % 500 == 0)
                                {
                                    APP_LOG_TRACE("app.av_core.audio")
                                        << "pop stream=" << stream_id
                                        << " remaining=" << q.size();
                                }
                            }
                        }
                    }
                }

                if (do_info_log)
                {
                    size_t mic_q = 0;
                    {
                        std::lock_guard<std::mutex> mic_lock(mic_mutex_);
                        mic_q = mic_queue_.size();
                    }
                    APP_LOG_INFO("app.av_core.audio")
                        << "buffers: audio_total=" << info_total
                        << " streams=[" << info_streams << "]"
                        << " mic_q=" << mic_q
                        << (prebuffer_info.empty() ? "" : (std::string(" ") + prebuffer_info));
                }

                if (mic_blocked)
                {
                    if (pcm_output && frame_bytes > 0)
                    {
                        AvFrame silence;
                        silence.ts_us = nextAudioTimestamp(stream_id, 0);
                        silence.data.assign(frame_bytes, 0);
                        audio_pipeline_.push(silence.ts_us, silence.data.data(), silence.data.size());
                    }
                    continue;
                }

                if (!has_frame)
                {
                    if (pcm_output && cfg_.underrun_policy == UnderrunPolicy::SILENCE && frame_bytes > 0)
                    {
                        static int underrun_count = 0;
                        if (++underrun_count % 100 == 0)
                        {
                            size_t total_items = 0;
                            for (auto &kv : audio_queues_)
                                total_items += kv.second.size();
                            APP_LOG_WARN("app.av_core.audio")
                                << "underrun: injecting silence (frame_bytes=" << frame_bytes
                                << ") total_queued=" << total_items
                                << " stream_id=" << stream_id;
                        }
                        AvFrame silence;
                        silence.ts_us = nextAudioTimestamp(stream_id, 0);
                        silence.data.assign(frame_bytes, 0);
                        audio_pipeline_.push(silence.ts_us, silence.data.data(), silence.data.size());
                    }
                    static int no_frame_count = 0;
                    if (++no_frame_count % 200 == 0)
                    {
                        size_t total_items = 0;
                        for (auto &kv : audio_queues_)
                            total_items += kv.second.size();
                        APP_LOG_DEBUG("app.av_core.audio")
                            << "no frame available; total_queued=" << total_items
                            << " pcm_output=" << (pcm_output ? "yes" : "no");
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                if (!pcm_output)
                {
                    uint64_t ts = frame.ts_us ? frame.ts_us : nextAudioTimestamp(stream_id, 0);
                    if (frame.ts_us)
                    {
                        uint64_t prev_global = last_audio_ts_.load();
                        if (ts > prev_global)
                            last_audio_ts_.store(ts);
                        if (stream_id >= 0)
                            last_audio_ts_per_stream_[stream_id] = ts;
                    }
                    static int encoded_out_count = 0;
                    if (++encoded_out_count % 200 == 0)
                    {
                        APP_LOG_DEBUG("app.av_core.audio")
                            << "push encoded stream=" << stream_id
                            << " ts=" << ts
                            << " size=" << frame.data.size();
                    }
                    audio_pipeline_.push(ts, frame.data.data(), frame.data.size());
                    continue;
                }

                if (mixed)
                {
                    AvFrame out;
                    out.ts_us = nextAudioTimestamp(-1, 0);
                    out.data.resize(mix.size() * sizeof(int16_t));
                    std::memcpy(out.data.data(), mix.data(), out.data.size());
                    static int mixed_out_count = 0;
                    if (++mixed_out_count % 200 == 0)
                    {
                        APP_LOG_DEBUG("app.av_core.audio")
                            << "push mixed ts=" << out.ts_us
                            << " size=" << out.data.size();
                    }
                    audio_pipeline_.push(out.ts_us, out.data.data(), out.data.size());
                }
                else
                {
                    uint64_t ts = nextAudioTimestamp(stream_id, frame.ts_us);
                    static int pcm_out_count = 0;
                    if (++pcm_out_count % 200 == 0)
                    {
                        APP_LOG_DEBUG("app.av_core.audio")
                            << "push pcm stream=" << stream_id
                            << " ts=" << ts
                            << " size=" << frame.data.size();
                    }
                    audio_pipeline_.push(ts, frame.data.data(), frame.data.size());
                }
            }
        }

        static std::string bytesToHex(const uint8_t *data, std::size_t size, std::size_t max_bytes)
        {
            if (!data || size == 0 || max_bytes == 0)
                return "<empty>";
            const std::size_t count = std::min(size, max_bytes);
            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (std::size_t i = 0; i < count; ++i)
            {
                if (i)
                    oss << ' ';
                oss << std::setw(2) << static_cast<int>(data[i]);
            }
            if (size > count)
                oss << " ...";
            return oss.str();
        }

        void micLoop()
        {
            const int bytes_per_sample = std::max(1, mic_bits_ / 8);
            const int bytes_per_ms = (mic_sample_rate_ * mic_channels_ * bytes_per_sample) / 1000;
            const std::size_t target_bytes = static_cast<std::size_t>(std::max(cfg_.mic_batch_ms, cfg_.mic_frame_ms) * std::max(1, bytes_per_ms));
            std::vector<uint8_t> batch;
            uint64_t batch_ts = 0;

            while (mic_running_.load())
            {
                AvFrame f;
                if (!mic_capture_.pull(f, 50))
                    continue;
                if (target_bytes == 0 || f.data.empty())
                {
                    std::lock_guard<std::mutex> lock(mic_mutex_);
                    if (!applyOverrunPolicy(mic_queue_))
                        continue;
                    mic_queue_.push_back(std::move(f));
                    mic_cv_.notify_one();
                    continue;
                }

                if (batch.empty())
                {
                    batch_ts = f.ts_us;
                }
                batch.insert(batch.end(), f.data.begin(), f.data.end());

                if (batch.size() >= target_bytes)
                {
                    AvFrame out;
                    out.ts_us = batch_ts;
                    out.data = std::move(batch);
                    batch.clear();
                    batch_ts = 0;

                    std::lock_guard<std::mutex> lock(mic_mutex_);
                    if (!applyOverrunPolicy(mic_queue_))
                        continue;
                    mic_queue_.push_back(std::move(out));
                    mic_cv_.notify_one();
                }
            }
        }

        bool hasAudioDataLocked() const
        {
            const std::string output_codec = normalizeCodecName(audio_codec_);
            for (const auto &kv : audio_queues_)
            {
                if (kv.second.empty())
                    continue;
                if (output_codec != "PCM")
                {
                    auto it = audio_stream_formats_.find(kv.first);
                    if (it != audio_stream_formats_.end() &&
                        normalizeCodecName(it->second.codec) != output_codec)
                    {
                        continue;
                    }
                }
                return true;
            }
            return false;
        }

        int encodedFrameDurationMsLocked(int stream_id) const
        {
            auto it = audio_stream_formats_.find(stream_id);
            if (it == audio_stream_formats_.end())
                return 20;
            const auto &fmt = it->second;
            const std::string codec = normalizeCodecName(fmt.codec);
            if (codec == "AAC_LC" && fmt.sample_rate > 0)
            {
                return static_cast<int>((1024 * 1000 + fmt.sample_rate - 1) / fmt.sample_rate);
            }
            if (codec == "OPUS")
                return 20;
            return 20;
        }

        bool groupHasDataLocked(const AudioGroup &g) const
        {
            for (int ch : g.channels)
            {
                auto it = audio_queues_.find(ch);
                if (it != audio_queues_.end() && !it->second.empty())
                    return true;
            }
            return false;
        }

        int pickTopGroupLocked(uint64_t now_ms)
        {
            if (active_group_ >= 0 && now_ms <= active_until_ms_)
            {
                return active_group_;
            }

            int top_idx = -1;
            int top_priority = -999999;
            for (std::size_t i = 0; i < audio_groups_.size(); ++i)
            {
                if (!groupHasDataLocked(audio_groups_[i]))
                    continue;
                if (audio_groups_[i].priority > top_priority)
                {
                    top_priority = audio_groups_[i].priority;
                    top_idx = static_cast<int>(i);
                }
            }
            if (top_idx >= 0)
            {
                active_group_ = top_idx;
                active_until_ms_ = now_ms + static_cast<uint64_t>(audio_groups_[top_idx].hold_ms);
            }
            else
            {
                active_group_ = -1;
                active_until_ms_ = 0;
            }
            return top_idx;
        }

        bool mixGroupLocked(int idx, std::vector<int16_t> &mix, float gain)
        {
            if (idx < 0 || idx >= static_cast<int>(audio_groups_.size()))
                return false;
            auto &g = audio_groups_[idx];
            bool got_any = false;
            for (std::size_t i = 0; i < g.channels.size(); ++i)
            {
                int ch = g.channels[i];
                auto it = audio_queues_.find(ch);
                if (it == audio_queues_.end() || it->second.empty())
                    continue;

                AvFrame frame = std::move(it->second.front());
                it->second.pop_front();
                got_any = true;

                float ch_gain = gain;
                if (!g.channel_gains.empty())
                {
                    float gval = g.channel_gains[std::min(i, g.channel_gains.size() - 1)];
                    ch_gain *= gval;
                }

                const int16_t *src = reinterpret_cast<const int16_t *>(frame.data.data());
                std::size_t samples = std::min(mix.size(), frame.data.size() / sizeof(int16_t));
                for (std::size_t s = 0; s < samples; ++s)
                {
                    int32_t v = mix[s] + static_cast<int32_t>(static_cast<float>(src[s]) * ch_gain);
                    if (v > 32767)
                        v = 32767;
                    if (v < -32768)
                        v = -32768;
                    mix[s] = static_cast<int16_t>(v);
                }
            }
            return got_any;
        }

        uint64_t nextAudioTimestamp(int stream_id, uint64_t preferred_ts)
        {
            // Track timestamps per stream so each audio channel can maintain correct timing.
            uint64_t last;
            if (stream_id >= 0)
            {
                last = last_audio_ts_per_stream_[stream_id];
            }
            else
            {
                last = last_audio_ts_.load();
            }

            uint64_t step = static_cast<uint64_t>(cfg_.audio_frame_ms) * 1000;
            uint64_t next = 0;

            if (last == 0)
            {
                next = (preferred_ts == 0) ? step : preferred_ts;
            }
            else
            {
                next = last + step;
                if (preferred_ts > next)
                    next = preferred_ts;
            }

            // Debug: log when incoming timestamp differs significantly from our ramp.
            static int ts_log_count = 0;
            if (++ts_log_count % 1000 == 0)
            {
                APP_LOG_DEBUG("app.av_core.audio")
                    << "nextAudioTimestamp stream=" << stream_id
                    << " preferred=" << preferred_ts << " last=" << last
                    << " next=" << next << " step=" << step;
            }
            if (stream_id >= 0)
            {
                last_audio_ts_per_stream_[stream_id] = next;
            }
            uint64_t prev_global = last_audio_ts_.load();
            if (next > prev_global)
            {
                last_audio_ts_.store(next);
            }
            return next;
        }

        static uint64_t nowMs()
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now().time_since_epoch())
                                             .count());
        }

        int audioBytesPerFrame() const
        {
            if (audio_sample_rate_ <= 0 || audio_channels_ <= 0 || audio_bits_ <= 0)
                return 0;
            int bytes_per_sample = audio_bits_ / 8;
            int samples = audio_sample_rate_ * cfg_.audio_frame_ms / 1000;
            return samples * audio_channels_ * bytes_per_sample;
        }

        AvCoreConfig cfg_;

        int video_width_ = 800;
        int video_height_ = 480;
        uintptr_t window_id_ = 0;

        int audio_sample_rate_ = 16000;
        int audio_channels_ = 1;
        int audio_bits_ = 16;
        std::string audio_codec_ = "PCM";

        int mic_sample_rate_ = 16000;
        int mic_channels_ = 1;
        int mic_bits_ = 16;

        GstVideoPipeline video_pipeline_;
        GstAudioPipeline audio_pipeline_;
        GstMicCapture mic_capture_;

        std::atomic<bool> video_running_{false};
        std::atomic<bool> audio_running_{false};
        std::atomic<bool> mic_running_{false};
        std::atomic<bool> mic_active_{false};

        std::thread video_thread_;
        std::thread audio_thread_;
        std::thread mic_thread_;

        // Optional dump of incoming audio (raw/encoded) for offline analysis
        std::mutex audio_dump_mutex_;
        std::string audio_dump_path_;
        bool audio_dump_single_ = false;
        std::unordered_map<int, std::ofstream> audio_dump_files_;

        std::deque<AvFrame> video_queue_;
        std::mutex video_mutex_;
        std::condition_variable video_cv_;

        std::unordered_map<int, std::deque<AvFrame>> audio_queues_;
        std::unordered_map<int, AudioStreamFormat> audio_stream_formats_;
        std::vector<uint8_t> audio_codec_data_;
        std::vector<int> audio_priority_;
        std::vector<AudioGroup> audio_groups_;
        int active_group_ = -1;
        uint64_t active_until_ms_ = 0;
        std::mutex audio_mutex_;
        std::condition_variable audio_cv_;

        std::deque<AvFrame> mic_queue_;
        std::mutex mic_mutex_;
        std::condition_variable mic_cv_;

        std::atomic<uint64_t> last_audio_ts_{0};
        std::unordered_map<int, uint64_t> last_audio_ts_per_stream_;
    };

} // namespace nemo

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

inline void init_av_core_binding(pybind11::module_ &m)
{
    namespace py = pybind11;

    py::enum_<nemo::OverrunPolicy>(m, "OverrunPolicy")
        .value("DROP_OLD", nemo::OverrunPolicy::DROP_OLD)
        .value("DROP_NEW", nemo::OverrunPolicy::DROP_NEW)
        .value("PROGRESSIVE_DISCARD", nemo::OverrunPolicy::PROGRESSIVE_DISCARD);

    py::enum_<nemo::UnderrunPolicy>(m, "UnderrunPolicy")
        .value("WAIT", nemo::UnderrunPolicy::WAIT)
        .value("SILENCE", nemo::UnderrunPolicy::SILENCE);

    py::class_<nemo::AvCoreConfig>(m, "AvCoreConfig")
        .def(py::init<>())
        .def_readwrite("jitter_buffer_ms", &nemo::AvCoreConfig::jitter_buffer_ms)
        .def_readwrite("max_queue_frames", &nemo::AvCoreConfig::max_queue_frames)
        .def_readwrite("audio_frame_ms", &nemo::AvCoreConfig::audio_frame_ms)
        .def_readwrite("max_av_lead_ms", &nemo::AvCoreConfig::max_av_lead_ms)
        .def_readwrite("mic_frame_ms", &nemo::AvCoreConfig::mic_frame_ms)
        .def_readwrite("mic_batch_ms", &nemo::AvCoreConfig::mic_batch_ms)
        .def_readwrite("audio_prebuffer_ms", &nemo::AvCoreConfig::audio_prebuffer_ms)
        .def_readwrite("overrun_policy", &nemo::AvCoreConfig::overrun_policy)
        .def_readwrite("underrun_policy", &nemo::AvCoreConfig::underrun_policy);

    py::class_<nemo::AvCore, std::shared_ptr<nemo::AvCore>>(m, "AvCore")
        .def(py::init<nemo::AvCoreConfig>(), py::arg("config") = nemo::AvCoreConfig())
        .def("configure_video", &nemo::AvCore::configureVideo, py::arg("width"), py::arg("height"))
        .def("set_window_id", &nemo::AvCore::setWindowId, py::arg("window_id"))
        .def("is_mic_active", &nemo::AvCore::isMicActive)
        .def("start_video", &nemo::AvCore::startVideo)
        .def("stop_video", &nemo::AvCore::stopVideo)
        .def("configure_audio", &nemo::AvCore::configureAudio,
             py::arg("sample_rate"), py::arg("channels"), py::arg("bits"), py::arg("codec") = "PCM")
        .def("configure_audio_stream", &nemo::AvCore::configureAudioStream,
             py::arg("stream_id"), py::arg("sample_rate"), py::arg("channels"), py::arg("bits"), py::arg("codec") = "PCM")
        .def("start_audio", &nemo::AvCore::startAudio)
        .def("stop_audio", &nemo::AvCore::stopAudio)
        .def("configure_mic", &nemo::AvCore::configureMic, py::arg("sample_rate"), py::arg("channels"), py::arg("bits"))
        .def("start_mic", &nemo::AvCore::startMicCapture)
        .def("stop_mic", &nemo::AvCore::stopMicCapture)
        .def("stop", &nemo::AvCore::stop)
        .def("set_audio_priority",
             [](nemo::AvCore &self, py::object obj)
             {
                 if (obj.is_none())
                 {
                     self.setAudioPriority({});
                     return;
                 }
                 if (!py::isinstance<py::sequence>(obj) || py::isinstance<py::str>(obj))
                 {
                     throw std::runtime_error("set_audio_priority expects a list/tuple");
                 }
                 py::sequence seq = obj;
                 if (seq.size() == 0)
                 {
                     self.setAudioPriority({});
                     return;
                 }

                 py::object first = seq[0];
                 if (py::isinstance<py::int_>(first))
                 {
                     std::vector<int> order;
                     order.reserve(seq.size());
                     for (auto item : seq)
                     {
                         order.push_back(py::cast<int>(item));
                     }
                     self.setAudioPriority(order);
                     return;
                 }

                 std::vector<nemo::AudioGroup> groups;
                 groups.reserve(seq.size());
                 for (auto item : seq)
                 {
                     if (!py::isinstance<py::sequence>(item) || py::isinstance<py::str>(item))
                     {
                         throw std::runtime_error("group entries must be tuples/lists");
                     }
                     py::sequence tup = item;
                     if (tup.size() < 2)
                     {
                         throw std::runtime_error("group entry must be (channels, priority[, ducking])");
                     }
                     py::sequence chs = tup[0];
                     std::vector<int> channels;
                     channels.reserve(chs.size());
                     for (auto ch : chs)
                     {
                         channels.push_back(py::cast<int>(ch));
                     }
                     int priority = py::cast<int>(tup[1]);
                     int ducking = 100;
                     int hold_ms = 100;
                     std::vector<float> gains;
                     if (tup.size() >= 3)
                     {
                         ducking = py::cast<int>(tup[2]);
                     }
                     if (tup.size() >= 4)
                     {
                         hold_ms = py::cast<int>(tup[3]);
                     }
                     if (tup.size() >= 5)
                     {
                         py::object gains_obj = tup[4];
                         if (py::isinstance<py::sequence>(gains_obj) && !py::isinstance<py::str>(gains_obj))
                         {
                             py::sequence gseq = gains_obj;
                             gains.reserve(gseq.size());
                             for (auto gval : gseq)
                             {
                                 float gv = py::cast<float>(gval);
                                 if (gv > 2.0f)
                                     gv = gv / 100.0f; // treat 50 as 0.5
                                 gains.push_back(gv);
                             }
                         }
                     }
                     nemo::AudioGroup g;
                     g.channels = std::move(channels);
                     g.priority = priority;
                     g.ducking = ducking;
                     g.hold_ms = hold_ms;
                     g.channel_gains = std::move(gains);
                     groups.push_back(std::move(g));
                 }
                 self.setAudioGroups(groups);
             })
        .def("set_policies", &nemo::AvCore::setPolicies)
        .def("set_jitter_buffer_ms", &nemo::AvCore::setJitterBufferMs)
        .def("set_max_queue_frames", &nemo::AvCore::setMaxQueueFrames)
        .def("set_audio_frame_ms", &nemo::AvCore::setAudioFrameMs)
        .def("set_max_av_lead_ms", &nemo::AvCore::setMaxAvLeadMs)
        .def("ptr", &nemo::AvCore::ptr);
}
