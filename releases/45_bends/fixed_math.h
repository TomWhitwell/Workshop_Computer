#ifndef FIXED_MATH_H
#define FIXED_MATH_H

#include <stdint.h>
#include <math.h>

// ============================================================================
// Q15 / Q31 Arithmetic
// All audio samples are Q15: range [-32768, 32767], representing [-1.0, 1.0).
// ============================================================================

// Q15 multiply: returns Q15 result (1-bit headroom lost in product)
inline int16_t mul_q15(int16_t a, int16_t b) {
    return (int16_t)(((int32_t)a * (int32_t)b) >> 15);
}

// Q31 multiply using 64-bit product (for high-precision coefficient work)
inline int32_t mul_q31(int32_t a, int32_t b) {
    return (int32_t)(((int64_t)a * (int64_t)b) >> 31);
}

// Legacy names kept for compatibility
inline int16_t multiply_q15(int16_t a, int16_t b) { return mul_q15(a, b); }
inline int32_t multiply_q31(int32_t a, int32_t b) { return mul_q31(a, b); }

// ============================================================================
// Clamping & Saturation
// ============================================================================

// Hard saturation to Q15 range via hardware INTERP1 clamp (branch-free, 2 cycles).
// Requires init_hardware_interp() to have been called once on Core 1.
// Bounds BASE0=-32768 / BASE1=32767 are loaded at init and never change.
#include "hardware/interp.h"
inline int16_t saturate_q15(int32_t x) {
    interp1->accum[0] = (uint32_t)x;
    return (int16_t)(int32_t)interp1->peek[0];
}

