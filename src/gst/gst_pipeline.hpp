#pragma once

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/video/videooverlay.h>
#include <iostream>
#include <stdexcept>
#include <atomic>
#include <cstdlib>
#include <string>

namespace nemo
{

    class GstPipeline
    {
    public:
        using Pointer = std::shared_ptr<GstPipeline>;

        GstPipeline() = default;
        ~GstPipeline() { stop(); }

        // -------------------------------------------------------------------
        // init()
        // window_handle : WId X11/Wayland (0 = modalità standalone/test)
        // width, height : risoluzione negoziata con Android Auto
        // -------------------------------------------------------------------
        void init(guintptr window_handle = 0, int width = 800, int height = 480)
        {
            gst_init(nullptr, nullptr);

            // ── Decoder (env var per multi-arch) ─────────────────────────────
            // NEMO_VIDEO_DECODER=v4l2h264dec   -> Raspberry Pi 4
            // NEMO_VIDEO_DECODER=omxh264dec    -> Raspberry Pi 3 legacy
            // NEMO_VIDEO_DECODER=avdec_h264    -> x86_64 (default)
            const char *env_dec = std::getenv("NEMO_VIDEO_DECODER");
            std::string decoder = (env_dec && *env_dec)
                                      ? std::string(env_dec)
                                      : "avdec_h264 max-threads=2";

            // ── Sink (env var, con fallback automatico su WId) ───────────────
            // Se NEMO_VIDEO_SINK è impostato viene usato sempre.
            // Se non è impostato:
            //   WId != 0  -> xvimagesink  (embedded nel widget PyQt6)
            //   WId == 0  -> autovideosink (finestra standalone, test headless)
            const char *env_sink = std::getenv("NEMO_VIDEO_SINK");
            std::string sink_elem;
            if (env_sink && *env_sink) {
                sink_elem = std::string(env_sink);
            } else {
                sink_elem = (window_handle != 0) ? "xvimagesink" : "autovideosink";
            }

            // ── Descrizione pipeline ─────────────────────────────────────────
            std::string pipe_desc =
                "appsrc name=src format=time is-live=true "
                "  caps=video/x-h264,stream-format=byte-stream,alignment=au,"
                "framerate=30/1,"
                "width="  + std::to_string(width)  + ","
                "height=" + std::to_string(height) + " "
                "! queue name=q max-size-buffers=8 leaky=downstream "
                "! h264parse config-interval=-1 "
                "! " + decoder + " "
                "! videoconvert "
                "! " + sink_elem + " name=sink sync=false";

            std::cout << "[GstPipeline] Pipeline: " << pipe_desc << std::endl;

            GError *err = nullptr;
            pipeline_ = gst_parse_launch(pipe_desc.c_str(), &err);
            if (!pipeline_ || err) {
                std::string msg = err ? err->message : "unknown";
                if (err) g_error_free(err);
                throw std::runtime_error("[GstPipeline] gst_parse_launch FAILED: " + msg);
            }

            // ── Recupero appsrc ──────────────────────────────────────────────
            appsrc_ = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(pipeline_), "src"));
            if (!appsrc_)
                throw std::runtime_error("[GstPipeline] elemento 'src' non trovato nella pipeline");

            // ── Embed nel WId (solo se valido e sink supporta VideoOverlay) ──
            if (window_handle != 0) {
                GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
                if (sink) {
                    if (GST_IS_VIDEO_OVERLAY(sink)) {
                        gst_video_overlay_set_window_handle(
                            GST_VIDEO_OVERLAY(sink), window_handle);
                        std::cout << "[GstPipeline] WId impostato: "
                                  << window_handle << std::endl;
                    } else {
                        std::cerr << "[GstPipeline] WARN: '" << sink_elem
                                  << "' non supporta VideoOverlay." << std::endl;
                    }
                    gst_object_unref(sink);
                }
            }

            // ── Avvio pipeline ───────────────────────────────────────────────
            GstStateChangeReturn ret =
                gst_element_set_state(pipeline_, GST_STATE_PLAYING);

