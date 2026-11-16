#pragma once
#include "../dsp/voice.hpp"
#include "controller.hpp"
namespace computer {
// Used by core1 firmware and desktop harness; all DSP/event ordering is shared.
class Processor {
public:
    mtm::Frame process(const Packet &packet);
    // Explicit state permits standalone numerical DC-rejection checks.
    static int32_t highpass(int32_t value, int32_t &state);

private:
    mtm::Voice voice;
    int32_t stringDC = 0, haloDC = 0;
};
} // namespace computer