// Clamp int32_t to arbitrary range
inline int32_t clamp_i32(int32_t x, int32_t lo, int32_t hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

// Soft saturation: musical "tape-style" knee starting at ~75% FS.
// Compresses gently above threshold, hard-clamps at ±32767.
// Sounds considerably less digital than hard clipping on transients.
inline int16_t soft_limit_q15(int32_t x) {
    // Knee at ±24576 (~75% full-scale)
    // Above knee: gain = 1/8 (8:1 compression)
    const int32_t knee = 24576;
    if (x > knee) {
        x = knee + ((x - knee) >> 3);
    } else if (x < -knee) {
        x = -knee + ((x + knee) >> 3);
    }
    return saturate_q15(x);
}

// ============================================================================
// Linear Interpolation
// ============================================================================

// lerp_q15: mix = 0 -> returns a, mix = 32767 -> returns b
// t is Q15 unsigned fraction [0, 32767]
inline int16_t lerp_q15(int16_t a, int16_t b, int16_t t) {
    // Uses int32 intermediate to avoid overflow in (b-a)*t
    return (int16_t)(a + (((int32_t)(b - a) * (int32_t)(uint16_t)t) >> 15));
}

inline int32_t lerp_q31(int32_t a, int32_t b, int32_t t) {
    return a + (int32_t)(((int64_t)(b - a) * (int64_t)t) >> 31);
}

// lerp_delay_q15: fast software linear interpolation.
// Highly optimized for ARM Cortex-M0+ single-cycle multiplier: 5 cycles, no bus latency,
// and provides 15-bit precision (32768 steps) instead of the hardware blend's 8-bit precision (256 steps).
inline int16_t lerp_delay_q15(int16_t y0, int16_t y1, uint16_t frac16) {
    int32_t diff = y1 - y0;
    return (int16_t)(y0 + ((diff * (int32_t)(frac16 >> 1)) >> 15));
}

// ============================================================================
// One-Pole IIR Smoother (coefficient-free, shift-based)
// Equivalent to: state += (target - state) * (1/2^shift)
// shift=4 -> tau ~16 samples (fast), shift=7 -> tau ~128 samples (slow).
// ============================================================================
#define IIR_SMOOTH(state, target, shift) \
    ((state) += ((target) - (state)) >> (shift))

// ============================================================================
// Fast PRNG — xorshift32 (superior avalanche vs LCG, same cost)
// ============================================================================
inline uint32_t fast_rand(uint32_t &seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

// Returns a signed symmetric random Q15 value in [-32768, 32767]
inline int16_t fast_rand_q15_sym(uint32_t &seed) {
    return (int16_t)(fast_rand(seed) >> 16);
}

// ============================================================================
// Sine Table & Interpolated Lookup
// 1024-entry table + 1 guard sample for wrap-safe interpolation.
// Phase is uint16_t representing full 0..2π in [0, 65535].
// ============================================================================
#define SINE_TABLE_SIZE 1024
extern int16_t sine_table[SINE_TABLE_SIZE + 1];

inline void init_sine_table() {
    const double TWO_PI = 6.28318530717958647692;
    for (int i = 0; i <= SINE_TABLE_SIZE; i++) {
        double angle = (double)i * TWO_PI / (double)SINE_TABLE_SIZE;
        sine_table[i] = (int16_t)(sin(angle) * 32767.0);
    }
}

// Linearly-interpolated sine lookup. No branching, wraps correctly.
inline int16_t lookup_sine(uint16_t phase) {
    // Map 16-bit phase to [0, 1023] index + 16-bit fractional part
    uint32_t scaled = (uint32_t)phase * SINE_TABLE_SIZE; // range [0, 1023 * 65536 + ...]
    uint32_t idx    = scaled >> 16;                       // [0, 1023]
    uint32_t frac   = scaled & 0xFFFF;                   // [0, 65535]
    int16_t  y0     = sine_table[idx];
    int16_t  y1     = sine_table[idx + 1]; // guard sample prevents out-of-bounds
    return (int16_t)(y0 + (int16_t)(((int32_t)(y1 - y0) * (int32_t)frac) >> 16));
}

// Fast direct-table sine lookup for slow-moving LFOs (zero division, zero multiply-interpolate).
inline int16_t lookup_sine_fast(uint16_t phase) {
    return sine_table[phase >> 6];
}

// ============================================================================
// Knob Lock
// Prevents parameter jumps when switching pages. The knob is "locked" to its
// last value on a page change. It unlocks once the hardware position moves
// past a threshold (~5% of the Q15 range = 1638 units).
// Mirrors the behaviour used in the Modes and Grains releases.
// ============================================================================
struct KnobLock {
    bool    locked = true;
    int32_t ref    = 0;
    int32_t val    = 0;
    bool    catchup_mode = false;
    bool    catchup_dir = false; // true if ref > val (need v <= val), false if ref < val (need v >= val)

    // Engage lock: call on every page change.
    // saved_value is the virtual parameter value for this page.
    void engage(int32_t current_hw_value, int32_t saved_value = 0, bool catchup = false) {
        locked = true;
        ref    = current_hw_value;
        val    = saved_value;
        catchup_mode = catchup;
        if (catchup) {
            catchup_dir = (ref > val);
        }
    }

    // Call every UI tick with the smoothed hardware knob value.
    // Returns the new virtual value.
    int32_t update(int32_t v) {
        if (locked) {
            if (catchup_mode) {
                if (catchup_dir) {
                    if (v <= val) locked = false;
                } else {
                    if (v >= val) locked = false;
                }
            } else {
                int32_t d = v - ref;
                if (d < 0) d = -d;
                if (d > 1638) {
                    locked = false; // Unlocked!
                }
            }
        }
        
        if (!locked) {
            if (catchup_mode) {
                val = v; // instant tracking once caught up
            } else {
                int32_t dist = v - val;
                if (dist < 0) dist = -dist;
                
                // Dynamic slew speed: if physical knob is far, slide slowly. If close, track fast.
                int32_t shift = 4; // fast tracking for small jumps (~16ms)
                if (dist > 20000)      shift = 7; // very slow slide for full-scale jumps (~128ms)
                else if (dist > 10000) shift = 6; // medium-slow for half-scale (~64ms)
                else if (dist > 3000)  shift = 5; // standard slew (~32ms)
                
                val += (v - val) >> shift;
                
                // Snap to physical value when extremely close
                int32_t diff = v - val;
                if (diff < 0) diff = -diff;
                if (diff < 32) {
                    val = v;
                }
            }
        }
        return val;
    }

    // Force re-lock at a new reference (e.g. if param was externally changed)
    void relock(int32_t v, int32_t saved_value = 0) {
        locked = true;
        ref    = v;
        val    = saved_value;
        catchup_mode = false;
    }
};

// ============================================================================
// 1V/Octave Exponential Pitch/Speed Converter
// Maps a signed 12-bit ADC value (1024 units per octave) to a Q15 speed multiplier.
// 0 -> 32768 (1.0x speed)
// 1024 -> 65536 (2.0x speed)
// -1024 -> 16384 (0.5x speed)
// ============================================================================
inline int32_t pow2_q15(int16_t raw_val) {
    int32_t oct = (int32_t)raw_val >> 10;
    int32_t frac = (int32_t)raw_val & 1023;
    // frac is [0..1023]. Scale to Q15 range [0..32736]
    int32_t f = frac << 5;
    int32_t term1 = (f * 21524) >> 15;              // 0.656852 * 32768
    int32_t term2 = (((f * f) >> 15) * 11244) >> 15; // 0.343148 * 32768
    int32_t frac_mult = 32768 + term1 + term2;
    
    if (oct >= 0) {
        if (oct > 10) oct = 10; // clamp to prevent shift overflow
        return frac_mult << oct;
    } else {
        int32_t shift = -oct;
        if (shift > 15) return 0;
        return frac_mult >> shift;
    }
}

// ============================================================================
// Split-Mix (Constant-Volume / Eurorack Dry-Wet Curve)
// Keeps dry signal at 100% in the first half of the knob (as wet rises to 100%).
// Keeps wet signal at 100% in the second half of the knob (as dry decays).
// Completely eliminates the -3dB volume drop in the middle of the knob.
// ============================================================================
inline int16_t split_mix_q15(int16_t dry, int16_t wet, int16_t mix) {
    if (mix < 16384) {
        // First half: dry is 100%, wet rises from 0% to 100%
        int32_t wet_gain = mix << 1;
        return soft_limit_q15(dry + (((int32_t)wet * wet_gain) >> 15));
    } else {
        // Second half: wet is 100%, dry decays from 100% to 0%
        int32_t dry_gain = (32767 - mix) << 1;
        return soft_limit_q15(wet + (((int32_t)dry * dry_gain) >> 15));
    }
}

#endif // FIXED_MATH_H

