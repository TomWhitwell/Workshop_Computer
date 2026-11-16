/**
 * @file auxiliary.hpp
 * @brief Core0 auxiliary outputs: pitch CV, halo follower and bloom pulse.
 *
 * Turns each returned 24 kHz voice frame into the non-audio outputs: the CV1
 * pitch millivolts (1 V/oct, 0 V=C4, from the smoothed Q16 model period,
 * decimated to 750 Hz), the CV2 halo follow level, LED level telemetry and
 * the Pulse2 5 ms bloom pulse. Shared between the device and desktop paths.
 *
 * Formats/units: periods in Q16 samples at 24 kHz, audio levels in Q15,
 * millivolt outputs nominal (calibration is applied by the caller).
 *
 *
 * @date 2026-11-16
 */
#pragma once
#include "../dsp/voice.hpp"
#include "bloom.hpp"
#include "envelopes.hpp"

namespace computer {
/// @brief Convert a smoothed Q16 string period to nominal 1 V/oct millivolts.
///
/// Convert the model's smoothed Q16 string period to nominal 1 V/oct, 0 V=C4.
/// This is a model-state conversion, not a measured audio-frequency estimator.
/// @param periodQ16 Period in Q16 24 kHz-domain samples; non-positive gives 0.
/// @return Millivolts relative to C4 (0 mV), 1000 mV per octave, over the
///         model's 40-2000 Hz period bounds.
int32_t pitchMillivoltsFromPeriod(int32_t periodQ16);

/// @brief Follower, pulse and pitch-output state advanced on core0 (or Offline).
///
/// Shared device/desktop auxiliary outputs. Core0 alone owns this state.
class AuxiliaryOutputs {
public:
    /// @brief Update levels, bloom detection and decimated pitch inversion.
    /// @param frame One 24 kHz voice frame (mix/dry/wet plus smoothed period);
    ///        callers may pass a decayed/fallback frame when invalid.
    /// @param validFrame False for underrun/fallback frames: the bloom
    ///        detector suppresses and the pitch output holds its last value.
    /// @note Called once per returned voice frame inside the core0 callback.
    void advance(const mtm::Frame &frame, bool validFrame = true);
    /// @brief Report a received DAMP so the bloom pulse cannot straddle it.
    void notifyDamp();
    /// @brief Pitch-output millivolts; nominal 1 V/oct, 0 mV at C4.
    /// @return Last converted pitch, held while frames are invalid.
    int32_t pitchMillivolts() const { return pitchMv; }
    /// @brief Halo follower as nominal CV2 millivolts (0..5000).
    /// @return Current converted halo follower level.
    int32_t haloMillivolts() const;
    /// @brief Smoothed melody-channel level, Q15, for LED telemetry.
    /// @return Current dry follower level (0..32768 nominal).
    int32_t dryLevelQ15() const { return levels.dry; }
    /// @brief Smoothed halo-channel level, Q15, for LED telemetry.
    /// @return Current halo follower level (0..32768 nominal).
    int32_t haloLevelQ15() const { return levels.halo; }
    /// @brief Live Pulse2 bloom-pulse state, sampled each callback.
    /// @return True while the 5 ms bloom pulse is high.
    bool bloomPulseHigh() const { return bloom.pulseHigh(); }

private:
    /// Melody (dry) and halo (wet) amplitude followers; see Envelopes.
    Envelopes levels;
    /// Halo-rise 5 ms pulse detector; see BloomDetector.
    BloomDetector bloom;
    /// Last converted pitch output in millivolts (0 until the first valid
    /// voice frame with a positive period).
    int32_t pitchMv = 0;
    /// Voice frames seen; low 5 bits pace the 750 Hz pitch refresh.
    uint32_t frames = 0;
};
} // namespace computer
