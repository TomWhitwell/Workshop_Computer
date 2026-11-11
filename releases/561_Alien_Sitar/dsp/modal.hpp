#pragma once
#include "fixed.hpp"
namespace mtm {
// 12 kHz, sixty partials. Each has two damped resonators at the same frequency.
// Subtracting their fast/slow responses makes a causal metallic bloom. Wider
// state coordinates keep weak tails alive without four fraction-carry loops.
struct ModalBank {
    static constexpr int rate = 12000, count = 60;
    static constexpr int32_t stateLimit = (1 << 24) - 1;
    struct Mode {
        int32_t aSlow, lossSlow, aFast, lossFast, gain;
        int32_t s1 = 0, s2 = 0, f1 = 0, f2 = 0;
    };
    uint32_t clippedStates() const { return clampCount; }
    using Modes = Mode[count];
    const Modes &modesView() const { return modes; }

private:
    Modes modes;
    uint32_t clampCount = 0;

public:
    ModalBank();
    // Split wide multiplies to avoid software 64-bit products in the hot path:
    // |x| < 2^24, |a| <= 32768, and loss <= 65535. No 64-bit hot-path helpers.
    static int32_t feedback(int32_t x, int32_t a);
    static int32_t loss(int32_t x, int32_t l);
    int32_t advance(int32_t &y1, int32_t &y2, int32_t a, int32_t l, int32_t injection);
    int32_t next(int32_t bridge, int32_t dampQ15);
};
} // namespace mtm
