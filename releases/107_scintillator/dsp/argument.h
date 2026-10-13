// The "argument" stage: eight ways to combine the two audio inputs (A, B)
// into a single signal, selected by Knob X's 8-way zone. Modelled on the
// arithmetic units of a classic analog computer - the same box that could
// add, multiply or divide two voltages before a "function" unit shaped the
// result (see function.h).
#pragma once

#include <cstdint>

#include "common.h"

namespace scintillator
{

	// Divide-by-zero guard: |B| is floored to this before it's used as a
	// denominator, so a silent or centred B input can't produce an actual
	// divide-by-zero or a wildly noisy near-zero denominator. The result is
	// still hard-clipped afterwards, so a small B deliberately makes these
	// two operations spike and clip - that's the character of "dividing by
	// something close to nothing", not a bug.
	constexpr int32_t kArgDivGuardFloor = 8;

	inline int32_t __not_in_flash_func(ComputeArgument)(int zone, int32_t a, int32_t b)
	{
		switch (zone)
		{
		case 0: // A
			return a;

		case 1: // B
			return b;

		case 2: // A + B
			return Clip(a + b);

		case 3: // A - B
			return Clip(a - b);

		case 4: // A * B / 10
			// (p>>4) * 6554 >> 12 is p/10 to within 0.01%, without a
			// division on the audio path, and without overflowing int32.
			return Clip((((a * b) >> 4) * 6554) >> 12);

		case 5: // sqrt(A^2 + B^2) - always >= 0
		{
			int32_t sumSq = a * a + b * b; // max ~2048^2*2, well inside int32
			return Clip(IntSqrt(sumSq));
		}

		case 6: // A / |B|
		{
			int32_t denom = Cabs(b);
			if (denom < kArgDivGuardFloor) denom = kArgDivGuardFloor;
			return Clip(a / denom);
		}

		case 7: // 10 * A / |B|
		{
			int32_t denom = Cabs(b);
			if (denom < kArgDivGuardFloor) denom = kArgDivGuardFloor;
			return Clip((10 * a) / denom);
		}

		default:
			return 0;
		}
	}

} // namespace scintillator
