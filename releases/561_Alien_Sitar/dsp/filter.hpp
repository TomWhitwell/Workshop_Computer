/// @file filter.hpp
/// @brief 17-tap halfband FIR converting between the 24 kHz and 12 kHz rates.
///
/// A Voice runs two Fir instances around the 12 kHz sympathetic bank: one
/// low-passes bridge drive before the bank consumes every other frame, and
/// one upsamples the bank's sparse 12 kHz output back to 24 kHz. Response,
/// rounding and latency match the original scalar FIR exactly. Fixed-size
/// state, integer only, allocation-free; owned by the Core1 worker.
#pragma once
#include "fixed.hpp"
namespace mtm {
/// Halfband FIR used in pairs for the voice/bank rate conversion.
// Same 17-tap response, rounding and latency as the original scalar FIR.
class Fir {
public:
    /// Process one Q15 sample through the sparse symmetric FIR.
    ///
    /// @param x New input sample; clipped internally to +/-32767.
    /// @return Filtered Q15 sample with the original FIR's rounding and latency.
    int32_t next(int32_t x);

private:
    // Mirrored history provides 17 consecutive samples without per-tap wrapping.
    // Two stores per input cost less than repeatedly wrapping nine tap addresses.
    int32_t history[34]{};
    unsigned head = 0;
};
} // namespace mtm
