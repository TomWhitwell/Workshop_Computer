#include "output.hpp"
namespace computer {
namespace {
constexpr int32_t makeupGain = 4;
constexpr int32_t fiveVoltCapQ15 = 27306; // Nominal full DAC range is approximately +/-6 V.
constexpr int32_t maximumStepQ15 = 8192;  // Limit abrupt transitions at the 48 kHz interface.
} // namespace

int32_t Output::advance(int32_t previous, int32_t filtered) {
    // Fixed gain uses more of the 12-bit DAC range; cap and slew preserve headroom.
    int32_t target = mtm::clip(filtered * makeupGain, -fiveVoltCapQ15, fiveVoltCapQ15);
    return previous + mtm::clip(target - previous, -maximumStepQ15, maximumStepQ15);
}

void Output::next(int32_t a, int32_t b) {
    mix = advance(mix, a);
    halo = advance(halo, b);
}
} // namespace computer
