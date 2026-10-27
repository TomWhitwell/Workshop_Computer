// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "sine_table.h"
namespace spatial {
inline int32_t Clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline int32_t Sin(uint32_t phase) {
    uint32_t i = phase >> 24;
    int32_t f = (phase >> 16) & 255;
    return kSine[i] + ((kSine[(i+1)&255] - kSine[i]) * f >> 8);
}
// Exact power-of-two slew: preserve truncation toward zero and the final
// one-unit step. DSP differences are bounded well away from INT32_MIN.
template<int Shift> inline int32_t SlewPower(int32_t from,int32_t to){
    static_assert(Shift>0&&Shift<31,"supported power-of-two slew");
    int32_t d=to-from;
    if(d==0)return from;
    int32_t sign=d>>31;
    int32_t magnitude=(d^sign)-sign;
    int32_t step=(magnitude>>Shift)+1;
    return from+((step^sign)-sign);
}
inline int32_t Slew(int32_t from, int32_t to, int32_t divisor) {
    int32_t d = to - from;
    return from + (d == 0 ? 0 : d / divisor + (d > 0 ? 1 : -1));
}
}
