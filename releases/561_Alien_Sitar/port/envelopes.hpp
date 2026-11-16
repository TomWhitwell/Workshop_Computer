#pragma once
#include "../dsp/fixed.hpp"
namespace computer {
struct Envelopes {
    int32_t dry = 0, halo = 0;
    static int32_t follow(int32_t x, int32_t goal);
    void next(int32_t direct, int32_t wet);
    int32_t haloMv() const;
};
} // namespace computer
