#pragma once

#include "av_types.hpp"
#include "av_utils.hpp"
#include "app/core/logging.hpp"

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace nemo
{

    /**
     * @brief Pipeline GStreamer per la riproduzione audio (PCM / AAC-LC / OPUS).
     *
     * Fix #4: push() e' lock-free sul path critico.
     * caps_mutex_ e' usato SOLO per applyCaps (path raro, quando caps_dirty_=true)
     * e per stop(). La gst_app_src_push_buffer avviene senza alcun lock.
     *
     * Variabili d'ambiente riconosciute:
     *   - NEMO_AUDIO_SINK       es. "pulsesink" / "alsasink"
     *   - NEMO_AUDIO_DECODER    sovrascrive il decoder automatico
     *   - NEMO_AUDIO_BUFFER_MS  dimensione del queue GStreamer in ms (default 100)
     */
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
                try { buffer_ms = std::max(0, std::stoi(env_buf)); }
                catch (...) { buffer_ms = 100; }
            }

            const std::string codec_norm = normalizeCodecName(codec);
            const bool pcm = (codec_norm == "PCM");
            sample_rate_ = sample_rate;
            channels_    = channels;
            bits_        = bits;
            codec_norm_  = codec_norm;

            if (bits != 16)
                APP_LOG_WARN("app.av_core.audio") << "only 16-bit PCM supported in mixer; forcing S16LE";

            const std::string format = "S16LE";
            std::string caps, decoder;

            if (pcm)
                caps = "audio/x-raw,format=" + format +
                       ",channels=" + std::to_string(channels) +
                       ",rate=" + std::to_string(sample_rate);
            else if (codec_norm == "AAC_LC")
            {
                caps    = "audio/mpeg,mpegversion=4,stream-format=raw,framed=true"
                          ",channels=" + std::to_string(channels) +
                          ",rate=" + std::to_string(sample_rate);
                decoder = "avdec_aac";
            }
            else if (codec_norm == "OPUS")
            {
                caps    = "audio/x-opus,channels=" + std::to_string(channels) +
                          ",rate=" + std::to_string(sample_rate);
                decoder = "opusparse ! opusdec";
            }
            else
            {
                caps    = "audio/x-raw,format=" + format +
                          ",channels=" + std::to_string(channels) +
                          ",rate=" + std::to_string(sample_rate);
                decoder = "decodebin";
            }
            caps_base_ = caps;

            const char *env_dec = std::getenv("NEMO_AUDIO_DECODER");
            if (env_dec && *env_dec) decoder = std::string(env_dec);

            std::string caps_full  = buildCapsString();
            std::string queue_desc = "queue name=q max-size-buffers=32 leaky=downstream";
            if (buffer_ms > 0)
            {
                const int64_t max_time_ns = static_cast<int64_t>(buffer_ms) * 1000 * 1000;
                queue_desc += " max-size-time=" + std::to_string(max_time_ns) + " max-size-bytes=0";
            }

            std::string pipe_desc = "appsrc name=src format=time is-live=true caps=" + caps_full +
                                    " ! " + queue_desc + " ";
            if (!decoder.empty() && !pcm)
                pipe_desc += "! " + decoder + " ";
            pipe_desc += "! audioconvert ! audioresample ! " + sink_elem + " sync=false";

            APP_LOG_INFO("app.av_core.audio") << "Gst pipeline: " << pipe_desc;
            APP_LOG_INFO("app.av_core.audio")
                << "codec=" << codec_norm << " pcm=" << (pcm ? "yes" : "no")
                << " caps=" << caps_full
                << " decoder=" << (decoder.empty() ? "<none>" : decoder)
                << " sink=" << sink_elem;

            GError *err = nullptr;
            pipeline_ = gst_parse_launch(pipe_desc.c_str(), &err);
            if (!pipeline_ || err)
            {
                std::string msg = err ? err->message : "unknown";
                if (err) g_error_free(err);
                throw std::runtime_error("[AvCore] gst_parse_launch audio failed: " + msg);
            }

            appsrc_ = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline_), "src"));
            if (!appsrc_)
                throw std::runtime_error("[AvCore] audio appsrc not found");
            {
                std::lock_guard<std::mutex> lock(caps_mutex_);
                applyCapsLocked();
            }
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

        /**
         * @brief Push lock-free sul path critico (Fix #4).
         *
         * caps_dirty_ e' controllato con exchange; solo se dirty acquisisce caps_mutex_
         * (evento raro: cambio codec_data AAC). La gst_app_src_push_buffer e' sempre
         * senza lock: GStreamer garantisce thread-safety su appsrc push concorrente.
         */
        void push(uint64_t ts_us, const uint8_t *data, std::size_t size)
        {
            if (!running_.load() || !appsrc_) return;
            if (size == 0)
            {
                APP_LOG_WARN("app.av_core.audio") << "push called with size=0";
                return;
            }
            if (caps_dirty_.exchange(false))
            {
                std::lock_guard<std::mutex> lock(caps_mutex_);
                applyCapsLocked();
                APP_LOG_INFO("app.av_core.audio") << "updated appsrc caps=" << buildCapsString();
            }
            GstBuffer *buf = gst_buffer_new_memdup(data, static_cast<gsize>(size));
            GST_BUFFER_PTS(buf) = static_cast<GstClockTime>(ts_us) * GST_USECOND;
            GST_BUFFER_DTS(buf) = GST_BUFFER_PTS(buf);
            gst_app_src_push_buffer(appsrc_, buf);
        }

        void stop()
        {
            running_.store(false);
            std::lock_guard<std::mutex> lock(caps_mutex_);
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

        /// Aggiorna i caps AAC codec_data (segnala caps_dirty_ per il prossimo push).
        void setCodecData(const std::vector<uint8_t> &codec_data)
        {
            std::lock_guard<std::mutex> lock(caps_mutex_);
            codec_data_ = codec_data;
            caps_dirty_.store(true);
        }

    private:
        std::string buildCapsString() const
        {
            std::string caps = caps_base_;
            if (!codec_data_.empty())
                caps += ",codec_data=(buffer)" + bytesToHex(codec_data_);
            return caps;
        }

        void applyCapsLocked()
        {
            if (!appsrc_ || caps_base_.empty()) return;
            GstCaps *caps = gst_caps_from_string(buildCapsString().c_str());
            if (caps) { gst_app_src_set_caps(appsrc_, caps); gst_caps_unref(caps); }
        }

        void logPipelineState()
        {
            if (!pipeline_) return;
            GstState current = GST_STATE_NULL, pending = GST_STATE_NULL;
            GstStateChangeReturn ret = gst_element_get_state(pipeline_, &current, &pending, 2 * GST_SECOND);
            APP_LOG_INFO("app.av_core.audio")
                << "pipeline state ret=" << static_cast<int>(ret)
                << " current=" << gst_element_state_get_name(current)
                << " pending=" << gst_element_state_get_name(pending);
        }

        static std::string capsToString(GstCaps *caps)
        {
            if (!caps) return "<null>";
            gchar *s = gst_caps_to_string(caps);
            std::string out = s ? s : "<null>";
            if (s) g_free(s);
            return out;
        }

        void logNegotiatedCaps()
        {
            if (!pipeline_) return;
            GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
            if (appsrc_)
            {
                GstCaps *caps = gst_app_src_get_caps(appsrc_);
                APP_LOG_INFO("app.av_core.audio") << "appsrc caps=" << capsToString(caps);
                if (caps) gst_caps_unref(caps);
            }
            if (sink)
            {
                GstPad *pad = gst_element_get_static_pad(sink, "sink");
                if (pad)
                {
                    GstCaps *caps = gst_pad_get_current_caps(pad);
                    APP_LOG_INFO("app.av_core.audio") << "sink caps=" << capsToString(caps);
                    if (caps) gst_caps_unref(caps);
                    gst_object_unref(pad);
                }
                gst_object_unref(sink);
            }
        }

        static void init_gst_once()
        {
            static std::once_flag flag;
            std::call_once(flag, [](){ gst_init(nullptr, nullptr); });
        }

        GstElement *pipeline_ = nullptr;
        GstAppSrc  *appsrc_   = nullptr;
        std::atomic<bool> running_{false};

        /// Fix #4: caps_mutex_ protegge SOLO le operazioni sui caps (path raro).
        std::mutex           caps_mutex_;
        int                  sample_rate_{0}, channels_{0}, bits_{0};
        std::string          codec_norm_;
        std::string          caps_base_;
        std::vector<uint8_t> codec_data_;
        std::atomic<bool>    caps_dirty_{false};
    };

} // namespace nemo
