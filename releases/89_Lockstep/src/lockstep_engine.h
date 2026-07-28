#pragma once
#include <stdint.h>
#include "markov_scales.h"

// Pure, hardware-free movement engine so it can be unit-tested on the host
// (see ../sim/test_engine.cpp). A bounded, reflected random walk in semitones
// around a center note, quantized to a scale.

static constexpr int32_t LOCKSTEP_CENTER  = 72; // walk center (MIDI). High, so the CV stays positive.
static constexpr int32_t LOCKSTEP_HALFMAX = 12; // max half-range (Y fully CW) = +/- one octave
static constexpr int32_t LOCKSTEP_MINNOTE = 60; // floor: keep output >= ~0V for positive-only osc inputs

struct Walk {
    int32_t  pos = 0;            // semitones from center
    uint32_t rng = 0x1a2b3c4du;  // xorshift32 state

    uint32_t next() {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return rng;
    }

    // One step of a reflected random walk bounded to +/- range semitones.
    void step(int32_t range) {
        if (range <= 0) { pos = 0; return; }
        int32_t stepMax = 1 + range / 3;                          // gentle drift when range is small
        int32_t s = (int32_t)(next() % (uint32_t)(2 * stepMax + 1)) - stepMax;
        pos += s;
        if (pos >  range) pos =  2 * range - pos;                 // reflect at top
        if (pos < -range) pos = -2 * range - pos;                 // reflect at bottom
        if (pos >  range) pos =  range;                           // clamp (safety after reflect)
        if (pos < -range) pos = -range;
    }

    void reseed(uint32_t seed) { rng = seed ? seed : 1u; pos = 0; }
};

// Passing-tone offset for the doubling voice: base +/- 1..2 semitones, bounded to
// the walk range. `r` is any random 32-bit value. Result stays within [-range, range].
static inline int32_t LockstepOrnament(int32_t base, int32_t range, uint32_t r) {
    if (range <= 0) return 0;
    int32_t d = 1 + (int32_t)(r % 2u);          // 1 or 2 semitones
    if (r & 0x10000u) d = -d;                    // direction from a different bit
    int32_t p = base + d;
    if (p >  range) p =  range;
    if (p < -range) p = -range;
    return p;
}

// Quantize center+pos to a scale tone, octave-folded to stay at/above LOCKSTEP_MINNOTE
// so the CV never goes negative (some oscillator pitch inputs won't track sub-0V --
// this card exists partly because a SineSquare wouldn't).
static inline uint8_t LockstepNote(int32_t pos, uint8_t scaleIdx) {
    int32_t raw = LOCKSTEP_CENTER + pos;
    if (raw < 0)   raw = 0;
    if (raw > 127) raw = 127;
    uint8_t n = QuantizeToScale((uint8_t)raw, scaleIdx);
    while (n < LOCKSTEP_MINNOTE) n += 12;                         // fold up, same scale degree
    while (n > 120)              n -= 12;
    return n;
}