            if (ret == GST_STATE_CHANGE_FAILURE) {
                // Leggi il primo messaggio di errore dal bus per diagnostica
                std::string detail = _readBusError();
                // Cleanup prima di lanciare
                gst_object_unref(GST_OBJECT(appsrc_));
                appsrc_ = nullptr;
                gst_object_unref(pipeline_);
                pipeline_ = nullptr;
                throw std::runtime_error(
                    "[GstPipeline] set_state(PLAYING) FAILED. "
                    "Sink=" + sink_elem + " Decoder=" + decoder +
                    (detail.empty() ? "" : " | Bus: " + detail));
            }

            running_.store(true);
            std::cout << "[GstPipeline] Avviata ("
                      << width << "x" << height
                      << ") decoder=" << decoder
                      << " sink=" << sink_elem << std::endl;
        }

        // -------------------------------------------------------------------
        // pushBuffer() — chiamato dal thread Boost.Asio, MAI dal thread Python
        // Ownership trasferita a GStreamer via gst_buffer_new_memdup.
        // -------------------------------------------------------------------
        void pushBuffer(uint64_t ts_us, const uint8_t *data, std::size_t size)
        {
            if (!running_.load() || !appsrc_) return;

            GstBuffer *buf = gst_buffer_new_memdup(
                static_cast<const void *>(data),
                static_cast<gsize>(size));

            // aasdk fornisce microsecondi, GStreamer vuole nanosecondi
            GST_BUFFER_PTS(buf) = static_cast<GstClockTime>(ts_us) * GST_USECOND;
            GST_BUFFER_DTS(buf) = GST_BUFFER_PTS(buf);

            GstFlowReturn flow = gst_app_src_push_buffer(appsrc_, buf);
            if (flow != GST_FLOW_OK && flow != GST_FLOW_FLUSHING) {
                std::cerr << "[GstPipeline] pushBuffer flow error: "
                          << gst_flow_get_name(flow) << std::endl;
            }
        }

        // -------------------------------------------------------------------
        // stop()
        // -------------------------------------------------------------------
        void stop()
        {
            if (!running_.exchange(false)) return;

            if (appsrc_) {
                gst_app_src_end_of_stream(appsrc_);
                gst_object_unref(GST_OBJECT(appsrc_));
                appsrc_ = nullptr;
            }
            if (pipeline_) {
                gst_element_set_state(pipeline_, GST_STATE_NULL);
                gst_object_unref(pipeline_);
                pipeline_ = nullptr;
            }
            std::cout << "[GstPipeline] Fermata." << std::endl;
        }

        bool isRunning() const { return running_.load(); }

    private:
        // -------------------------------------------------------------------
        // _readBusError()
        // Legge il primo GST_MESSAGE_ERROR dal bus (timeout 2s) per fornire
        // un messaggio diagnostico leggibile in caso di FAILURE.
        // -------------------------------------------------------------------
        std::string _readBusError()
        {
            if (!pipeline_) return "";
            GstBus *bus = gst_element_get_bus(pipeline_);
            if (!bus) return "";

            std::string detail;
            // Timeout 2 secondi (in nanosecondi)
            GstMessage *msg = gst_bus_timed_pop_filtered(
                bus,
                2 * GST_SECOND,
                static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING));

            if (msg) {
                GError *gerr  = nullptr;
                gchar  *dbg   = nullptr;
                if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                    gst_message_parse_error(msg, &gerr, &dbg);
                } else {
                    gst_message_parse_warning(msg, &gerr, &dbg);
                }
                if (gerr) {
                    detail = std::string(gerr->message);
                    if (dbg) detail += std::string(" [debug: ") + dbg + "]";
                    g_error_free(gerr);
                }
                if (dbg) g_free(dbg);
                gst_message_unref(msg);
            }
            gst_object_unref(bus);
            return detail;
        }

        GstElement       *pipeline_ = nullptr;
        GstAppSrc        *appsrc_   = nullptr;
        std::atomic<bool> running_{false};
    };

} // namespace nemo
