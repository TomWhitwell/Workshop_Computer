#pragma once
#include "fixed.hpp"
namespace mtm {
// Same 17-tap response, rounding and latency as the original scalar FIR.
class Fir {
public:
    int32_t next(int32_t x);

private:
    // Mirrored history provides 17 consecutive samples without per-tap wrapping.
    // Two stores per input cost less than repeatedly wrapping nine tap addresses.
    int32_t history[34]{};
    unsigned head = 0;
};
} // namespace mtm
