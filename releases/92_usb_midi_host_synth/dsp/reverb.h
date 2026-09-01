// Compact Freeverb-style mono reverb (adapted from 61_ZX_Spectrum).
#pragma once
#include <cstdint>
#include "pico.h"

class CardReverb
{
public:
    void Reset();
    int16_t __not_in_flash_func(Process)(int16_t in);

private:
    static constexpr int kNumCombs = 4;
    static constexpr int kNumAllpass = 2;
    static constexpr int kCombLen[kNumCombs] = {1116, 1188, 1277, 1356};
    static constexpr int kAllpassLen[kNumAllpass] = {556, 441};

    int16_t comb_[kNumCombs][1356];
    int32_t combStore_[kNumCombs];
    int combIdx_[kNumCombs];
    int16_t allpass_[kNumAllpass][556];
    int apIdx_[kNumAllpass];
};
