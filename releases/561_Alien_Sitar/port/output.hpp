#pragma once
#include "../dsp/fixed.hpp"
namespace computer {
struct Output {
    int32_t mix = 0, halo = 0;
    static int32_t advance(int32_t previous, int32_t filtered);
    void next(int32_t a, int32_t b);
};
} // namespace computer
