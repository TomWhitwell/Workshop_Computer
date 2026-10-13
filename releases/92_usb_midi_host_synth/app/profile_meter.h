// On-hardware ProcessSample() duration meter for profiling (Phase 1).
//
// Core 0 calls beginSample/endSample from the 48 kHz ISR. Core 1 reads peak
// and overrun via SysEx for the web editor.
#pragma once

#include <cstdint>

#include "pico/time.h"

constexpr uint32_t kProcessSampleBudgetUs = 20;

class ProfileMeter
{
public:
    void beginSample() { start_us_ = time_us_32(); }

    void endSample()
    {
        uint32_t elapsed = time_us_32() - start_us_;
        if (elapsed > window_peak_us_)
            window_peak_us_ = elapsed;
        if (elapsed > kProcessSampleBudgetUs)
            overrun_ = true;

        if (++window_count_ >= kWindowSamples)
        {
            peak_us_ = window_peak_us_;
            window_peak_us_ = 0;
            window_count_ = 0;
        }
    }

    uint32_t peakUs() const { return peak_us_; }
    bool overrun() const { return overrun_; }

private:
    // ~170 ms at 48 kHz — fast enough to feel live in the web UI.
    static constexpr uint32_t kWindowSamples = 8192;

    uint32_t start_us_ = 0;
    uint32_t window_peak_us_ = 0;
    uint32_t window_count_ = 0;
    volatile uint32_t peak_us_ = 0;
    volatile bool overrun_ = false;
};

extern ProfileMeter g_processSampleMeter;
