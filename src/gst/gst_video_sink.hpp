#pragma once

#include <memory>
#include <cstdint>
#include <stdexcept>
#include <iostream>
#include "gst/gst_pipeline.hpp"

namespace nemo
{

    // -----------------------------------------------------------------------
    // GstVideoSink
    // -----------------------------------------------------------------------
    // Facade tra Python e GstPipeline.
    //
    // Regola GIL:
    //   - setWindowId / startPipeline / stop -> chiamati dal thread Python
    //     startPipeline() NON propaga eccezioni verso Python: le logga e
    //     imposta pipeline_ a nullptr (sicuro per pushBuffer).
    //   - pushBuffer() -> chiamato SOLO da VideoEventHandler (C++ Boost.Asio)
    //     Non tocca mai Python.
    // -----------------------------------------------------------------------

    class GstVideoSink
    {
    public:
        using Pointer = std::shared_ptr<GstVideoSink>;

        GstVideoSink(int width = 800, int height = 480)
            : width_(width), height_(height) {}

        ~GstVideoSink() { stop(); }

        void setWindowId(guintptr wid)
        {
            window_id_ = wid;
            std::cout << "[GstVideoSink] window_id impostato: " << wid << std::endl;
        }

        // Idempotente. Cattura std::exception e le logga senza
        // propagarle — evita pybind11::error_already_set al chiamante Python.
        void startPipeline()
        {
            if (pipeline_ && pipeline_->isRunning()) {
                std::cout << "[GstVideoSink] Pipeline già attiva, skip." << std::endl;
                return;
            }
            try {
                pipeline_ = std::make_shared<GstPipeline>();
                pipeline_->init(window_id_, width_, height_);
            } catch (const std::exception &e) {
                std::cerr << "[GstVideoSink] ERRORE startPipeline: "
                          << e.what() << std::endl;
                std::cerr << "[GstVideoSink] Suggerimenti:" << std::endl
                          << "  x86_64  : export NEMO_VIDEO_SINK=autovideosink" << std::endl
                          << "  Wayland : export NEMO_VIDEO_SINK=waylandsink" << std::endl
                          << "  Headless: export NEMO_VIDEO_SINK=fakesink" << std::endl
                          << "  RPi4    : export NEMO_VIDEO_DECODER=v4l2h264dec" << std::endl;
                pipeline_.reset(); // pushBuffer sara' no-op finche' non si riprova
            }
        }

        // Chiamato da VideoEventHandler (thread Boost.Asio) — NO GIL
        void pushBuffer(uint64_t ts_us, const uint8_t *data, std::size_t size)
        {
            if (pipeline_) pipeline_->pushBuffer(ts_us, data, size);
        }

        void stop()
        {
            if (pipeline_) {
                pipeline_->stop();
                pipeline_.reset();
            }
        }

        bool isRunning() const
        {
            return pipeline_ && pipeline_->isRunning();
        }

        int width()  const { return width_; }
        int height() const { return height_; }

    private:
        int      width_     = 800;
        int      height_    = 480;
        guintptr window_id_ = 0;
        std::shared_ptr<GstPipeline> pipeline_;
    };

} // namespace nemo
