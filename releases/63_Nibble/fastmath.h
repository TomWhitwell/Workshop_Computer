// fastmath.h — fixed-point primitives for the 48kHz audio path.
//
// Everything here is integer-only. No libm, no float: the build runs with
// -Wdouble-promotion -Wfloat-conversion and the RP2040 has no FPU, so a stray
// double would cost hundreds of cycles inside ProcessSample().
//
// Conventions:
//   Q15  — int32_t, 32768 == 1.0. Used for signed unit values (sine, pan).
//   Q16  — int32_t, 65536 == 1.0. Used for levels, probabilities, envelopes.
//   phase— uint32_t, the full 32-bit range == one cycle. Wraps for free.

#pragma once
#include <stdint.h>

namespace nib {

constexpr int32_t kQ15One = 32768;
constexpr int32_t kQ16One = 65536;

// ---------------------------------------------------------------------------
// Sine
// ---------------------------------------------------------------------------

// Quarter-wave sine table, Q15. 257 entries so the interpolator can always read
// kSinTable[i+1] without a bounds check. Index i corresponds to angle
// (i/256)*(pi/2), value = round(32767 * sin(angle)).
extern const int16_t kSinTable[257];

/// Sine of a 32-bit phase (full range = one cycle). Returns Q15, -32767..32767.
///
/// Reconstructs the full wave from the quarter table by folding the top two
/// phase bits: quadrant 1/2 mirror the index, quadrant 2/3 negate the result.
/// Linearly interpolates between table entries, so error is well under 1 LSB
/// at 12-bit audio depth.
static inline int32_t __attribute__((always_inline)) fast_sin(uint32_t phase)
{
	uint32_t quadrant = phase >> 30;          // 0..3
	uint32_t frac     = (phase >> 6) & 0xFFFFFF; // 24 bits within the quadrant
	uint32_t idx      = frac >> 16;           // 0..255
	uint32_t mu       = frac & 0xFFFF;        // interpolation weight, Q16

	// Quadrants 1 and 3 traverse the quarter-wave backwards.
	if (quadrant & 1)
	{
		idx = 255 - idx;
		mu  = 65536 - mu;
		if (mu == 65536) { mu = 0; idx++; }
	}

	int32_t a = kSinTable[idx];
	int32_t b = kSinTable[idx + 1];
	int32_t v = a + (((b - a) * static_cast<int32_t>(mu)) >> 16);

	// Quadrants 2 and 3 are the negative half of the cycle.
	return (quadrant & 2) ? -v : v;
}

// ---------------------------------------------------------------------------
// Randomness
// ---------------------------------------------------------------------------

/// Marsaglia xorshift32. One multiply-free step, ~5 cycles. Never returns 0
/// once seeded non-zero, which is exactly what the shift chain requires.
static inline uint32_t __attribute__((always_inline)) xorshift32(uint32_t &s)
{
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

/// Uniform random in Q16 [0, 65536).
static inline int32_t __attribute__((always_inline)) rand_q16(uint32_t &s)
{
	return static_cast<int32_t>(xorshift32(s) >> 16);
}

/// Uniform random in Q15 [-32768, 32767] — a bipolar unit value.
static inline int32_t __attribute__((always_inline)) rand_bipolar(uint32_t &s)
{
	return static_cast<int32_t>(xorshift32(s) >> 17) - 16384;
}

// ---------------------------------------------------------------------------
// Envelopes / smoothing
// ---------------------------------------------------------------------------

/// One-pole exponential decay: v -= v >> shift. Larger shift = slower decay.
/// The `v > 0` floor stops the shift from stalling at a small non-zero value
/// (v >> shift rounds to 0 once v < 2^shift, which would leave a DC tail).
static inline int32_t __attribute__((always_inline)) fast_exp_decay(int32_t v, uint8_t shift)
{
	int32_t d = v >> shift;
	if (d == 0) return 0;
	return v - d;
}

/// One-pole slew toward a target: v += (target - v) >> shift.
///
/// STALLS by design: once |target - v| < 2^shift the shift rounds to zero and
/// this stops moving, permanently short of the target. That is harmless — even
/// desirable — for audio smoothing, where the residual is inaudible and the
/// early-out saves work.
///
/// It is NOT acceptable anywhere a value is later compared against a threshold.
/// Use slew_exact() there. See its comment for what this cost us.
static inline int32_t __attribute__((always_inline)) slew(int32_t v, int32_t target, uint8_t shift)
{
	return v + ((target - v) >> shift);
}

/// One-pole slew that always REACHES its target.
///
/// The plain shift above stalls, and on a signed shift the stall is
/// ASYMMETRIC: arithmetic shift-right floors toward negative infinity, so
/// approaching a target from above lands exactly while approaching from below
/// stops ~2^shift short. A value settled from one direction therefore differs
/// from the same physical voltage settled from the other.
///
/// That is fatal for level discrimination. Measured in tools/ghostsim.py at 17
/// units of direction-dependent error on the CV input chain — over a quarter of
/// kCollisionMin — which put two combos on the wrong side of a learned
/// threshold and broke the learn round-trip. The guard is the same one
/// 18_chord_organ uses at main.cpp:136-138: if the step rounds to zero but a
/// difference remains, move one LSB.
static inline int32_t __attribute__((always_inline)) slew_exact(int32_t v, int32_t target, uint8_t shift)
{
	int32_t d = target - v;
	if (d == 0) return v;
	int32_t step = d >> shift;
	if (step == 0) step = (d > 0) ? 1 : -1;
	return v + step;
}

// ---------------------------------------------------------------------------
// Scaling helpers
// ---------------------------------------------------------------------------

/// Multiply two Q16 values, result Q16. Uses a 64-bit intermediate: Q16 levels
/// can reach 65536, and 65536*65536 overflows 32 bits.
static inline int32_t __attribute__((always_inline)) mul_q16(int32_t a, int32_t b)
{
	return static_cast<int32_t>((static_cast<int64_t>(a) * b) >> 16);
}

/// Multiply a signal by a Q15 gain. Inputs stay well inside 32 bits here
/// (audio is +/-2048), so no 64-bit widening is needed.
static inline int32_t __attribute__((always_inline)) mul_q15(int32_t a, int32_t g)
{
	return (a * g) >> 15;
}

/// Square root of a Q16 value in 0..65536, result Q16 in 0..65536.
///
/// Restoring bitwise integer sqrt: no libm, no float, no divide, and bounded at
/// 16 iterations. Used to bend knob laws so the bottom of the travel moves
/// fastest -- sqrt(x) rises steeply from zero, which is exactly the shape a
/// control needs when its underlying model has a threshold near the bottom.
static inline int32_t __attribute__((always_inline)) fast_sqrt_q16(int32_t x)
{
	if (x <= 0) return 0;
	// sqrt(x/65536)*65536 == sqrt(x*65536), so widen before rooting.
	uint32_t v = static_cast<uint32_t>(x) << 16;
	uint32_t res = 0;
	uint32_t bit = 1u << 30;
	while (bit > v) bit >>= 2;
	while (bit)
	{
		if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; }
		else                { res >>= 1; }
		bit >>= 2;
	}
	return static_cast<int32_t>(res);
}

/// Clamp to the DAC's signed 12-bit range.
static inline int16_t __attribute__((always_inline)) clamp12(int32_t v)
{
	if (v < -2048) return -2048;
	if (v >  2047) return  2047;
	return static_cast<int16_t>(v);
}

/// Linear map of a 12-bit knob (0..4095) to Q16 (0..65536).
static inline int32_t __attribute__((always_inline)) knob_to_q16(int32_t knob)
{
	return knob << 4;
}

// ---------------------------------------------------------------------------
// Rate -> phase increment
// ---------------------------------------------------------------------------

/// Convert a Q8 frequency (Hz * 256) into a phase increment at the control rate.
///
/// The obvious form is
///     (int64_t)hz_q8 * 4294967296LL / (256LL * kCtrlRate)
/// and it is what this code used to do — in three places, one of them inside a
/// twelve-iteration loop. **That was the single biggest cause of the card
/// overrunning its 20.8us sample budget by 4-8x.** The Cortex-M0+ has no
/// hardware divider at all, so a 64-bit divide is a libgcc call costing several
/// hundred cycles, and the compiler cannot strength-reduce it because the
/// numerator is 64-bit.
///
/// Dividing by the compile-time constant 256*kCtrlRate is the same as
/// multiplying by 2^32/(256*kCtrlRate), precomputed here as a Q16 constant.
/// The result is EXACT over the whole useful range, and costs one 64-bit
/// multiply instead of a divide.
///
/// NIBBLE runs a 3000Hz control tick, not WorkshopBio's 1500Hz, so this
/// constant is HALF the value that file carries. If kCtrlRate ever changes,
/// this must change with it: round(2^32 / (256 * kCtrlRate) * 65536).
constexpr uint32_t kHzToIncQ16 = 366503876u;    // round(2^32 / 768000 * 65536)

static inline uint32_t __attribute__((always_inline)) hz_to_inc(int32_t hz_q8)
{
	if (hz_q8 < 0) hz_q8 = 0;
	return static_cast<uint32_t>(
		(static_cast<uint64_t>(static_cast<uint32_t>(hz_q8)) * kHzToIncQ16) >> 16);
}

} // namespace nib
