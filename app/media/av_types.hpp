#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
        int jitter_buffer_ms  = 120;
        int max_queue_frames  = 64;
        int audio_frame_ms    = 20;
        int max_av_lead_ms    = 80;
        int mic_frame_ms      = 20;
        int mic_batch_ms      = 100;
        int audio_prebuffer_ms = 100;
        OverrunPolicy  overrun_policy  = OverrunPolicy::DROP_OLD;
        UnderrunPolicy underrun_policy = UnderrunPolicy::SILENCE;
    };

    struct AvFrame
    {
        uint64_t             ts_us = 0;
        std::vector<uint8_t> data;
    };

    struct AudioGroup
    {
        std::vector<int>   channels;
        int                priority = 0;
        int                ducking  = 100;
        int                hold_ms  = 100;
        std::vector<float> channel_gains;
    };

    struct AudioStreamFormat
    {
        int         sample_rate = 16000;
        int         channels    = 1;
        int         bits        = 16;
        std::string codec       = "PCM";
    };

} // namespace nemo
