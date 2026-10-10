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
inline int32_t Slew(int32_t from, int32_t to, int32_t divisor) {
    int32_t d = to - from;
    return from + (d == 0 ? 0 : d / divisor + (d > 0 ? 1 : -1));
}
}
