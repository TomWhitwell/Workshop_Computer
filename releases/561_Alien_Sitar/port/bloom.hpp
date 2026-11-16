/**
 * @file bloom.hpp
 * @brief Core0 1 kHz halo-rise detector producing the 5 ms Pulse2 bloom pulse.
 *
 * Watches the audible sympathetic halo for a sustained relative rise and
 * emits a short pulse, with qualification, hysteresis, refractory holdoff and
 * damp/fault inhibition so a swell or a DAMP release cannot chatter the
 * output. All state is core0-only; the 24 kHz voice never touches it.
 *
 * Formats: halo samples are Q15; internal envelope/trend states carry 8
 * extra fractional bits below a Q15 LSB (values scaled by 256); all timers
 * count 1 kHz analysis milliseconds.
 *
 *
 * @date 2026-11-16
 */
#pragma once
#include <cstdint>

namespace computer {
/// Detect a sustained relative rise in audible halo energy, not a delayed copy of
/// the pluck trigger. Owned by core0; advance once per 24 kHz instrument frame.
class BloomDetector {
public:
    /// @brief Feed one halo sample into the averaging buckets and analyse at 1 kHz.
    /// @param haloQ15 Signed halo-channel sample for this 24 kHz frame (Q15).
    /// @param validFrame False for underrun/fallback frames; the detector
    ///        then suppresses and inhibits as if damped.
    void advance(int32_t haloQ15, bool validFrame = true);
    /// @brief Suppress and disarm: clears any pulse and starts damp-style inhibition.
    /// @note Called for DAMP presses and invalid (underrun) frames; a rising
    ///       bloom must not be reported across a damped or synthetic recovery.
    void suppress();
    /// @brief Current Pulse2 pulse state, sampled each callback.
    /// @return True while the 5 ms bloom pulse remains high (240 voice frames).
    bool pulseHigh() const { return pulseFrames > 0; }

private:
    /// @brief Run one 1 kHz analysis tick on the mean magnitude of the bucket.
    /// @param meanMagnitude Average |halo| over the last 24-sample bucket,
    ///        full-wave Q15 units.
    void analyse(int32_t meanMagnitude);
    /// Q15 magnitudes accumulated in the current 24-sample (1 ms) bucket.
    int32_t magnitudeSum = 0;
    /// Samples collected in the bucket so far; analyse() runs at 24.
    unsigned samples = 0;
    /// x256-scaled envelopes (8 bits below a Q15 LSB): fast follower, slow
    /// baseline, smoothed rise trend.
    int32_t fastQ8 = 0, slowQ8 = 0, trendQ8 = 0;
    /// Millisecond timers: consecutive rising qualification, settled rearm.
    unsigned risingMs = 0, settledMs = 0;
    /// Millisecond timers: post-pulse refractory, damp/fault inhibition.
    unsigned holdoffMs = 0, inhibitMs = 0;
    /// Remaining 24 kHz frames in the active 5 ms output pulse.
    unsigned pulseFrames = 0;
    /// False after a pulse or suppression until the rise has settled again.
    bool armed = true;
};
} // namespace computer
