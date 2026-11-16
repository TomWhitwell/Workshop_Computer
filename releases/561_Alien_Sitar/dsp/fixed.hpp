/// @file fixed.hpp
/// @brief Shared fixed-point arithmetic and control-value primitives.
///
/// Numeric toolkit for the sitar DSP: bounded Q15 sample helpers (clip(),
/// mul(), abs32(), limit()) plus control-rate smoothing and sub-LSB decay
/// accumulators. The 24 kHz Core1 voice worker and its 12 kHz modal bank call
/// these in their hot loops, so every helper is inline, bounded 32-bit integer
/// arithmetic: no allocation, blocking, floating point or 64-bit multiplies.
#pragma once
#include <algorithm>
#include <cstdint>
namespace mtm {
/// Voice sample rate in Hz, and the Q15 scale factor representing 1.0.
constexpr int RATE = 24000, Q = 32768;
/// Saturating clamp.
///
/// @param x Value to bound.
/// @param lo Lower limit (default -32767, the smallest Q15 sample).
/// @param hi Upper limit (default 32767, the largest Q15 sample).
/// @return x clamped into [lo, hi]; branch-only, audio-callback safe.
inline int32_t clip(int32_t x, int32_t lo = -32767, int32_t hi = 32767) {
    return x < lo ? lo : (x > hi ? hi : x);
}
/// Q15 multiply truncating toward zero via a plain 32-bit product and shift.
///
/// @param x Q15-scaled value.
/// @param c Q15 coefficient.
/// @return (x * c) >> 15, sign-corrected so truncation is toward zero.
// Bounded Q15 multiply, rounded toward zero. Callers must keep the signed
// product above INT32_MIN and below INT32_MAX. No 64-bit hot-path multiply.
inline int32_t mul(int32_t x, int32_t c) {
    int32_t p = x * c;
    return p < 0 ? -((-p) >> 15) : (p >> 15);
}
/// Absolute value that cannot overflow.
///
/// @param x Any int32_t value.
/// @return |x|, except INT32_MIN which maps to INT32_MAX instead of itself.
inline int32_t abs32(int32_t x) {
    return x == INT32_MIN ? INT32_MAX : (x < 0 ? -x : x);
}
/// Move x toward target by a proportional step of at least one LSB.
///
/// @param x Current value.
/// @param target Value to approach.
/// @param shift Step divisor as a power of two: the step is d >> shift.
/// @return Updated x one step closer to target; the step never becomes so
///         small that convergence stalls before x reaches target.
inline int32_t approach(int32_t x, int32_t target, unsigned shift) {
    int32_t d = target - x;
    if (!d) {
        return x;
    }
    int32_t step = d >> shift;
    if (step == 0) {
        step = 1;
    }
    return x + step;
}
/// Advance an xorshift32 state in place and return it.
///
/// @param r Generator state in/out; must be nonzero or it stays stuck at 0.
/// @return The updated state value (used as the pseudo-random output).
/// @note Deterministic, allocation-free; cheap enough to run every sample.
inline uint32_t random(uint32_t &r) {
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    return r;
}
/// Interpolate a 257-entry Q15 curve table at a Q15 position.
///
/// @param t Curve table of 257 Q15 entries; indexed at x/128 with a 7-bit
///        linear interpolation between neighbours.
/// @param x Q15 position, clamped to 0..32767 before the lookup.
/// @return Interpolated curve value in Q15.
/// @note Lookup-table only; runs inside the per-sample hot path.
int32_t ratio(const uint16_t *t, int32_t x);
/// Soft-limit |x| through the generated limiter curve table.
///
/// @param x Amplitude in Q15 sample units; |x| may travel up to 4 * Q before
///        saturating at the curve's top entry.
/// @return Limited amplitude in Q15 sample units, keeping the sign of x.
int32_t limit(int32_t x);
/// Take one whole step of a fractional accumulator.
///
/// @param p Per-frame increment; its low bits may be a partial unit.
/// @param r Remainder accumulator in/out, scaled by 2^bits.
/// @param bits Fraction size in bits (the whole part is p >> bits).
/// @return Whole units to apply this frame; sub-unit parts stay in r, so
///         motion smaller than one LSB is still delivered over time.
// Fractional accumulation preserves weak decay/DC motion below one sample LSB.
inline int32_t carryStep(int32_t p, int32_t &r, unsigned bits) {
    p += r;
    int32_t value = p >> bits;
    r = p - value * (1 << bits);
    return value;
}
/// Decay |x| by a Q24 fraction, accumulating sub-LSB parts across calls.
///
/// @param x Value to decay in/out; the sign is kept, the magnitude reduced.
/// @param error Q24 remainder accumulator in/out holding unapplied loss.
/// @param loss Q24 fraction of |x| to remove this call (loss < 2^24).
/// @note |x| and error are summed in unsigned form first, so oscillating
///       signs cannot cancel the intended damping.
// Accumulate magnitude loss so alternating oscillator signs do not cancel
// sub-LSB damping. Apply each accumulated unit using the current sign.
inline void decayWithRemainder(int32_t &x, uint32_t &error, int32_t loss) {
    uint32_t sum = uint32_t(abs32(x)) * uint32_t(loss) + error;
    int32_t d = int32_t(sum >> 24);
    error = sum & 0xffffffu;
    x += x < 0 ? d : -d;
}
/// Direct synthesis parameter set with boot defaults.
///
/// Defaults match the accepted reference preset: String .8, Jawari/Swell .65,
/// Sympathy 1, Pick 0. See member groups below for ranges and units.
// Direct Sitar Morph controls, unipolar Q15 except bipolar Pick.
struct Parameters {
    /// String/Jawari/Swell/Sympathy tone controls in Q15; pick is bipolar Q15.
    int32_t string = 26214, jawari = 21299, swell = 21299, sympathy = 32767, pick = 0;
    /// Pick position percent of one period; haloLevel trims the audible halo in Q15.
    int32_t pickPositionPercent = 17, haloLevel = 16384;
    /// Decay length scale factors in integer percent; 25..400, 100 = nominal.
    int32_t melodyDecayPercent = 100, sympatheticDecayPercent = 100;
};
/// Clamp every member of a parameter set into its legal range.
///
/// @param p Raw parameter values, possibly out of range.
/// @return Copy of p with every member clamped (see the ranges above).
inline Parameters bounded(Parameters p) {
    return {clip(p.string, 0, 32767),
            clip(p.jawari, 0, 32767),
            clip(p.swell, 0, 32767),
            clip(p.sympathy, 0, 32767),
            clip(p.pick),
            clip(p.pickPositionPercent, 5, 40),
            clip(p.haloLevel, 0, 32768),
            clip(p.melodyDecayPercent, 25, 400),
            clip(p.sympatheticDecayPercent, 25, 400)};
}
} // namespace mtm
