#pragma once

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/videooverlay.h>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <cstdlib>

namespace nemo
{

    // -----------------------------------------------------------------------
    // GstPipeline
    // -----------------------------------------------------------------------
    // Incapsula una pipeline GStreamer zero-copy per decodifica H.264 e
    // rendering hardware dentro una finestra X11/Wayland fornita da PyQt6.
    //
    // Regola architetturale (invariante Fase 5):
    //   I buffer NAL units NON attraversano mai il GIL di Python.
    //   Python passa esclusivamente un guintptr (WId) all'avvio.
    //   Tutta la pipeline gira su thread GStreamer interni, separati da
    //   Boost.Asio e dal thread Python.
    //
    // Thread-safety:
    //   - init()       -> chiamato una sola volta dal thread Python (via GstVideoSink)
    //   - pushBuffer() -> chiamato dal thread Boost.Asio (VideoEventHandler)
    //   - stop()       -> chiamato dal thread Python (closeEvent)
    //   L'accesso a appsrc_ è protetto da running_ (atomic) come guard.
    // -----------------------------------------------------------------------

    class GstPipeline
    {
    public:
        using Pointer = std::shared_ptr<GstPipeline>;

        GstPipeline() = default;

        ~GstPipeline()
        {
            stop();
        }

        // -------------------------------------------------------------------
        // init()
        // -------------------------------------------------------------------
        // window_handle : WId X11/Wayland del VideoWidget PyQt6 (0 = standalone)
        // width, height : risoluzione negoziata con Android Auto
        // -------------------------------------------------------------------
        void init(guintptr window_handle = 0, int width = 800, int height = 480)
        {
            gst_init(nullptr, nullptr);

            // ── Selezione decoder via env var per supporto multi-arch ────────
            // export NEMO_VIDEO_DECODER=v4l2h264dec    (Raspberry Pi 4)
            // export NEMO_VIDEO_DECODER=omxh264dec     (Raspberry Pi 3 legacy)
            // export NEMO_VIDEO_DECODER=avdec_h264     (x86_64, default)
            const char *env_dec = std::getenv("NEMO_VIDEO_DECODER");
            std::string decoder = (env_dec && *env_dec)
                                      ? std::string(env_dec)
                                      : "avdec_h264 max-threads=2";

            // ── Selezione sink via env var ────────────────────────────────────
            // export NEMO_VIDEO_SINK=waylandsink  (Wayland)
            // export NEMO_VIDEO_SINK=kmssink      (DRM/KMS headless)
            // export NEMO_VIDEO_SINK=glimagesink  (OpenGL)
            // export NEMO_VIDEO_SINK=autovideosink (debug)
            const char *env_sink = std::getenv("NEMO_VIDEO_SINK");
            std::string sink_elem = (env_sink && *env_sink)
                                        ? std::string(env_sink)
                                        : "xvimagesink";

            // ── Descrizione pipeline ──────────────────────────────────────────
            // queue leaky=downstream: scartiamo frame in coda se il decoder
            // non riesce a stare al passo (meglio artefatti che freeze totale).
            std::string pipe_desc =
                "appsrc name=src format=time is-live=true "
                "  caps=video/x-h264,stream-format=byte-stream,alignment=au,"
                "framerate=30/1,"
                "width=" + std::to_string(width) + ","
                "height=" + std::to_string(height) + " "
                "! queue name=q max-size-buffers=8 leaky=downstream "
                "! h264parse config-interval=-1 "
                "! " + decoder + " "
                "! videoconvert "
                "! " + sink_elem + " name=sink sync=false";

            std::cout << "[GstPipeline] Descrizione: " << pipe_desc << std::endl;

            GError *err = nullptr;
            pipeline_ = gst_parse_launch(pipe_desc.c_str(), &err);
            if (!pipeline_ || err)
            {
                std::string msg = err ? err->message : "unknown";
                if (err) g_error_free(err);
                throw std::runtime_error("[GstPipeline] gst_parse_launch FAILED: " + msg);
            }

            // ── Recupero appsrc ───────────────────────────────────────────────
            appsrc_ = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline_), "src"));
            if (!appsrc_)
                throw std::runtime_error("[GstPipeline] elemento 'src' non trovato nella pipeline");

            // ── Embed nella finestra PyQt6 ────────────────────────────────────
            // Nota: il WId deve essere già valido (widget.show() chiamato
            // prima di passare qui). Se == 0 la pipeline apre una finestra
            // standalone (utile per test headless).
            if (window_handle != 0)
            {
                GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
                if (sink)
                {
                    gst_video_overlay_set_window_handle(
                        GST_VIDEO_OVERLAY(sink),
                        window_handle);
                    gst_object_unref(sink);
                    std::cout << "[GstPipeline] WId impostato: " << window_handle << std::endl;
                }
                else
                {
                    std::cerr << "[GstPipeline] WARN: sink '" << sink_elem
                              << "' non supporta VideoOverlay, WId ignorato." << std::endl;
                }
            }

            // ── Avvio pipeline ────────────────────────────────────────────────
            GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
            if (ret == GST_STATE_CHANGE_FAILURE)
                throw std::runtime_error("[GstPipeline] set_state(PLAYING) FAILED");

            running_.store(true);
            std::cout << "[GstPipeline] Pipeline avviata ("
                      << width << "x" << height << ") decoder=" << decoder
                      << " sink=" << sink_elem << std::endl;
        }

        // -------------------------------------------------------------------
        // pushBuffer()
        // -------------------------------------------------------------------
        // Chiamato da VideoEventHandler::onMediaWithTimestampIndication
        // nel thread Boost.Asio — MAI dal thread Python.
        //
        // Ownership dei byte: aasdk mantiene il buffer valido per tutta la
        // durata della callback. Usiamo gst_buffer_new_memdup (una copia)
        // per trasferire ownership a GStreamer in modo sicuro.
        // -------------------------------------------------------------------
        void pushBuffer(uint64_t ts_us,
                        const uint8_t *data,
                        std::size_t    size)
        {
            if (!running_.load() || !appsrc_) return;

            GstBuffer *buf = gst_buffer_new_memdup(
                static_cast<const void *>(data),
                static_cast<gsize>(size));

            // Timestamp: aasdk fornisce microsecondi, GStreamer vuole nanosecondi
            GST_BUFFER_PTS(buf) = static_cast<GstClockTime>(ts_us) * GST_USECOND;
            GST_BUFFER_DTS(buf) = GST_BUFFER_PTS(buf);

            // gst_app_src_push_buffer prende ownership di buf — NON fare unref.
            GstFlowReturn flow = gst_app_src_push_buffer(appsrc_, buf);
            if (flow != GST_FLOW_OK && flow != GST_FLOW_FLUSHING)
            {
                std::cerr << "[GstPipeline] pushBuffer flow error: " << flow << std::endl;
            }
        }

        // -------------------------------------------------------------------
        // stop()
        // -------------------------------------------------------------------
        void stop()
        {
            if (!running_.exchange(false)) return;

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
            std::cout << "[GstPipeline] Fermata." << std::endl;
        }

        bool isRunning() const { return running_.load(); }

    private:
        GstElement       *pipeline_ = nullptr;
        GstAppSrc        *appsrc_   = nullptr;
        std::atomic<bool> running_{false};
    };

} // namespace nemo
