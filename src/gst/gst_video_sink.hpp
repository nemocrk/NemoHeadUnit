#pragma once

#include <memory>
#include <cstdint>
#include "gst/gst_pipeline.hpp"

namespace nemo
{

    // -----------------------------------------------------------------------
    // GstVideoSink
    // -----------------------------------------------------------------------
    // Wrapper attorno a GstPipeline che viene esposto a Python tramite
    // pybind11 nella classe `GstVideoSink`.
    //
    // Python interagisce SOLO con:
    //   - set_window_id(wid)   -> passa il WId del VideoWidget
    //   - start_pipeline()     -> avvia la pipeline (triggerato da
    //                             on_video_channel_open_request)
    //   - stop()               -> ferma la pipeline (chiamato in closeEvent)
    //   - is_running()         -> polling di stato
    //
    // VideoEventHandler (C++) chiama pushBuffer() direttamente — mai via Python.
    // -----------------------------------------------------------------------

    class GstVideoSink
    {
    public:
        using Pointer = std::shared_ptr<GstVideoSink>;

        GstVideoSink(int width = 800, int height = 480)
            : width_(width), height_(height) {}

        ~GstVideoSink()
        {
            stop();
        }

        // Chiamato da Python prima di start_pipeline()
        void setWindowId(guintptr wid)
        {
            window_id_ = wid;
        }

        // Chiamato da Python quando on_video_channel_open_request è triggerato
        // Idempotente: una seconda chiamata è no-op.
        void startPipeline()
        {
            if (pipeline_ && pipeline_->isRunning()) return;
            pipeline_ = std::make_shared<GstPipeline>();
            pipeline_->init(window_id_, width_, height_);
        }

        // Chiamato da VideoEventHandler (thread Boost.Asio) — NO GIL
        void pushBuffer(uint64_t ts_us, const uint8_t *data, std::size_t size)
        {
            if (pipeline_) pipeline_->pushBuffer(ts_us, data, size);
        }

        void stop()
        {
            if (pipeline_)
            {
                pipeline_->stop();
                pipeline_.reset();
            }
        }

        bool isRunning() const
        {
            return pipeline_ && pipeline_->isRunning();
        }

        int  width()  const { return width_; }
        int  height() const { return height_; }

    private:
        int      width_     = 800;
        int      height_    = 480;
        guintptr window_id_ = 0;
        std::shared_ptr<GstPipeline> pipeline_;
    };

} // namespace nemo
