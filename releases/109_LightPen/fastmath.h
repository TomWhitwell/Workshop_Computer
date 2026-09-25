// fastmath.h — fixed-point primitives for the 48kHz audio path.
//
// Ported from WorkshopNibbleDrum's fastmath.h (namespace nko), trimmed to what
// LightPen uses, plus pow2_scale() and note_to_inc() for knob laws and pitch.
//
// Everything here is integer-only. No libm, no float: the build runs with
// -Wdouble-promotion -Wfloat-conversion and the RP2040 has no FPU, so a stray
// double would cost hundreds of cycles inside ProcessSample().
//
// Conventions:
//   Q15  — int32_t, 32768 == 1.0. Used for signed unit values (waveforms, mixes).
//   Q16  — int32_t, 65536 == 1.0. Used for levels and unipolar sensor values.
//   phase— uint32_t, the full 32-bit range == one cycle. Wraps for free.

#pragma once
#include <cstdint>

namespace lp {

constexpr int32_t kQ15One = 32768;
constexpr int32_t kQ16One = 65536;

constexpr int32_t kSampleRate = 48000;
constexpr int32_t kCtrlDiv    = 32;                       // control tick every 32 samples
constexpr int32_t kCtrlRate   = kSampleRate / kCtrlDiv;   // 1500Hz

/// A press shorter than this is a TAP — it cycles a mode's option. Longer is
/// the mode's record gesture. 171ms sits above the slowest deliberate tap
/// (~150ms) and below the shortest swipe the LDRs can physically resolve
/// (~300ms: a CdS cell's own rise and fall is 10-30ms, so a bar passing
/// faster than that is smeared away before the ADC ever sees it).
constexpr int kTapTicks  = 256;
constexpr int kHoldTicks = kCtrlRate;   // 1s

// ---------------------------------------------------------------------------
// Sine
// ---------------------------------------------------------------------------

// Quarter-wave sine table, Q15. 257 entries so the interpolator can always read
// kSinTable[i+1] without a bounds check.
extern const int16_t kSinTable[257];

/// Sine of a 32-bit phase (full range = one cycle). Returns Q15, -32767..32767.
static inline int32_t __attribute__((always_inline)) fast_sin(uint32_t phase)
{
	uint32_t quadrant = phase >> 30;
	uint32_t frac     = (phase >> 6) & 0xFFFFFF;
	uint32_t idx      = frac >> 16;
	uint32_t mu       = frac & 0xFFFF;

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

	return (quadrant & 2) ? -v : v;
}

// ---------------------------------------------------------------------------
// Smoothing
// ---------------------------------------------------------------------------

/// One-pole slew toward a target: v += (target - v) >> shift.
///
/// STALLS by design once |target - v| < 2^shift, and the stall is asymmetric
/// (arithmetic shift floors toward -inf). Fine for audio smoothing; NOT fine
/// anywhere the value is later compared against a threshold — use slew_exact().
static inline int32_t __attribute__((always_inline)) slew(int32_t v, int32_t target, uint8_t shift)
{
	return v + ((target - v) >> shift);
}

/// One-pole slew that always REACHES its target: if the shifted step rounds to
/// zero but a difference remains, move one LSB. NibbleDrum measured 17 units of
/// direction-dependent error without this guard (tools/ghostsim.py there).
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

/// Multiply a signal by a Q15 gain. Callers keep |a| * |g| inside 31 bits:
/// a full-scale Q15 difference (65535) times a Q15 weight (<= 32767) just fits.
static inline int32_t __attribute__((always_inline)) mul_q15(int32_t a, int32_t g)
{
	return (a * g) >> 15;
}

/// Clamp to the DAC's signed 12-bit range.
static inline int16_t __attribute__((always_inline)) clamp12(int32_t v)
{
	if (v < -2048) return -2048;
	if (v >  2047) return  2047;
	return static_cast<int16_t>(v);
}

static inline int32_t __attribute__((always_inline)) clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/// base * 2^(octQ12 / 4096), for exponential knob and sensor laws.
///
/// Piecewise-linear within each octave: monotonic, exact at whole octaves,
/// at most ~6% sharp mid-octave — plenty for a control law, not for pitch
/// (pitch uses note_to_inc's semitone table). Negative octaves allowed.
/// 64-bit intermediate, so CONTROL RATE ONLY.
static inline int32_t pow2_scale(int32_t base, int32_t octQ12)
{
	int32_t whole = octQ12 >> 12;          // floors, also for negatives
	int32_t frac  = octQ12 & 0xFFF;
	int64_t v = base;
	if (whole > 0) v <<= whole;
	else if (whole < 0) v >>= -whole;
	return static_cast<int32_t>(v + ((v * frac) >> 12));
}

// ---------------------------------------------------------------------------
// Noise
// ---------------------------------------------------------------------------

/// Marsaglia xorshift32. One multiply-free step, ~6 cycles. Never returns 0
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

/// Bipolar unit noise, the FULL -32768..32767. WorkshopNibbleDrum's version of
/// this shifts by 17 and so only reaches +/-16384 despite its comment; the
/// divergence here is deliberate, so do not "fix" it back.
static inline int32_t __attribute__((always_inline)) rand_bipolar(uint32_t &s)
{
	return static_cast<int32_t>(xorshift32(s) >> 16) - 32768;
}

/// White noise at the DAC's scale, -2048..2047.
static inline int32_t __attribute__((always_inline)) rand_audio(uint32_t &s)
{
	return static_cast<int32_t>(xorshift32(s) >> 20) - 2048;
}

// ---------------------------------------------------------------------------
// Logarithm
// ---------------------------------------------------------------------------

// log2(1 + i/32) in Q16, 33 entries so the interpolator can read i+1. 32-bit
// because the last entry, log2(2), is 65536 exactly.
extern const uint32_t kLog2Table[33];

/// log2 of a positive integer, Q16. Exponent from the leading bit, mantissa
/// from the table above: within 0.0002 bits. Used to spread each LDR evenly
/// across its own resistance range, which is boot-time work — the Cortex-M0+
/// has no CLZ instruction, so this is not for the audio path.
static inline int32_t log2_q16(uint32_t v)
{
	if (v == 0) return 0;
	int msb = 31 - __builtin_clz(v);
	// Mantissa as Q16 in [1, 2): 65536..131071.
	uint32_t m = (msb >= 16) ? (v >> (msb - 16)) : (v << (16 - msb));
	uint32_t x = m - 65536;
	uint32_t i = x >> 11;
	uint32_t f = x & 0x7FF;
	uint32_t a = kLog2Table[i];
	uint32_t b = kLog2Table[i + 1];
	return (msb << 16) + static_cast<int32_t>(a + (((b - a) * f) >> 11));
}

// 2^(i/32) in Q16, 33 entries so the interpolator can read i+1.
extern const uint32_t kExp2Table[33];

/// 2^x for x in Q16, returning Q16, saturating. The mirror of log2_q16, and
/// good to 6e-5 relative — pow2_scale is NOT a substitute here: its 6%
/// mid-octave error would put a visible ripple on the sensor response law.
/// Boot-time work, like log2_q16.
static inline int32_t exp2_q16(int32_t xQ16)
{
	int32_t  w = xQ16 >> 16;                              // floors, also for negatives
	uint32_t f = static_cast<uint32_t>(xQ16) & 0xFFFF;
	uint32_t a = kExp2Table[f >> 11];
	uint32_t b = kExp2Table[(f >> 11) + 1];
	uint32_t m = a + (((b - a) * (f & 0x7FF)) >> 11);     // Q16 in [1, 2)
	if (w >= 15) return INT32_MAX;
	if (w >= 0)  return static_cast<int32_t>(m << w);
	if (w < -17) return 0;
	return static_cast<int32_t>((m + (1u << (-w - 1))) >> -w);   // round to nearest
}

/// base^exp for a positive base and exp in Q16. Boot-time only: two table
/// lookups and a 64-bit multiply.
static inline int32_t pow_q16(uint32_t base, int32_t expQ16)
{
	if (base == 0) return 0;
	int64_t l = static_cast<int64_t>(log2_q16(base)) * expQ16;
	return exp2_q16(static_cast<int32_t>(l >> 16));
}

/// Square root of a Q16 value, result Q16. Restoring bitwise integer sqrt:
/// no libm, no divide, bounded at 16 iterations. Boot-time only here.
static inline int32_t fast_sqrt_q16(int32_t x)
{
	if (x <= 0) return 0;
	uint32_t v = static_cast<uint32_t>(x) << 16;   // sqrt(x/2^16)*2^16 == sqrt(x*2^16)
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

static inline int32_t max3_i32(int32_t a, int32_t b, int32_t c)
{
	int32_t m = a > b ? a : b;
	return m > c ? m : c;
}

// ---------------------------------------------------------------------------
// Pitch
// ---------------------------------------------------------------------------

// Phase increments at 48kHz for MIDI notes 0..12 (C-1 .. C0), A4 = 440Hz.
extern const uint32_t kNoteInc[13];

/// Hz -> phase increment per sample at 48kHz. Compile time only: the 64-bit
/// intermediate is free in a constexpr and would not be in the audio path.
static constexpr uint32_t HzToInc(int32_t hz)
{
	return static_cast<uint32_t>((static_cast<int64_t>(hz) << 32) / kSampleRate);
}

/// Triangle fold: reflect off +/-2048 as many times as the drive needs.
/// Closed form over one 8192-unit period, so no loop in the interrupt, and the
/// identity for anything already inside the rails — at 8x drive a full-scale
/// input needs more reflections than an unrolled reflect loop would give it.
static inline int32_t __attribute__((always_inline)) fold12(int32_t v)
{
	int32_t t = (v + 2048) & 8191;
	if (t > 4096) t = 8192 - t;
	return t - 2048;
}

/// MIDI note in Q8 (note * 256) -> phase increment per sample at 48kHz.
/// Semitone table plus linear interpolation, then an octave shift.
/// Uses a divide, so CONTROL RATE ONLY.
static inline uint32_t note_to_inc(int32_t noteQ8)
{
	noteQ8 = clamp_i32(noteQ8, 0, 127 << 8);
	int32_t n    = noteQ8 >> 8;
	int32_t frac = noteQ8 & 0xFF;
	int32_t oct  = n / 12;
	int32_t semi = n - oct * 12;
	uint32_t a = kNoteInc[semi];
	uint32_t b = kNoteInc[semi + 1];
	uint32_t inc = a + (((b - a) * static_cast<uint32_t>(frac)) >> 8);
	return inc << oct;
}

} // namespace lp
