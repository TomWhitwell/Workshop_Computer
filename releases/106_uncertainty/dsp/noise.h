// Three noise "colours" for the Buchla 266-style noise source.
//
// All three share one xorshift32 generator, so switching modes never clicks
// or restarts the underlying randomness — only how it's filtered changes.
//
//   Flat:        raw generator output, full-scale white noise.
//   Low-biased:  Paul Kellett's cheap pink-noise filter (-3dB/octave), a
//                well-known three-stage one-pole cascade.
//   High-biased: white minus pink. Pink noise is white noise with the highs
//                rolled off; subtracting it back out cancels the lows
//                instead, leaving a +3dB/octave tilt (blue noise) — the
//                mirror image of the pink filter, for the cost of one
//                subtraction rather than a second filter.
//
// Everything here is fixed-point int32_t: ProcessSample() has ~20us total,
// and float on the RP2040 is software-emulated.

#ifndef UNCERTAINTY_DSP_NOISE_H_
#define UNCERTAINTY_DSP_NOISE_H_

#include <cstdint>

namespace uncertainty
{

	// Marsaglia xorshift32. One multiply-free step, a handful of cycles.
	// Never returns 0 once seeded non-zero, which the shift chain requires.
	static inline uint32_t Xorshift32(uint32_t &s)
	{
		s ^= s << 13;
		s ^= s >> 17;
		s ^= s << 5;
		return s;
	}

	// Signed 12-bit white noise sample, -2048..2047, matching the card's
	// audio range directly so it can go straight to AudioOut.
	static inline int32_t WhiteSample(uint32_t &s)
	{
		return static_cast<int32_t>(Xorshift32(s) & 0x0FFF) - 2048;
	}

	class NoiseSource
	{
	public:
		enum Colour
		{
			Flat = 0,
			LowBiased = 1,  // -3dB/octave, pink
			HighBiased = 2, // +3dB/octave, blue
			kNumColours = 3
		};

		explicit NoiseSource(uint32_t seed = 1) : seed_(seed ? seed : 1) {}

		// Advances the shared generator once and returns the sample for the
		// requested colour. Called once per audio sample.
		int32_t Next(Colour colour)
		{
			int32_t white = WhiteSample(seed_);
			int32_t pink = Pink(white);
			switch (colour)
			{
			case LowBiased:
				return pink;
			case HighBiased:
				// Blue = white - pink, then rescaled: subtracting two
				// correlated full-scale-ish signals roughly halves the
				// resulting amplitude on average, so nudge it back up.
				return Clip12(((white - pink) * 3) >> 1);
			case Flat:
			default:
				return white;
			}
		}

	private:
		// Paul Kellett's "refined" pink noise filter: three one-pole stages
		// in parallel, each weighted so the summed response approximates
		// -3dB/octave from a few Hz up to Nyquist. Coefficients are the
		// well-known published constants, converted to Q15 fixed point.
		//
		// State is kept in Q15 (input scaled up by 32768/2048 headroom is
		// unnecessary here because the coefficients themselves are <1, so
		// plain int32_t multiply-then-shift is safe without overflowing
		// 32 bits for our 12-bit-range input).
		int32_t Pink(int32_t white)
		{
			// Coefficients (b_gain, white_gain), both Q15.
			constexpr int32_t kB0 = 32693, kW0 = 3246;  // 0.99765, 0.09905
			constexpr int32_t kB1 = 31558, kW1 = 9719;  // 0.96300, 0.29652
			constexpr int32_t kB2 = 18677, kW2 = 34489; // 0.57000, 1.05269 (>1 by design)

			b0_ = static_cast<int32_t>((static_cast<int64_t>(kB0) * b0_) >> 15) +
				  static_cast<int32_t>((static_cast<int64_t>(kW0) * white) >> 15);
			b1_ = static_cast<int32_t>((static_cast<int64_t>(kB1) * b1_) >> 15) +
				  static_cast<int32_t>((static_cast<int64_t>(kW1) * white) >> 15);
			b2_ = static_cast<int32_t>((static_cast<int64_t>(kB2) * b2_) >> 15) +
				  static_cast<int32_t>((static_cast<int64_t>(kW2) * white) >> 15);

			// Kellett's formula includes + white*0.1848 as a fourth term;
			// fold it in, then scale the whole sum back to our 12-bit range
			// (the filter's passband gain is a little over unity).
			int32_t sum = b0_ + b1_ + b2_ + ((white * 6053) >> 15); // 0.1848 in Q15
			return Clip12((sum * 3) >> 2);
		}

		static int32_t Clip12(int32_t v)
		{
			if (v > 2047) return 2047;
			if (v < -2048) return -2048;
			return v;
		}

		uint32_t seed_;
		int32_t b0_ = 0, b1_ = 0, b2_ = 0;
	};

} // namespace uncertainty

#endif // UNCERTAINTY_DSP_NOISE_H_
