#pragma once

#include "av_types.hpp"
#include "av_utils.hpp"
#include "app/core/logging.hpp"

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/videooverlay.h>

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>

namespace nemo
{

    /**
     * @brief Pipeline GStreamer per la decodifica e la presentazione video H.264.
     *
     * Ciclo di vita: init() → push() (N volte) → stop().
     * Thread-safety: push() e' safe da un singolo producer thread.
     * Le variabili d'ambiente riconosciute sono:
     *   - NEMO_VIDEO_DECODER  es. "avdec_h264 max-threads=4"
     *   - NEMO_VIDEO_SINK     es. "glimagesink" / "waylandsink"
     */
    class GstVideoPipeline
    {
    public:
        void init(uintptr_t window_id, int width, int height)
        {
            init_gst_once();

            const char *env_dec = std::getenv("NEMO_VIDEO_DECODER");
            std::string decoder = (env_dec && *env_dec)
                ? std::string(env_dec)
                : "avdec_h264 max-threads=2";

            const char *env_sink = std::getenv("NEMO_VIDEO_SINK");
            std::string sink_elem = (env_sink && *env_sink)
                ? std::string(env_sink)
                : (window_id ? "xvimagesink" : "autovideosink");

            std::string pipe_desc =
                "appsrc name=src format=time is-live=true "
                "caps=video/x-h264,stream-format=byte-stream,alignment=au,framerate=30/1,"
                "width="  + std::to_string(width) +
                ",height=" + std::to_string(height) +
                " ! queue name=q max-size-buffers=8 leaky=downstream "
                "! h264parse config-interval=-1 "
                "! " + decoder +
                " ! videoconvert "
                "! " + sink_elem + " name=sink sync=false";

            GError *err = nullptr;
            pipeline_ = gst_parse_launch(pipe_desc.c_str(), &err);
            if (!pipeline_ || err)
            {
                std::string msg = err ? err->message : "unknown";
                if (err) g_error_free(err);
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
                        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sink), window_id);
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

        /**
         * @brief Invia un NAL-unit H.264 alla pipeline (zero-copy via gst_buffer_new_memdup).
         * @note  Non acquisisce alcun lock: GStreamer garantisce thread-safety su appsrc.
         */
        void push(uint64_t ts_us, const uint8_t *data, std::size_t size)
        {
            if (!running_.load() || !appsrc_) return;
            GstBuffer *buf = gst_buffer_new_memdup(data, static_cast<gsize>(size));
            GST_BUFFER_PTS(buf) = static_cast<GstClockTime>(ts_us) * GST_USECOND;
            GST_BUFFER_DTS(buf) = GST_BUFFER_PTS(buf);
            GstFlowReturn ret = gst_app_src_push_buffer(appsrc_, buf);
            if (ret != GST_FLOW_OK)
            {
                APP_LOG_WARN("app.av_core.video")
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
            std::call_once(flag, [](){ gst_init(nullptr, nullptr); });
        }

        GstElement *pipeline_ = nullptr;
        GstAppSrc  *appsrc_   = nullptr;
        std::atomic<bool> running_{false};
    };

} // namespace nemo
