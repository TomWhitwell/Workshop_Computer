#ifndef DSP_H
#define DSP_H

#include <cstdint>

// Fixed-point arithmetic utilities for audio DSP on RP2040
// Q15: signed 16-bit with 15 fractional bits (-1.0 to ~+1.0)
// Q16.16: signed 32-bit with 16 fractional bits

// Multiply two Q15 values, return Q15
inline int16_t q15_mul(int16_t a, int16_t b)
{
    return (int16_t)((int32_t)a * b >> 15);
}

// Multiply Q15 by a 0-4095 knob value (treat as 0.0-1.0 unsigned fraction)
// Result is Q15
inline int16_t q15_scale(int16_t sample, int32_t knob_val)
{
    return (int16_t)((int32_t)sample * knob_val >> 12);
}

// Clip a value to Q15 range
inline int16_t q15_clip(int32_t val)
{
    if (val > 32767) return 32767;
    if (val < -32768) return -32768;
    return (int16_t)val;
}

// Clip to 12-bit audio output range (-2048 to 2047)
inline int16_t clip12(int32_t val)
{
    if (val > 2047) return 2047;
    if (val < -2048) return -2048;
    return (int16_t)val;
}

// Simple one-pole lowpass filter for parameter smoothing
// state is Q15, target is Q15, coeff is smoothing (0 = instant, 32767 = very slow)
struct OnePoleLPF
{
    int32_t state = 0; // Q15 stored as int32 for precision

    int16_t process(int16_t target, int16_t coeff)
    {
        // state = state + (target - state) * (1 - coeff)
        // Using: state += (target - state) >> shift  for simple version
        int32_t diff = (int32_t)target - state;
        state += (diff * (32767 - coeff)) >> 15;
        return (int16_t)state;
    }

    // Fast version with fixed shift (e.g., shift=8 gives ~0.004 per sample = smooth)
    int16_t process_shift(int16_t target, int shift)
    {
        int32_t diff = (int32_t)target - state;
        state += diff >> shift;
        return (int16_t)state;
    }
};

// Parameter smoother specifically for knob values (0-4095)
struct KnobSmoother
{
    int32_t state = 0; // Stored as value << 8 for precision

    int32_t process(int32_t raw)
    {
        state += (((int32_t)raw << 8) - state) >> 6;
        return state >> 8;
    }
};

// Linear interpolation between two Q15 values
// mix is 0-4095 (0 = all a, 4095 = all b)
inline int16_t q15_lerp(int16_t a, int16_t b, int32_t mix)
{
    return (int16_t)((int32_t)a + (((int32_t)b - a) * mix >> 12));
}

// Scale Q15 audio to 12-bit output range
inline int16_t q15_to_12bit(int16_t val)
{
    return clip12((int32_t)val >> 3); // Q15 (±32767) to ±2047 with headroom
}

// Scale 12-bit input to Q15
inline int16_t bit12_to_q15(int16_t val)
{
    return q15_clip((int32_t)val << 3);
}

#endif // DSP_H
