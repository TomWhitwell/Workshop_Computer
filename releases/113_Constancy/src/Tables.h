#pragma once
#include <cstdint>
#include "Config.h"
#include "TableData.h"

namespace constancy
{

// Lookups into the tables in TableData.h (made on a computer by
// tools/tables/make_tables.py), each blending between the two nearest
// entries. With these the card never needs floating point.

// Sine of a 32-bit phase (a whole cycle is 2^32), +/-32767. The top 10 bits
// pick a table entry; the next 16 slide linearly to the one after it.
__attribute__((always_inline)) inline int32_t Sine(uint32_t phase)
{
	uint32_t i = phase >> 22;
	int32_t frac = (int32_t)((phase >> 6) & 0xFFFF);
	int32_t a = table::sine[i];
	int32_t b = table::sine[i + 1];
	return a + (((b - a) * frac) >> 16);
}

// 2^(x/4096), Q16 (so Exp2(0) = 65536 and Exp2(4096) = 131072). x may be
// negative, down to a few octaves, and up to about 14 octaves.
inline int32_t Exp2(int32_t x)
{
	int32_t octave = x >> 12; // rounds down, so the fraction is always positive
	int32_t frac = x & 4095;
	int32_t i = frac >> 7;
	int32_t f = frac & 127;
	int32_t v = table::exp2[i] + (((table::exp2[i + 1] - table::exp2[i]) * f) >> 7);
	return octave >= 0 ? v << octave : v >> -octave;
}

// log2(x) x 4096, for x >= 1: the reverse of Exp2. Core 1 only.
inline int32_t Log2(uint32_t x)
{
	if (x == 0)
		return 0;
	// The highest bit set, by halving the search five times. (Not
	// __builtin_clz: on the RP2040 that calls into the boot ROM, which the
	// cycle-counting emulator doesn't have.)
	int32_t top = 0;
	for (int32_t step = 16; step > 0; step >>= 1)
	{
		if (x >> (top + step))
			top += step;
	}
	// Slide x so its highest bit is bit 15: 32768..65535 is 1.0..2.0.
	uint32_t m = top >= 15 ? x >> (top - 15) : x << (15 - top);
	int32_t f = (int32_t)(m - 32768); // the fraction, 15 bits
	int32_t i = f >> 10, r = f & 1023;
	return top * 4096 + table::log2[i] + (((table::log2[i + 1] - table::log2[i]) * r) >> 10);
}

// Somewhere between two values, exponentially -- so the middle of a range
// of times sounds like the middle: from a at amount 0 to b at amount 4096.
// a and b from 1 to 30000.
inline int32_t LogBlend(int32_t a, int32_t b, int32_t amountQ12)
{
	int32_t la = Log2((uint32_t)a), lb = Log2((uint32_t)b);
	int32_t l = la + (((lb - la) * amountQ12) >> 12);
	return (Exp2(l) + 32768) >> 16;
}

// A frequency as a pitch: MIDI note number x 256, so filter cutoffs can
// glide between settings evenly. 12 semitones x 256 per octave of Log2's
// 4096: x 3/4.
inline int32_t HzToPitchQ8(int32_t hz)
{
	return 69 * 256 + (Log2((uint32_t)hz) - Log2(440)) * 3 / 4;
}

// A pitch (MIDI note x 65536, so fractions of a semitone are smooth) as a
// phase step per sample. Core 1 only: it multiplies in 64 bits.
inline uint32_t PitchToInc(int32_t pitchQ16)
{
	if (pitchQ16 < 0)
		pitchQ16 = 0;
	if (pitchQ16 > (127 << 16))
		pitchQ16 = 127 << 16;
	int32_t note = pitchQ16 >> 16;
	// The fraction of a semitone, as a fraction of an octave (Q12): / 12.
	int32_t frac = (pitchQ16 & 0xFFFF) / 192;
	return (uint32_t)(((uint64_t)table::noteInc[note] * (uint32_t)Exp2(frac)) >> 16);
}

// The one-pole coefficient (Q15) for a cutoff pitch (MIDI note x 256).
inline int32_t CutoffCoef(int32_t pitchQ8)
{
	constexpr int32_t kTop = 135;
	if (pitchQ8 < 0)
		pitchQ8 = 0;
	if (pitchQ8 >= kTop << 8)
		return table::cutoff[kTop];
	int32_t i = pitchQ8 >> 8, f = pitchQ8 & 255;
	int32_t a = table::cutoff[i], b = table::cutoff[i + 1];
	return a + (((b - a) * f) >> 8);
}

// Look up a saturation curve (table::softClip). x is in the loop's units,
// where 32768 is full scale; beyond 4x full scale it holds the curve's end
// value.
__attribute__((always_inline)) inline int32_t Shape(const int16_t *curve, int32_t x)
{
	constexpr int32_t kRange = 4 << 15; // 4x full scale
	if (x >= kRange)
		return curve[512];
	if (x < -kRange)
		return curve[0];
	x += kRange;
	int32_t i = x >> 9; // 2^18 across 512 steps
	int32_t f = x & 511;
	int32_t a = curve[i], b = curve[i + 1];
	return a + (((b - a) * f) >> 9);
}

} // namespace constancy
