#pragma once

// av_core.hpp - Orchestratore A/V.
// Questo header include le cinque classi specializzate e coordina:
//   - GstVideoPipeline  (ciclo vita video H.264)
//   - GstAudioPipeline  (playback audio PCM / AAC-LC / OPUS, Fix #4 lock-free)
//   - GstMicCapture     (cattura microfono con splitter opzionale)
//   - audio routing (priority list, AudioGroup mix, codec negotiation)
//   - A/V sync gate PTS-based (Fix #3 + Fix #10 Master Audio Clock)
//
// Regole architetturali rispettate:
//   - Media data (PCM / H.264) NON attraversa mai il GIL Python.
//   - I counter di log sono std::atomic<int> (Bug A fix).
//   - nextAudioTimestampLocked() chiamato SOLO con audio_mutex_ gia' acquisito (Bug B fix).

#include "av_types.hpp"
#include "av_utils.hpp"
#include "gst_video_pipeline.hpp"
#include "gst_audio_pipeline.hpp"
#include "gst_mic_capture.hpp"
#include "app/core/logging.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nemo
{

    class AvCore
    {
    public:
        explicit AvCore(AvCoreConfig cfg = {})
            : cfg_(cfg)
        {}

        ~AvCore() { stop(); }

        // ---------------------------------------------------------------- video
        void configureVideo(int width, int height) { video_width_ = width; video_height_ = height; }
        void setWindowId(uintptr_t wid)             { window_id_ = wid; }

        void startVideo()
        {
            if (video_running_.load()) return;
            video_pipeline_.init(window_id_, video_width_, video_height_);
            video_running_.store(true);
            video_thread_ = std::thread([this](){ videoLoop(); });
        }

        void stopVideo()
        {
            video_running_.store(false);
            video_cv_.notify_all();
            if (video_thread_.joinable()) video_thread_.join();
            video_pipeline_.stop();
            clearQueue(video_queue_, video_mutex_);
        }

        // ---------------------------------------------------------------- audio
        void configureAudio(int sample_rate, int channels, int bits,
                            const std::string &codec = "PCM")
        {
            audio_sample_rate_ = sample_rate;
            audio_channels_    = channels;
            audio_bits_        = bits;
            audio_codec_       = normalizeCodecName(codec);
            APP_LOG_INFO("app.av_core.audio")
                << "configureAudio: " << sample_rate << "Hz "
                << channels << "ch " << bits << "bit codec=" << audio_codec_;
        }

        void configureAudioStream(int stream_id, int sample_rate, int channels,
                                   int bits, const std::string &codec = "PCM")
        {
            AudioStreamFormat fmt;
            fmt.sample_rate = sample_rate; fmt.channels = channels;
            fmt.bits        = bits;        fmt.codec    = normalizeCodecName(codec);
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
            if (audio_running_.load()) return;

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
                        audio_dump_single_ = envTruthy(dump_single);
                    else
                    {
                        std::string path = audio_dump_path_;
                        if (path.find("{stream}") != std::string::npos)
                            audio_dump_single_ = false;
                        else
                        {
                            std::error_code ec;
                            if (std::filesystem::is_directory(path, ec))
                                audio_dump_single_ = false;
                            else if (!path.empty() && (path.back() == '/' || path.back() == '\\'))
                                audio_dump_single_ = false;
                            else
                                audio_dump_single_ = true;
                        }
                    }
                    APP_LOG_INFO("app.av_core.audio")
                        << "dumping incoming audio stream to " << audio_dump_path_
                        << (audio_dump_single_ ? " (single file)" : " (per stream)");
                }
            }

            APP_LOG_INFO("app.av_core.audio")
                << "startAudio: sample_rate=" << audio_sample_rate_
                << " channels=" << audio_channels_
                << " bits=" << audio_bits_
                << " codec=" << audio_codec_;
            audio_pipeline_.init(audio_sample_rate_, audio_channels_, audio_bits_, audio_codec_);
            if (!audio_codec_data_.empty())
                audio_pipeline_.setCodecData(audio_codec_data_);
            {
                std::lock_guard<std::mutex> lock(audio_mutex_);
                last_audio_ts_.store(0);
                last_audio_ts_per_stream_.clear();
            }
            audio_clock_us_.store(-1);
            audio_running_.store(true);
            audio_thread_ = std::thread([this](){ audioLoop(); });
        }

        void stopAudio()
        {
            audio_running_.store(false);
            audio_cv_.notify_all();
            if (audio_thread_.joinable()) audio_thread_.join();
            audio_pipeline_.stop();
            clearAudioQueues();
            audio_codec_data_.clear();
            audio_clock_us_.store(-1);
            {
                std::lock_guard<std::mutex> lock(audio_dump_mutex_);
                closeAudioDumpFilesLocked();
                audio_dump_path_.clear();
                audio_dump_single_ = false;
            }
        }

        // ------------------------------------------------------------------ mic
        void configureMic(int sample_rate, int channels, int bits)
        {
            mic_sample_rate_ = sample_rate;
            mic_channels_    = channels;
            mic_bits_        = bits;
        }

        void startMicCapture()
        {
            if (mic_running_.load()) return;
            int frame_ms = cfg_.mic_frame_ms > 0 ? cfg_.mic_frame_ms : 0;
            APP_LOG_INFO("app.av_core.audio")
                << "startMicCapture: rate=" << mic_sample_rate_
                << " ch=" << mic_channels_ << " bits=" << mic_bits_
                << " frame_ms=" << frame_ms << " batch_ms=" << cfg_.mic_batch_ms;
            mic_capture_.init(mic_sample_rate_, mic_channels_, mic_bits_, frame_ms);
            mic_running_.store(true);
            mic_thread_ = std::thread([this](){ micLoop(); });
        }

        void stopMicCapture()
        {
            mic_running_.store(false);
            mic_cv_.notify_all();
            if (mic_thread_.joinable()) mic_thread_.join();
            mic_capture_.stop();
            clearQueue(mic_queue_, mic_mutex_);
        }

        void setMicActive(bool active) { mic_active_.store(active); }
        bool isMicActive()       const { return mic_active_.load(); }

        void stop() { stopVideo(); stopAudio(); stopMicCapture(); }

        /// Fix #10: Master Audio Clock - PTS (us) dell'ultimo chunk inviato alla pipeline.
        /// -1 se la pipeline non e' ancora avviata. Lock-free, safe da qualsiasi thread.
        int64_t getAudioClockUs() const { return audio_clock_us_.load(); }

        // ---------------------------------------------------- push / pop pubblici
        void pushVideo(uint64_t ts_us, const uint8_t *data, std::size_t size)
        {
            if (!video_running_.load()) return;
            AvFrame f;
            f.ts_us = ts_us;
            f.data.assign(data, data + size);
            {
                std::lock_guard<std::mutex> lock(video_mutex_);
                if (!applyOverrunPolicy(video_queue_)) return;
                video_queue_.push_back(std::move(f));
            }
            video_cv_.notify_one();
        }

        void pushAudio(int stream_id, uint64_t ts_us, const uint8_t *data, std::size_t size)
        {
            if (!audio_running_.load()) return;

            // Bug A fix: counter atomico, thread-safe.
            const int in_cnt = ++in_log_count_;
            if (ts_us == 0 || in_cnt % 200 == 0)
            {
                APP_LOG_DEBUG("app.av_core.audio")
                    << "incoming stream=" << stream_id
                    << " ts=" << ts_us << " size=" << size
                    << " head=" << bytesToHex(data, size, 16);
            }
            if (data && size > 0)
                writeAudioDump(stream_id, data, size);

            AvFrame f;
            f.ts_us = ts_us;
            f.data.assign(data, data + size);

            AudioStreamFormat fmt     = getStreamFormat(stream_id);
            const bool stream_pcm     = isPcmCodec(fmt.codec);
            const bool output_pcm     = isPcmCodec(audio_codec_);

            // Intercetta AAC codec_data (2-byte packet prima dei frame reali).
            if (!output_pcm && !stream_pcm && size == 2 &&
                normalizeCodecName(fmt.codec) == "AAC_LC")
            {
                std::vector<uint8_t> codec_data(data, data + size);
                if (codec_data != audio_codec_data_)
                {
                    audio_codec_data_ = codec_data;
                    audio_pipeline_.setCodecData(audio_codec_data_);
                    APP_LOG_INFO("app.av_core.audio")
                        << "captured AAC codec_data="
                        << bytesToHex(data, size, size)
                        << " stream=" << stream_id;
                }
                return;
            }

            if (output_pcm)
            {
                if (!stream_pcm)
                {
                    const int mm = ++codec_mismatch_count_;
                    if (mm % 200 == 0)
                        APP_LOG_WARN("app.av_core.audio")
                            << "codec mismatch: stream=" << fmt.codec
                            << " output=" << audio_codec_ << " (dropping frames)";
                    return;
                }
                if (!normalizePcmFrame(f, fmt)) return;
            }
            {
                std::lock_guard<std::mutex> lock(audio_mutex_);
                auto &q = audio_queues_[stream_id];
                if (!applyOverrunPolicy(q)) return;
                q.push_back(std::move(f));

                const int qlc = ++queue_log_count_;
                if (qlc % 500 == 0)
                    APP_LOG_DEBUG("app.av_core.audio")
                        << "queue status: stream=" << stream_id
                        << " size=" << q.size()
                        << " total_streams=" << audio_queues_.size();
            }
            audio_cv_.notify_one();
        }

        bool popMicFrame(AvFrame &out, int timeout_ms)
        {
            std::unique_lock<std::mutex> lock(mic_mutex_);
            if (!mic_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                    [this](){ return !mic_queue_.empty() || !mic_running_.load(); }))
                return false;
            if (mic_queue_.empty()) return false;
            out = std::move(mic_queue_.front());
            mic_queue_.pop_front();
            return true;
        }

        uintptr_t ptr() const { return reinterpret_cast<uintptr_t>(this); }

        // ----------------------------------------------------- audio routing API
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
            cfg_.overrun_policy  = overrun;
            cfg_.underrun_policy = underrun;
        }

        void setJitterBufferMs(int ms) { cfg_.jitter_buffer_ms = ms; }
        void setMaxQueueFrames(int f)  { cfg_.max_queue_frames = f; }
        void setAudioFrameMs(int ms)   { cfg_.audio_frame_ms   = ms; }
        void setMaxAvLeadMs(int ms)    { cfg_.max_av_lead_ms   = ms; }

    private:
        // --------------------------------------------------- stream format helpers
        AudioStreamFormat getStreamFormat(int stream_id)
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            return getStreamFormatLocked(stream_id);
        }

        AudioStreamFormat getStreamFormatLocked(int stream_id) const
        {
            auto it = audio_stream_formats_.find(stream_id);
            if (it != audio_stream_formats_.end()) return it->second;
            AudioStreamFormat fmt;
            fmt.sample_rate = audio_sample_rate_; fmt.channels = audio_channels_;
            fmt.bits        = audio_bits_;        fmt.codec    = audio_codec_;
            return fmt;
        }

        // --------------------------------------------- PCM normalization (SRC+CH)
        bool normalizePcmFrame(AvFrame &frame, const AudioStreamFormat &fmt)
        {
            if (frame.data.empty()) return false;
            if (fmt.bits != 16 || audio_bits_ != 16) return true;
            if (audio_sample_rate_ <= 0 || audio_channels_ <= 0 || fmt.sample_rate <= 0) return true;
            if (fmt.sample_rate == audio_sample_rate_ && fmt.channels == audio_channels_) return true;

            const int16_t  *src           = reinterpret_cast<const int16_t *>(frame.data.data());
            std::size_t     total_samples = frame.data.size() / sizeof(int16_t);
            if (total_samples == 0 || fmt.channels <= 0) return false;
            std::size_t in_frames = total_samples / static_cast<std::size_t>(fmt.channels);
            if (in_frames == 0) return false;

            std::vector<int16_t> ch_conv;
            if (fmt.channels == audio_channels_)
                ch_conv.assign(src, src + total_samples);
            else if (fmt.channels == 1 && audio_channels_ == 2)
            {
                ch_conv.resize(in_frames * 2);
                for (std::size_t i = 0; i < in_frames; ++i)
                    { ch_conv[i*2] = src[i]; ch_conv[i*2+1] = src[i]; }
            }
            else if (fmt.channels == 2 && audio_channels_ == 1)
            {
                ch_conv.resize(in_frames);
                for (std::size_t i = 0; i < in_frames; ++i)
                    ch_conv[i] = static_cast<int16_t>(
                        (static_cast<int32_t>(src[i*2]) +
                         static_cast<int32_t>(src[i*2+1])) / 2);
            }
            else
            {
                const int cw = ++channel_warn_count_;
                if (cw % 200 == 0)
                    APP_LOG_ERROR("app.av_core.audio")
                        << "unsupported channel conversion: "
                        << fmt.channels << " -> " << audio_channels_;
                return false;
            }

            std::size_t conv_frames = ch_conv.size() / static_cast<std::size_t>(audio_channels_);
            std::vector<int16_t> resampled;
            if (fmt.sample_rate != audio_sample_rate_ && conv_frames > 0)
            {
                const double   ratio      = static_cast<double>(fmt.sample_rate) /
                                            static_cast<double>(audio_sample_rate_);
                std::size_t    out_frames = std::max<std::size_t>(
                    1, static_cast<std::size_t>(static_cast<double>(conv_frames) / ratio));
                resampled.resize(out_frames * static_cast<std::size_t>(audio_channels_));
                for (std::size_t of = 0; of < out_frames; ++of)
                {
                    double      src_pos = static_cast<double>(of) * ratio;
                    std::size_t idx     = static_cast<std::size_t>(src_pos);
                    double      frac    = src_pos - static_cast<double>(idx);
                    if (idx >= conv_frames) idx = conv_frames - 1;
                    std::size_t idx2 = std::min(idx + 1, conv_frames - 1);
                    for (int ch = 0; ch < audio_channels_; ++ch)
                    {
                        int16_t s0 = ch_conv[idx  * audio_channels_ + ch];
                        int16_t s1 = ch_conv[idx2 * audio_channels_ + ch];
                        resampled[of * audio_channels_ + ch] = static_cast<int16_t>(
                            static_cast<double>(s0) +
                            static_cast<double>(s1 - s0) * frac);
                    }
                }
            }
            else
                resampled = std::move(ch_conv);

            const int frame_bytes = audioBytesPerFrame();
            if (frame_bytes > 0)
            {
                std::size_t expected = frame_bytes / sizeof(int16_t);
                if (resampled.size() < expected) resampled.resize(expected, 0);
                else if (resampled.size() > expected) resampled.resize(expected);
            }
            frame.data.resize(resampled.size() * sizeof(int16_t));
            std::memcpy(frame.data.data(), resampled.data(), frame.data.size());
            return true;
        }

        // -------------------------------------------------- audio dump helpers
        void writeAudioDump(int stream_id, const uint8_t *data, std::size_t size)
        {
            if (!data || size == 0) return;
            std::lock_guard<std::mutex> lock(audio_dump_mutex_);
            if (audio_dump_path_.empty()) return;
            int file_id = audio_dump_single_ ? 0 : stream_id;
            auto &file  = audio_dump_files_[file_id];
            if (!file.is_open())
            {
                const std::string path = resolveDumpPathLocked(stream_id);
                if (!path.empty())
                {
                    file.open(path, std::ios::binary | std::ios::app);
                    if (file.is_open())
                        APP_LOG_INFO("app.av_core.audio")
                            << "dump stream=" << stream_id << " -> " << path;
                    else
                        APP_LOG_ERROR("app.av_core.audio")
                            << "failed to open dump file: " << path;
                }
            }
            if (file.is_open())
                file.write(reinterpret_cast<const char *>(data),
                           static_cast<std::streamsize>(size));
        }

        std::string resolveDumpPathLocked(int stream_id) const
        {
            if (audio_dump_path_.empty()) return "";
            if (audio_dump_single_) return audio_dump_path_;
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
                return path + "/audio_stream_" + std::to_string(stream_id) + ".dump";
            if (!path.empty() && (path.back() == '/' || path.back() == '\\'))
                return path + "audio_stream_" + std::to_string(stream_id) + ".dump";
            return path + ".stream_" + std::to_string(stream_id);
        }

        void closeAudioDumpFilesLocked()
        {
            for (auto &kv : audio_dump_files_)
                if (kv.second.is_open()) kv.second.close();
            audio_dump_files_.clear();
        }

        // --------------------------------------------------- overrun / queue
        bool applyOverrunPolicy(std::deque<AvFrame> &q)
        {
            if (static_cast<int>(q.size()) < cfg_.max_queue_frames) return true;
            if (cfg_.overrun_policy == OverrunPolicy::DROP_NEW)
            {
                if (++drop_new_count_ % 500 == 0)
                    APP_LOG_WARN("app.av_core.audio")
                        << "overrun: dropping new frame (queue=" << q.size() << ")";
                return false;
            }
            if (cfg_.overrun_policy == OverrunPolicy::DROP_OLD)
            {
                if (++drop_old_count_ % 500 == 0)
                    APP_LOG_WARN("app.av_core.audio")
                        << "overrun: dropping old frame (queue=" << q.size() << ")";
                q.pop_front();
                return true;
            }
            // PROGRESSIVE_DISCARD
            while (static_cast<int>(q.size()) >= cfg_.max_queue_frames / 2 && !q.empty())
                q.pop_front();
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

        void clearAudioQueuesLocked() { audio_queues_.clear(); }

        // --------------------------------------------------- video loop (Fix #3)
        void videoLoop()
        {
            while (video_running_.load())
            {
                AvFrame frame;
                {
                    std::unique_lock<std::mutex> lock(video_mutex_);
                    video_cv_.wait(lock, [this](){
                        return !video_queue_.empty() || !video_running_.load(); });
                    if (!video_running_.load()) break;
                    frame = std::move(video_queue_.front());
                    video_queue_.pop_front();
                }
                if (audio_running_.load())
                {
                    const int64_t audio_clock = audio_clock_us_.load();
                    if (audio_clock >= 0)
                    {
                        const int64_t lead_us    = static_cast<int64_t>(frame.ts_us) - audio_clock;
                        const int64_t max_lead_us = static_cast<int64_t>(cfg_.max_av_lead_ms) * 1000;
                        if (lead_us > max_lead_us)
                        {
                            const int sleep_ms = static_cast<int>(
                                std::min<int64_t>((lead_us - max_lead_us) / 1000, 8));
                            if (sleep_ms > 0)
                                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
                        }
                    }
                }
                video_pipeline_.push(frame.ts_us, frame.data.data(), frame.data.size());
            }
        }

        // --------------------------------------------------- audio stream select
        int pickAudioStream()
        {
            if (!audio_priority_.empty())
            {
                for (int id : audio_priority_)
                {
                    auto it = audio_queues_.find(id);
                    if (it == audio_queues_.end() || it->second.empty()) continue;
                    if (normalizeCodecName(getStreamFormatLocked(id).codec) !=
                        normalizeCodecName(audio_codec_)) continue;
                    return id;
                }
            }
            for (auto &kv : audio_queues_)
            {
                if (normalizeCodecName(getStreamFormatLocked(kv.first).codec) !=
                    normalizeCodecName(audio_codec_)) continue;
                if (!kv.second.empty()) return kv.first;
            }
            return -1;
        }

        // Fix #10: aggiorna il Master Audio Clock dopo ogni push alla pipeline.
        void updateAudioClock(uint64_t ts_us)
        {
            audio_clock_us_.store(static_cast<int64_t>(ts_us));
        }

        // ------------------------------------------------------- audio main loop
        void audioLoop()
        {
            const bool pcm_output  = isPcmCodec(audio_codec_);
            const int  frame_bytes = pcm_output ? audioBytesPerFrame() : 0;

            while (audio_running_.load())
            {
                int    stream_id  = -1;
                AvFrame frame;
                bool   has_frame  = false;
                // Fix #2: frames_to_mix estratti dentro il lock; mix PCM calcolato fuori.
                std::vector<std::pair<std::vector<int16_t>, float>> frames_to_mix;
                bool   mixed      = false;
                uint64_t now_ms   = nowMs();

                // Bug D fix: flag calcolato fuori dal lock.
                bool do_info_log  = false;
                {
                    uint64_t expected = last_info_ms_.load();
                    if (now_ms >= expected + 1000)
                        if (last_info_ms_.compare_exchange_strong(expected, now_ms))
                            do_info_log = true;
                }

                struct StreamSnapshot { int id; std::size_t size; };
                std::vector<StreamSnapshot> stream_snapshot;
                int snap_prebuf_stream_id = -1;
                int snap_queued_frames = 0, snap_prebuffer_frames = 0;
                int snap_max_frames = 0, snap_queued_ms = 0, snap_target_ms = 0;

                {
                    std::unique_lock<std::mutex> lock(audio_mutex_);
                    audio_cv_.wait_for(lock, std::chrono::milliseconds(cfg_.audio_frame_ms),
                        [this](){ return hasAudioDataLocked() || !audio_running_.load(); });
                    if (!audio_running_.load()) break;

                    bool mic_blocked = false;
                    if (mic_active_.load())
                    {
                        clearAudioQueuesLocked();
                        mic_blocked = true;
                    }

                    if (do_info_log)
                        for (auto &kv : audio_queues_)
                            stream_snapshot.push_back({kv.first, kv.second.size()});

                    if (!pcm_output)
                    {
                        stream_id = pickAudioStream();
                        if (stream_id >= 0)
                        {
                            auto &q          = audio_queues_[stream_id];
                            int   frame_ms   = encodedFrameDurationMsLocked(stream_id);
                            snap_queued_frames    = static_cast<int>(q.size());
                            snap_max_frames       = std::max(1, cfg_.max_queue_frames);
                            snap_prebuffer_frames = std::max(1,
                                (cfg_.audio_prebuffer_ms + frame_ms - 1) / frame_ms);
                            if (snap_prebuffer_frames >= snap_max_frames)
                                snap_prebuffer_frames = std::max(1, snap_max_frames - 1);
                            snap_queued_ms    = frame_ms * snap_queued_frames;
                            snap_target_ms    = cfg_.audio_prebuffer_ms;
                            snap_prebuf_stream_id = stream_id;

                            if (cfg_.audio_prebuffer_ms <= 0 ||
                                snap_queued_frames >= snap_prebuffer_frames)
                            {
                                frame = std::move(q.front());
                                q.pop_front();
                                has_frame = true;
                                const int epc = ++enc_pop_count_;
                                if (epc <= 5 || epc % 200 == 0)
                                    APP_LOG_DEBUG("app.av_core.audio")
                                        << "pop encoded stream=" << stream_id
                                        << " size=" << frame.data.size();
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
                                frames_to_mix = drainGroupFrames(
                                    top_idx, audio_groups_[top_idx].priority);
                                has_frame = !frames_to_mix.empty();
                            }
                        }
                        else
                        {
                            stream_id = pickAudioStream();
                            if (stream_id >= 0)
                            {
                                auto &q = audio_queues_[stream_id];
                                frame   = std::move(q.front());
                                q.pop_front();
                                has_frame = true;
                                if (++pop_log_count_ % 500 == 0)
                                    APP_LOG_TRACE("app.av_core.audio")
                                        << "pop stream=" << stream_id
                                        << " remaining=" << q.size();
                            }
                        }
                    }

                    if (mic_blocked)
                    {
                        if (pcm_output && frame_bytes > 0)
                        {
                            uint64_t ts = nextAudioTimestampLocked(stream_id, 0);
                            AvFrame silence;
                            silence.ts_us = ts;
                            silence.data.assign(frame_bytes, 0);
                            audio_pipeline_.push(silence.ts_us,
                                                  silence.data.data(), silence.data.size());
                            updateAudioClock(silence.ts_us);
                        }
                        continue;
                    }
                } // fine lock audio_mutex_

                // Bug D fix: log costruito FUORI dal lock.
                if (do_info_log)
                {
                    std::ostringstream oss;
                    std::size_t total = 0; bool first = true;
                    for (auto &s : stream_snapshot)
                    {
                        if (!first) oss << " ";
                        first = false;
                        oss << s.id << "=" << s.size;
                        total += s.size;
                    }
                    std::string prebuffer_info;
                    if (snap_prebuf_stream_id >= 0)
                    {
                        std::ostringstream poss;
                        poss << "prebuffer stream=" << snap_prebuf_stream_id
                             << " queued_frames=" << snap_queued_frames
                             << " prebuffer_frames=" << snap_prebuffer_frames
                             << " max_frames=" << snap_max_frames
                             << " queued_ms=" << snap_queued_ms
                             << " target_ms=" << snap_target_ms;
                        prebuffer_info = poss.str();
                    }
                    std::size_t mic_q = 0;
                    {
                        std::lock_guard<std::mutex> mic_lock(mic_mutex_);
                        mic_q = mic_queue_.size();
                    }
                    APP_LOG_INFO("app.av_core.audio")
                        << "buffers: audio_total=" << total
                        << " streams=[" << oss.str() << "]"
                        << " mic_q=" << mic_q
                        << (prebuffer_info.empty() ? "" : (" " + prebuffer_info));
                }

                if (!has_frame)
                {
                    if (pcm_output && cfg_.underrun_policy == UnderrunPolicy::SILENCE
                        && frame_bytes > 0)
                    {
                        if (++underrun_count_ % 100 == 0)
                        {
                            std::size_t total_items = 0;
                            {
                                std::lock_guard<std::mutex> lock(audio_mutex_);
                                for (auto &kv : audio_queues_) total_items += kv.second.size();
                            }
                            APP_LOG_WARN("app.av_core.audio")
                                << "underrun: injecting silence (frame_bytes=" << frame_bytes
                                << ") total_queued=" << total_items
                                << " stream_id=" << stream_id;
                        }
                        uint64_t ts;
                        {
                            std::lock_guard<std::mutex> lock(audio_mutex_);
                            ts = nextAudioTimestampLocked(stream_id, 0);
                        }
                        AvFrame silence;
                        silence.ts_us = ts;
                        silence.data.assign(frame_bytes, 0);
                        audio_pipeline_.push(silence.ts_us,
                                              silence.data.data(), silence.data.size());
                        updateAudioClock(silence.ts_us);
                    }
                    if (++no_frame_count_ % 200 == 0)
                    {
                        std::size_t total_items = 0;
                        {
                            std::lock_guard<std::mutex> lock(audio_mutex_);
                            for (auto &kv : audio_queues_) total_items += kv.second.size();
                        }
                        APP_LOG_DEBUG("app.av_core.audio")
                            << "no frame available; total_queued=" << total_items
                            << " pcm_output=" << (pcm_output ? "yes" : "no");
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                if (!pcm_output)
                {
                    uint64_t ts;
                    {
                        std::lock_guard<std::mutex> lock(audio_mutex_);
                        ts = frame.ts_us ? frame.ts_us
                                         : nextAudioTimestampLocked(stream_id, 0);
                        if (frame.ts_us)
                        {
                            uint64_t prev = last_audio_ts_.load();
                            if (ts > prev) last_audio_ts_.store(ts);
                            if (stream_id >= 0) last_audio_ts_per_stream_[stream_id] = ts;
                        }
                    }
                    if (++encoded_out_count_ % 200 == 0)
                        APP_LOG_DEBUG("app.av_core.audio")
                            << "push encoded stream=" << stream_id
                            << " ts=" << ts << " size=" << frame.data.size();
                    audio_pipeline_.push(ts, frame.data.data(), frame.data.size());
                    updateAudioClock(ts);
                    continue;
                }

                // Fix #2: mix PCM FUORI dal lock.
                if (!frames_to_mix.empty())
                {
                    std::vector<int16_t> mix(frame_bytes / 2, 0);
                    for (auto &[samples, gain] : frames_to_mix)
                    {
                        std::size_t n = std::min(mix.size(), samples.size());
                        for (std::size_t s = 0; s < n; ++s)
                        {
                            int32_t v = mix[s] + static_cast<int32_t>(
                                static_cast<float>(samples[s]) * gain);
                            mix[s] = static_cast<int16_t>(std::clamp(v, -32768, 32767));
                        }
                    }
                    mixed = true;
                    uint64_t ts;
                    {
                        std::lock_guard<std::mutex> lock(audio_mutex_);
                        ts = nextAudioTimestampLocked(-1, 0);
                    }
                    AvFrame out;
                    out.ts_us = ts;
                    out.data.resize(mix.size() * sizeof(int16_t));
                    std::memcpy(out.data.data(), mix.data(), out.data.size());
                    if (++mixed_out_count_ % 200 == 0)
                        APP_LOG_DEBUG("app.av_core.audio")
                            << "push mixed ts=" << out.ts_us << " size=" << out.data.size();
                    audio_pipeline_.push(out.ts_us, out.data.data(), out.data.size());
                    updateAudioClock(out.ts_us);
                }
                else if (!mixed)
                {
                    uint64_t ts;
                    {
                        std::lock_guard<std::mutex> lock(audio_mutex_);
                        ts = nextAudioTimestampLocked(stream_id, frame.ts_us);
                    }
                    if (++pcm_out_count_ % 200 == 0)
                        APP_LOG_DEBUG("app.av_core.audio")
                            << "push pcm stream=" << stream_id
                            << " ts=" << ts << " size=" << frame.data.size();
                    audio_pipeline_.push(ts, frame.data.data(), frame.data.size());
                    updateAudioClock(ts);
                }
            }
        }

        // ---------------------------------------------------------- mic loop
        void micLoop()
        {
            const int bytes_per_sample = std::max(1, mic_bits_ / 8);
            const int bytes_per_ms     = (mic_sample_rate_ * mic_channels_ * bytes_per_sample) / 1000;
            const std::size_t target_bytes = static_cast<std::size_t>(
                std::max(cfg_.mic_batch_ms, cfg_.mic_frame_ms) * std::max(1, bytes_per_ms));
            std::vector<uint8_t> batch;
            uint64_t batch_ts = 0;

            while (mic_running_.load())
            {
                AvFrame f;
                if (!mic_capture_.pull(f, 50)) continue;
                if (target_bytes == 0 || f.data.empty())
                {
                    std::lock_guard<std::mutex> lock(mic_mutex_);
                    if (!applyOverrunPolicy(mic_queue_)) continue;
                    mic_queue_.push_back(std::move(f));
                    mic_cv_.notify_one();
                    continue;
                }
                if (batch.empty()) batch_ts = f.ts_us;
                batch.insert(batch.end(), f.data.begin(), f.data.end());
                if (batch.size() >= target_bytes)
                {
                    AvFrame out;
                    out.ts_us = batch_ts;
                    out.data  = std::move(batch);
                    batch.clear(); batch_ts = 0;
                    std::lock_guard<std::mutex> lock(mic_mutex_);
                    if (!applyOverrunPolicy(mic_queue_)) continue;
                    mic_queue_.push_back(std::move(out));
                    mic_cv_.notify_one();
                }
            }
        }

        // ----------------------------------------------- audio helpers (locked)
        bool hasAudioDataLocked() const
        {
            const std::string output_codec = normalizeCodecName(audio_codec_);
            for (const auto &kv : audio_queues_)
            {
                if (kv.second.empty()) continue;
                if (output_codec != "PCM")
                {
                    auto it = audio_stream_formats_.find(kv.first);
                    if (it != audio_stream_formats_.end() &&
                        normalizeCodecName(it->second.codec) != output_codec)
                        continue;
                }
                return true;
            }
            return false;
        }

        int encodedFrameDurationMsLocked(int stream_id) const
        {
            auto it = audio_stream_formats_.find(stream_id);
            if (it == audio_stream_formats_.end()) return 20;
            const auto  &fmt   = it->second;
            const std::string codec = normalizeCodecName(fmt.codec);
            if (codec == "AAC_LC" && fmt.sample_rate > 0)
                return static_cast<int>((1024 * 1000 + fmt.sample_rate - 1) / fmt.sample_rate);
            if (codec == "OPUS") return 20;
            return 20;
        }

        bool groupHasDataLocked(const AudioGroup &g) const
        {
            for (int ch : g.channels)
            {
                auto it = audio_queues_.find(ch);
                if (it != audio_queues_.end() && !it->second.empty()) return true;
            }
            return false;
        }

        int pickTopGroupLocked(uint64_t now_ms)
        {
            if (active_group_ >= 0 && now_ms <= active_until_ms_) return active_group_;
            int top_idx = -1, top_priority = -999999;
            for (std::size_t i = 0; i < audio_groups_.size(); ++i)
            {
                if (!groupHasDataLocked(audio_groups_[i])) continue;
                if (audio_groups_[i].priority > top_priority)
                {
                    top_priority = audio_groups_[i].priority;
                    top_idx      = static_cast<int>(i);
                }
            }
            active_group_    = top_idx;
            active_until_ms_ = (top_idx >= 0)
                ? now_ms + static_cast<uint64_t>(audio_groups_[top_idx].hold_ms)
                : 0;
            return top_idx;
        }

        // Fix #2: drain dentro audio_mutex_ (gia' acquisito), mix PCM fuori.
        std::vector<std::pair<std::vector<int16_t>, float>>
        drainGroupFrames(int top_idx, int top_priority)
        {
            std::vector<std::pair<std::vector<int16_t>, float>> result;
            for (std::size_t gi = 0; gi < audio_groups_.size(); ++gi)
            {
                auto &g     = audio_groups_[gi];
                float gain  = (static_cast<int>(gi) == top_idx)
                    ? 1.0f
                    : ((g.priority < top_priority)
                        ? static_cast<float>(g.ducking) / 100.0f
                        : 1.0f);
                for (std::size_t ci = 0; ci < g.channels.size(); ++ci)
                {
                    int ch  = g.channels[ci];
                    auto it = audio_queues_.find(ch);
                    if (it == audio_queues_.end() || it->second.empty()) continue;
                    AvFrame fm = std::move(it->second.front());
                    it->second.pop_front();
                    float ch_gain = gain;
                    if (!g.channel_gains.empty())
                        ch_gain *= g.channel_gains[
                            std::min(ci, g.channel_gains.size() - 1)];
                    const std::size_t n_samples = fm.data.size() / sizeof(int16_t);
                    std::vector<int16_t> samples(n_samples);
                    std::memcpy(samples.data(), fm.data.data(), fm.data.size());
                    result.emplace_back(std::move(samples), ch_gain);
                }
            }
            return result;
        }

        // Bug B fix: rinominato per segnalare che richiede audio_mutex_ acquisito.
        uint64_t nextAudioTimestampLocked(int stream_id, uint64_t preferred_ts)
        {
            uint64_t last = (stream_id >= 0)
                ? last_audio_ts_per_stream_[stream_id]
                : last_audio_ts_.load();
            uint64_t step = static_cast<uint64_t>(cfg_.audio_frame_ms) * 1000;
            uint64_t next = (last == 0)
                ? (preferred_ts == 0 ? step : preferred_ts)
                : std::max(last + step, preferred_ts > 0 ? preferred_ts : last + step);
            if (++ts_log_count_ % 1000 == 0)
                APP_LOG_DEBUG("app.av_core.audio")
                    << "nextAudioTimestamp stream=" << stream_id
                    << " preferred=" << preferred_ts
                    << " last=" << last << " next=" << next << " step=" << step;
            if (stream_id >= 0) last_audio_ts_per_stream_[stream_id] = next;
            uint64_t prev = last_audio_ts_.load();
            if (next > prev) last_audio_ts_.store(next);
            return next;
        }

        int audioBytesPerFrame() const
        {
            if (audio_sample_rate_ <= 0 || audio_channels_ <= 0 || audio_bits_ <= 0) return 0;
            return (audio_sample_rate_ * cfg_.audio_frame_ms / 1000)
                   * audio_channels_ * (audio_bits_ / 8);
        }

        // ---------------------------------------------------------------- members
        AvCoreConfig cfg_;

        int       video_width_ = 800, video_height_ = 480;
        uintptr_t window_id_   = 0;

        int         audio_sample_rate_ = 16000, audio_channels_ = 1, audio_bits_ = 16;
        std::string audio_codec_       = "PCM";
        int         mic_sample_rate_   = 16000, mic_channels_   = 1, mic_bits_   = 16;

        GstVideoPipeline video_pipeline_;
        GstAudioPipeline audio_pipeline_;
        GstMicCapture    mic_capture_;

        std::atomic<bool> video_running_{false};
        std::atomic<bool> audio_running_{false};
        std::atomic<bool> mic_running_{false};
        std::atomic<bool> mic_active_{false};

        std::thread video_thread_, audio_thread_, mic_thread_;

        std::mutex   audio_dump_mutex_;
        std::string  audio_dump_path_;
        bool         audio_dump_single_ = false;
        std::unordered_map<int, std::ofstream> audio_dump_files_;

        std::deque<AvFrame> video_queue_;
        std::mutex          video_mutex_;
        std::condition_variable video_cv_;

        std::unordered_map<int, std::deque<AvFrame>>     audio_queues_;
        std::unordered_map<int, AudioStreamFormat>       audio_stream_formats_;
        std::vector<uint8_t>   audio_codec_data_;
        std::vector<int>       audio_priority_;
        std::vector<AudioGroup> audio_groups_;
        int      active_group_    = -1;
        uint64_t active_until_ms_ = 0;
        std::mutex              audio_mutex_;
        std::condition_variable audio_cv_;

        std::deque<AvFrame>     mic_queue_;
        std::mutex              mic_mutex_;
        std::condition_variable mic_cv_;

        std::atomic<uint64_t> last_audio_ts_{0};
        std::unordered_map<int, uint64_t> last_audio_ts_per_stream_;

        /// Fix #10: Master Audio Clock. -1 = pipeline non avviata.
        /// Usato da videoLoop() per il gate A/V sync (Fix #3).
        std::atomic<int64_t> audio_clock_us_{-1};

        // Bug A fix: counter promossi a std::atomic<int> per thread-safety.
        std::atomic<int> in_log_count_{0};
        std::atomic<int> codec_mismatch_count_{0};
        std::atomic<int> queue_log_count_{0};
        std::atomic<int> drop_new_count_{0};
        std::atomic<int> drop_old_count_{0};
        std::atomic<int> channel_warn_count_{0};
        std::atomic<int> underrun_count_{0};
        std::atomic<int> no_frame_count_{0};
        std::atomic<int> enc_pop_count_{0};
        std::atomic<int> pop_log_count_{0};
        std::atomic<int> encoded_out_count_{0};
        std::atomic<int> mixed_out_count_{0};
        std::atomic<int> pcm_out_count_{0};
        std::atomic<int> ts_log_count_{0};
        std::atomic<uint64_t> last_info_ms_{0};
    };

} // namespace nemo
