#include "bloom.hpp"
#include "../dsp/fixed.hpp"
#include "config.hpp"

namespace computer {
namespace {
constexpr unsigned analysisSamples = timing::voiceRate / 1000;
constexpr unsigned pulseSamples = timing::voiceRate * 5 / 1000;
constexpr unsigned qualificationMs = 20;
constexpr unsigned rearmMs = 30;
constexpr unsigned refractoryMs = 200;
constexpr unsigned dampInhibitMs = 350;
constexpr int32_t minimumLevelQ8 = 24 * 256;
constexpr int32_t minimumRiseQ8 = 12 * 256;
constexpr int32_t minimumTrendQ8 = 16;
static_assert(timing::voiceRate % 1000 == 0);
} // namespace

void BloomDetector::suppress() {
    inhibitMs = dampInhibitMs;
    pulseFrames = 0;
    risingMs = settledMs = 0;
    armed = false;
}

void BloomDetector::advance(int32_t haloQ15, bool validFrame) {
    if (pulseFrames > 0) {
        --pulseFrames;
    }
    if (!validFrame) {
        // Queue recovery/fallback is not a genuine physical-model bloom.
        suppress();
    }
    magnitudeSum += mtm::abs32(mtm::clip(haloQ15));
    if (++samples == analysisSamples) {
        analyse(magnitudeSum / static_cast<int32_t>(analysisSamples));
        samples = 0;
        magnitudeSum = 0;
    }
}

void BloomDetector::analyse(int32_t meanMagnitude) {
    // Work at 1 kHz after averaging full-wave magnitude. Matched rising/falling
    // smoothing rejects individual audio cycles; slow is the adaptive baseline.
    // Extra fractional bits avoid biased one-LSB steps dominating quiet tails.
    const int32_t targetQ8 = meanMagnitude * 256;
    const int32_t previousFastQ8 = fastQ8;
    fastQ8 = mtm::approach(fastQ8, targetQ8, 5); // About 32 ms.
    slowQ8 = mtm::approach(slowQ8, targetQ8, 8); // About 256 ms.
    trendQ8 = mtm::approach(trendQ8, fastQ8 - previousFastQ8, 3);
    if (holdoffMs > 0) {
        --holdoffMs;
    }
    if (inhibitMs > 0) {
        --inhibitMs;
        risingMs = settledMs = 0;
        return;
    }

    const int32_t rise = fastQ8 - slowQ8;
    const int32_t threshold = std::max<int32_t>(minimumRiseQ8, slowQ8 / 5);
    if (!armed) {
        // Hysteresis: a continuous swell cannot retrigger after holdoff alone.
        if (holdoffMs == 0 && rise <= threshold / 2) {
            if (++settledMs >= rearmMs) {
                armed = true;
                settledMs = 0;
            }
        } else {
            settledMs = 0;
        }
        return;
    }

    if (fastQ8 >= minimumLevelQ8 && rise >= threshold && trendQ8 >= minimumTrendQ8) {
        if (++risingMs >= qualificationMs) {
            pulseFrames = pulseSamples;
            holdoffMs = refractoryMs;
            risingMs = settledMs = 0;
            armed = false;
        }
    } else {
        risingMs = 0;
    }
}
} // namespace computer
