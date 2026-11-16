#pragma once
#include <cstdint>

namespace computer {
// Detect a sustained relative rise in audible halo energy, not a delayed copy of
// the pluck trigger. Owned by core0; advance once per 24 kHz instrument frame.
class BloomDetector {
public:
    void advance(int32_t haloQ15, bool validFrame = true);
    void suppress();
    bool pulseHigh() const { return pulseFrames > 0; }

private:
    void analyse(int32_t meanMagnitude);
    int32_t magnitudeSum = 0;
    unsigned samples = 0;
    int32_t fastQ8 = 0, slowQ8 = 0, trendQ8 = 0;
    unsigned risingMs = 0, settledMs = 0;
    unsigned holdoffMs = 0, inhibitMs = 0;
    unsigned pulseFrames = 0;
    bool armed = true;
};
} // namespace computer
