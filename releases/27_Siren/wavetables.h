#ifndef WAVETABLES_H
#define WAVETABLES_H

#include <cstdint>

// All wavetables are 1024 entries, stored in flash (const)
// Values are Q15 signed: -32768 to 32767

static constexpr int TABLE_SIZE = 1024;
static constexpr int TABLE_MASK = TABLE_SIZE - 1;

// Phase accumulator uses 32-bit fixed point: upper 10 bits index the table,
// lower 22 bits are fractional for interpolation
static constexpr int PHASE_FRAC_BITS = 22;
static constexpr int32_t PHASE_FRAC_MASK = (1 << PHASE_FRAC_BITS) - 1;

// Generate sine table at compile time
// Using a simple approximation since constexpr trig isn't available everywhere
// We'll compute it with a runtime init instead
static int16_t sine_table[TABLE_SIZE];
static int16_t saw_table[TABLE_SIZE];
static int16_t tri_table[TABLE_SIZE];

// Tanh approximation table for waveshaping (input range -4.0 to +4.0 mapped to 0..1023)
static int16_t tanh_table[TABLE_SIZE];

// Wavefold table: maps -2.0..+2.0 to folded output
static int16_t fold_table[TABLE_SIZE];

// Equal-power crossfade curve: sin(x * pi/2) scaled to Q15
// Used for smooth bank transitions without volume dip
static constexpr int XFADE_TABLE_SIZE = 256;
static int16_t xfade_curve[XFADE_TABLE_SIZE];

// Initialize all wavetables (call once at startup)
inline void init_wavetables()
{
    // Sine: one full cycle
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        // Use double for init precision, store as Q15
        double phase = (double)i / TABLE_SIZE * 2.0 * 3.14159265358979;
        double s = 0.0;
        // Compute sine via Taylor series (enough terms for good accuracy)
        double x = phase;
        if (x > 3.14159265358979) x -= 2.0 * 3.14159265358979;
        double term = x;
        s = term;
        term *= -x * x / (2.0 * 3.0);
        s += term;
        term *= -x * x / (4.0 * 5.0);
        s += term;
        term *= -x * x / (6.0 * 7.0);
        s += term;
        term *= -x * x / (8.0 * 9.0);
        s += term;
        term *= -x * x / (10.0 * 11.0);
        s += term;
        term *= -x * x / (12.0 * 13.0);
        s += term;

        int32_t val = (int32_t)(s * 32767.0);
        if (val > 32767) val = 32767;
        if (val < -32768) val = -32768;
        sine_table[i] = (int16_t)val;
    }

    // Saw: ramp from -32767 to +32767
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        saw_table[i] = (int16_t)((int32_t)i * 65534 / TABLE_SIZE - 32767);
    }

    // Triangle
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        if (i < TABLE_SIZE / 2)
            tri_table[i] = (int16_t)((int32_t)i * 65534 / (TABLE_SIZE / 2) - 32767);
        else
            tri_table[i] = (int16_t)(32767 - (int32_t)(i - TABLE_SIZE / 2) * 65534 / (TABLE_SIZE / 2));
    }

    // Tanh table: input mapped from -4.0 to +4.0
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        double x = ((double)i / TABLE_SIZE * 8.0) - 4.0;
        // tanh approximation: x / (1 + |x| + x*x*0.28)
        double absx = x < 0 ? -x : x;
        double t = x / (1.0 + absx + x * x * 0.28);
        tanh_table[i] = (int16_t)(t * 32767.0);
    }

    // Wavefold table: input mapped from -2.0 to +2.0, folds at +-1.0
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        double x = ((double)i / TABLE_SIZE * 4.0) - 2.0;
        // Triangle-wave fold
        // Wrap x into -1..1 range by folding
        while (x > 1.0) x = 2.0 - x;
        while (x < -1.0) x = -2.0 - x;
        fold_table[i] = (int16_t)(x * 32767.0);
    }

    // Equal-power crossfade: sin(x * pi/2) for x in [0, 1]
    // fade_in uses xfade_curve[i], fade_out uses xfade_curve[255 - i]
    for (int i = 0; i < XFADE_TABLE_SIZE; i++)
    {
        double x = (double)i / (XFADE_TABLE_SIZE - 1) * 3.14159265358979 / 2.0;
        // Taylor series for sin(x), x in [0, pi/2] — converges quickly
        double term = x;
        double s = term;
        term *= -x * x / (2.0 * 3.0);
        s += term;
        term *= -x * x / (4.0 * 5.0);
        s += term;
        term *= -x * x / (6.0 * 7.0);
        s += term;
        term *= -x * x / (8.0 * 9.0);
        s += term;
        xfade_curve[i] = (int16_t)(s * 32767.0);
    }
}

// Look up value from table with linear interpolation
// phase is a 32-bit phase accumulator (upper 10 bits = index, lower 22 = frac)
inline int16_t table_lookup(const int16_t* table, uint32_t phase)
{
    uint32_t idx = (phase >> PHASE_FRAC_BITS) & TABLE_MASK;
    uint32_t frac = (phase >> (PHASE_FRAC_BITS - 15)) & 0x7FFF; // 15-bit fraction
    int32_t a = table[idx];
    int32_t b = table[(idx + 1) & TABLE_MASK];
    return (int16_t)(a + ((b - a) * (int32_t)frac >> 15));
}

// Convert frequency (Hz) to phase increment for 48kHz sample rate
// freq is in Q16.16 fixed point (Hz * 65536)
// Returns phase increment for 32-bit accumulator
inline uint32_t freq_to_phase_inc(int32_t freq_q16)
{
    // phase_inc = freq * TABLE_SIZE / SAMPLE_RATE * 2^PHASE_FRAC_BITS
    // = freq * 1024 / 48000 * 2^22
    // = freq * 1024 * 4194304 / 48000
    // = freq * 89478.485...
    // With freq in Q16.16: freq_q16 * 89478 >> 16
    // Simplify: freq_q16 * 89478 >> 16
    // But 89478 is close to 89479 = ~(1024 * 2^22 / 48000)
    return (uint32_t)((int64_t)freq_q16 * 89478LL >> 16);
}

// Convenience: frequency in integer Hz to phase increment
inline uint32_t freq_hz_to_phase_inc(int32_t freq_hz)
{
    return freq_to_phase_inc(freq_hz << 16);
}

#endif // WAVETABLES_H
