/**
 * @file output.cpp
 * @brief Makeup-gain, cap and slew maths for the core0 48 kHz output stage.
 *
 * Both channels share one clip-and-slew advance; the cap keeps the nominal
 * +/-5 V output span inside headroom and the slew limit suppresses abrupt
 * transitions (underrun fades, faults) at the 48 kHz interface.
 *
 *
 * @date 2026-11-16
 */
#include "output.hpp"
namespace computer {
namespace {
/// @brief Fixed post-reconstruction gain that uses more of the DAC range.
constexpr int32_t makeupGain = 4;
/// @brief Output level cap in Q15 (nominal +/-5 V of the ~ +/-6 V DAC span).
constexpr int32_t fiveVoltCapQ15 = 27306; // Nominal full DAC range is approximately +/-6 V.
/// @brief Maximum change per 48 kHz sample: Q15/4, so full-scale moves in 4 samples.
constexpr int32_t maximumStepQ15 = 8192; // Limit abrupt transitions at the 48 kHz interface.
} // namespace

int32_t Output::advance(int32_t previous, int32_t filtered) {
    // Fixed gain uses more of the 12-bit DAC range; cap and slew preserve headroom.
    // The unclipped 4x product can exceed Q15; clip() bounds each stage below.
    int32_t target = mtm::clip(filtered * makeupGain, -fiveVoltCapQ15, fiveVoltCapQ15);
    return previous + mtm::clip(target - previous, -maximumStepQ15, maximumStepQ15);
}

void Output::next(int32_t a, int32_t b) {
    mix = advance(mix, a);
    halo = advance(halo, b);
}
} // namespace computer
