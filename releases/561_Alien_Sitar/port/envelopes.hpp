/**
 * @file envelopes.hpp
 * @brief Core0 asymmetric amplitude followers for melody/halo telemetry.
 *
 * Holds the smoothed Q15 level state consumed by the level LEDs, the CV2 halo
 * follower and the bloom detector. One call per returned 24 kHz frame moves
 * each channel toward the current signal magnitude with asymmetric rates:
 * a fast attack and a slow release. Owned by core0 (or the offline adapter);
 * purely incremental state with no allocation or hardware access.
 *
 *
 * @date 2026-11-16
 */
#pragma once
#include "../dsp/fixed.hpp"
namespace computer {
/// @brief Two amplitude followers fed with the melody (dry) and halo (wet) channels.
struct Envelopes {
    /// Persistent Q15 follower levels: dry tracks the melody output magnitude,
    /// halo the sympathetic output magnitude. Nominal full scale is 32768.
    int32_t dry = 0, halo = 0;
    /// @brief One asymmetric approach step toward a magnitude target.
    /// @param x Current smoothed level (Q15, non-negative in practice).
    /// @param goal New target magnitude (Q15, typically an abs32 result).
    /// @return Level moved toward goal: 2^-7 of the remaining distance rising,
    ///         2^-12 falling, with a minimum one-unit step either way.
    static int32_t follow(int32_t x, int32_t goal);
    /// @brief Update both followers from fresh frame channels.
    /// @param direct Melody-channel sample (signed Q15); magnitude is taken.
    /// @param wet Halo-channel sample (signed Q15); magnitude is taken.
    /// @note Called once per returned 24 kHz frame on core0 or in Offline.
    void next(int32_t direct, int32_t wet);
    /// @brief Halo level mapped to nominal CV2 millivolts.
    /// @return Level scaled by 5000/600 (600 Q15 maps to 5000 mV), clipped
    ///         to 0..5000; a nominal follower, not a calibrated level.
    int32_t haloMv() const;
};
} // namespace computer
