// osc.h — naive phase-accumulator waveforms and a two-shape morph.
//
// All shapes are phase-aligned: they cross zero rising at phase 0, so a
// crossfade between neighbours never cancels. Saw and square are not
// band-limited; fine at drone/bass pitches, audibly aliased up high.

#pragma once
#include <cstdint>
#include "fastmath.h"

namespace lp {

enum class Wave : uint8_t { Sine, Tri, Saw, Square };

static inline int32_t __attribute__((always_inline)) wave_q15(Wave w, uint32_t phase)
{
	switch (w)
	{
	case Wave::Sine:
		return fast_sin(phase);
	case Wave::Tri:
	{
		// Shift a quarter so the fold lands at phase 0.25 and 0.75.
		int32_t v = static_cast<int32_t>((phase + 0x40000000u) >> 15);   // 0..131071
		if (v >= 65536) v = 131071 - v;
		return v - 32768;
	}
	case Wave::Saw:
		return static_cast<int32_t>((phase + 0x80000000u) >> 16) - 32768;
	case Wave::Square:
	default:
		return (phase < 0x80000000u) ? 32767 : -32767;
	}
}

/// A point on a morph path: crossfade `frac` (Q15) of the way from a to b.
struct Morph
{
	Wave    a    = Wave::Sine;
	Wave    b    = Wave::Sine;
	int32_t frac = 0;

	/// Place `pos` (0..4095, a knob) along a path of `n` shapes.
	void Set(const Wave *path, int n, int32_t pos)
	{
		int32_t m = pos * (n - 1);                  // 0 .. 4095*(n-1)
		int32_t seg = m >> 12;
		if (seg >= n - 1)
		{
			a = b = path[n - 1];
			frac = 0;
			return;
		}
		a = path[seg];
		b = path[seg + 1];
		frac = (m & 0xFFF) << 3;                    // Q12 -> Q15, <= 32760
	}

	int32_t __attribute__((always_inline)) Render(uint32_t phase) const
	{
		int32_t va = wave_q15(a, phase);
		if (frac == 0) return va;
		int32_t vb = wave_q15(b, phase);
		return va + mul_q15(vb - va, frac);
	}
};

} // namespace lp
