#pragma once

#include "av_types.hpp"
#include "app/core/logging.hpp"

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>

namespace nemo
{

    /**
     * @brief Cattura microfono via GStreamer con opzionale splitter a frame fissi.
     *
     * Il pipeline primario usa audiobuffersplit per garantire frame a dimensione
     * costante (frame_ms ms). In caso di fallback (plugin non disponibile) viene
     * avviato un pipeline senza splitter. pull() blocca al massimo timeout_ms.
     *
     * Variabile d'ambiente riconosciuta:
     *   - NEMO_MIC_SRC  es. "alsasrc device=hw:1" / "pulsesrc"
     */
    class GstMicCapture
    {
    public:
        void init(int sample_rate, int channels, int bits, int frame_ms)
        {
            init_gst_once();

            const char *env_src = std::getenv("NEMO_MIC_SRC");
            std::string src_elem = (env_src && *env_src) ? std::string(env_src) : "autoaudiosrc";
            const std::string format = (bits == 16) ? "S16LE" : "S16LE";

            std::string base_desc =
                src_elem + " ! audioconvert ! audioresample "
                "! audio/x-raw,format=" + format +
                ",channels=" + std::to_string(channels) +
                ",rate=" + std::to_string(sample_rate) + " ";

            auto build_desc = [&](bool with_split) -> std::string
            {
                std::string desc = base_desc;
                if (with_split && frame_ms > 0)
                {
                    desc += "! identity do-timestamp=true ";
                    desc += "! audiobuffersplit output-buffer-duration="
                         + std::to_string(frame_ms * 1000000LL) + " ";
                    desc += "! queue max-size-buffers=8 leaky=downstream ";
                }
                desc += "! appsink name=sink sync=false max-buffers=4 drop=true";
                return desc;
            };

            std::string pipe_desc = build_desc(true);
            APP_LOG_INFO("app.av_core.audio") << "Mic pipeline: " << pipe_desc;

            GError *err = nullptr;
            pipeline_ = gst_parse_launch(pipe_desc.c_str(), &err);
            if (!pipeline_ || err)
            {
                std::string msg = err ? err->message : "unknown";
                if (err) g_error_free(err);
                if (pipeline_) { gst_object_unref(pipeline_); pipeline_ = nullptr; }
                if (frame_ms > 0)
                {
                    std::string fallback = build_desc(false);
                    APP_LOG_WARN("app.av_core.audio")
                        << "Mic pipeline failed with splitter, retrying without: " << msg;
                    APP_LOG_INFO("app.av_core.audio") << "Mic pipeline (fallback): " << fallback;
                    err = nullptr;
                    pipeline_ = gst_parse_launch(fallback.c_str(), &err);
                    if (!pipeline_ || err)
                    {
                        std::string msg2 = err ? err->message : "unknown";
                        if (err) g_error_free(err);
                        if (pipeline_) { gst_object_unref(pipeline_); pipeline_ = nullptr; }
                        throw std::runtime_error("[AvCore] gst_parse_launch mic failed: " + msg2);
                    }
                }
                else
                    throw std::runtime_error("[AvCore] gst_parse_launch mic failed: " + msg);
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

        /// Estrae un frame dal mic; restituisce false se timeout o pipeline ferma.
        bool pull(AvFrame &out, int timeout_ms)
        {
            if (!running_.load() || !appsink_) return false;
            GstSample *sample = gst_app_sink_try_pull_sample(
                appsink_, static_cast<GstClockTime>(timeout_ms) * GST_MSECOND);
            if (!sample) return false;
            GstBuffer *buffer = gst_sample_get_buffer(sample);
            GstMapInfo map;
            if (!buffer || !gst_buffer_map(buffer, &map, GST_MAP_READ))
            {
                gst_sample_unref(sample);
                return false;
            }
            out.data.assign(map.data, map.data + map.size);
            if (GST_BUFFER_PTS_IS_VALID(buffer))
                out.ts_us = static_cast<uint64_t>(GST_BUFFER_PTS(buffer) / GST_USECOND);
            gst_buffer_unmap(buffer, &map);
            gst_sample_unref(sample);
            return true;
        }

        void stop()
        {
            running_.store(false);
            if (appsink_) { gst_object_unref(GST_OBJECT(appsink_)); appsink_ = nullptr; }
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
            std::call_once(flag, [](){ gst_init(nullptr, nullptr); });
        }

        GstElement  *pipeline_ = nullptr;
        GstAppSink  *appsink_  = nullptr;
        std::atomic<bool> running_{false};
    };

} // namespace nemo
