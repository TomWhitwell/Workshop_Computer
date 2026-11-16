/**
 * @file output.hpp
 * @brief Core0 output conditioning: makeup gain, level cap and slew limit.
 *
 * Applies identical shaping to the two reconstructed 48 kHz channels (the
 * complete mono instrument and the halo alone) after the zero-stuffed
 * 24-to-48 kHz FIR reconstruction. The resulting Q15 values are what the
 * firmware callback divides by timing::q15ToDac for 12-bit DAC codes.
 * Incremental state only; no allocation, hardware access or blocking.
 *
 *
 * @date 2026-11-16
 */
#pragma once
#include "../dsp/fixed.hpp"
namespace computer {
/// @brief Pair of slew-limited conditioner channels owned by core0 (or Offline).
struct Output {
    /// Conditioned Q15 channel values: mix is the main instrument output,
    /// halo the sympathetic band alone; consumers read them per 48 kHz sample.
    int32_t mix = 0, halo = 0;
    /// @brief Shape one channel: 4x makeup gain, +/-5 V cap, slew limit.
    /// @param previous Immediately preceding conditioned sample (Q15).
    /// @param filtered Fresh reconstruction-doubled input for this sample (Q15).
    /// @return New conditioned sample (Q15), bounded by cap and one-step slew.
    static int32_t advance(int32_t previous, int32_t filtered);
    /// @brief Advance both channels one 48 kHz sample in place.
    /// @param a New reconstruction-doubled sample for the main mix channel (Q15).
    /// @param b New reconstruction-doubled sample for the halo channel (Q15).
    void next(int32_t a, int32_t b);
};
} // namespace computer
