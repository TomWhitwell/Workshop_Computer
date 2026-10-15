#pragma once

// Minimal host definitions required by the original Bib DSP sources.
// The original common.h supplies the same fixed-point helpers.

#include <cstdint>
#include <cstdlib>

#define BUILD_RELEASE
#define BLOCK_SIZE_SH 6
#define BLOCK_SIZE (1 << BLOCK_SIZE_SH)
#define SAMPLE_FREQ 48000
#define debug_log(...) ((void)0)

static inline int clampi(int value, int minimum, int maximum)
{
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

static inline int maxi(int left, int right)
{
    return left > right ? left : right;
}

static inline int mini(int left, int right)
{
    return left < right ? left : right;
}

static inline int make_lpf_delta(int target, int current, int speedShift)
{
    const int delta = target - current;
    return delta > 0 ? -(-delta >> speedShift) : (delta >> speedShift);
}
