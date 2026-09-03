// Buchla-lineage wavefolder (not a Source of Uncertainty circuit itself,
// but built in the same spirit to sit alongside it on the card).
//
// Folding works by reflecting the signal back and forth off a pair of
// mirrors at +-threshold, the way a West Coast folder reflects a triangle
// wave: anything that would go past the mirror bounces back instead of
// clipping. A smaller threshold means the signal hits the mirrors more
// often per cycle, i.e. more folds, i.e. a brighter, more complex tone.
//
// Rather than looping the reflection (variable iteration count is a poor
// fit for a fixed ~20us budget) or building a lookup table, the whole
// multi-reflection shape is a triangle wave of period 4*threshold, so one
// integer modulo gets the same result in constant time. The RP2040 has a
// single-cycle hardware divider, so this is cheap.

#ifndef UNCERTAINTY_DSP_WAVEFOLDER_H_
#define UNCERTAINTY_DSP_WAVEFOLDER_H_

#include <cstdint>

namespace uncertainty
{

	// Folds x (any range) into a triangle-wave reflection with corners at
	// +-threshold. threshold must be > 0.
	static inline int32_t TriangleFold(int32_t x, int32_t threshold)
	{
		int32_t period = 4 * threshold;

		// Wrap x into one period, [0, period). C++'s % can return negative
		// for negative x, so correct that by hand.
		int32_t m = x % period;
		if (m < 0) m += period;

		// One period of a triangle wave: rises 0..threshold, falls back
		// through 2*threshold, rises again to 4*threshold=0.
		if (m <= threshold)
			return m;
		else if (m <= 3 * threshold)
			return 2 * threshold - m;
		else
			return m - period;
	}

	class Wavefolder
	{
	public:
		// in: audio input, -2048..2047.
		// intensity: fold control, -2048..2047 (raw CV in), higher = more
		// folding. Only the positive half is used, matching a unipolar
		// "amount" control fed from CV.
		int32_t Process(int32_t in, int32_t intensity) const
		{
			int32_t clamped = intensity;
			if (clamped < 0) clamped = 0;
			if (clamped > 2047) clamped = 2047;

			// threshold shrinks from full-scale (no folding) down to a
			// small floor (heavy folding) as intensity rises.
			constexpr int32_t kMinThreshold = 96;
			int32_t threshold = 2047 - ((clamped * (2047 - kMinThreshold)) >> 11);
			if (threshold < kMinThreshold) threshold = kMinThreshold;

			return TriangleFold(in, threshold);
		}
	};

} // namespace uncertainty

#endif // UNCERTAINTY_DSP_WAVEFOLDER_H_
