// Fixed-point single-sideband frequency shifter, after the parasites
// FEATURE_MODE_FREQUENCY_SHIFTER.
//
// Structure: a phase-difference network (two cascades of four second-order
// allpass sections whose outputs are 90 degrees apart across the audio band)
// feeds a quadrature oscillator ring modulator, producing BOTH sidebands:
//
//   up   = I * cos(wt) - Q * sin(wt)
//   down = I * cos(wt) + Q * sin(wt)
//
// The xfade control crossfades the main output between the down and up
// sidebands (equal-power), and the aux output always carries the opposite
// mix, like the parasites dual up/down outputs. Feedback (soft-clipped,
// low-passed, xfade-dependent maximum as in the parasites "mic.w tweak")
// re-injects the main output into the shifter input for barberpole-style
// regeneration.
//
// Allpass coefficients are the classic Hilbert half-band pair (e.g. as used
// in the Warps/parasites frequency shifter and many phase-vocoder designs),
// quantised to Q14. Audio is Q12 throughout; no floats in the audio path
// (the sine table is built once at construction).

#ifndef ALLOY_DSP_FREQUENCY_SHIFTER_H_
#define ALLOY_DSP_FREQUENCY_SHIFTER_H_

#include <cmath>
#include <cstdint>

#include "dsp/xmod_algorithms.h"

namespace xmod {

// Second-order allpass: y[n] = a*(x[n] + y[n-2]) - x[n-2], a in Q14.
class AllpassSection
{
public:
	explicit AllpassSection(int32_t a_q14) : a_(a_q14) {}

	int32_t Process(int32_t x)
	{
		// Rounded shift to avoid the truncation DC bias (see vocoder.h).
		int32_t y = ((a_ * (x + y2_) + 8192) >> 14) - x2_;
		x2_ = x1_; x1_ = x;
		y2_ = y1_; y1_ = y;
		return y;
	}

private:
	int32_t a_;
	int32_t x1_ = 0, x2_ = 0;
	int32_t y1_ = 0, y2_ = 0;
};

class FrequencyShifter
{
public:
	FrequencyShifter()
	{
		for (int i = 0; i < kSineSize; ++i)
		{
			sine_q14_[i] = static_cast<int16_t>(
				16384.0f * sinf(2.0f * 3.14159265f * static_cast<float>(i) /
				                static_cast<float>(kSineSize)));
		}
	}

	// parameter: 0..4095. Centre (2048) = no shift; below shifts down, above
	// shifts up, with a quadratic curve out to roughly +/-1 kHz.
	// xfade: 0..4095 crossfades the main output from the down sideband (0)
	// to the up sideband (4095); the aux output gets the opposite mix.
	// feedback: 0..4095 re-injects the main output into the input.
	int32_t Process(int32_t carrier, int32_t modulator, int32_t parameter,
	                int32_t xfade, int32_t feedback)
	{
		int32_t x = Clip((carrier + modulator) >> 1);

		// Feedback, after the original: an expansive amount law
		// (a = f*(2-f), applied twice) and a maximum that grows as the
		// xfade leaves the centre (the parasites "mic.w tweak"), then
		// x += a * (SoftClip(x + max_fb * fb * a) - x).
		int32_t a = (feedback * (8190 - feedback)) >> 12;     // Q12
		a = (a * (8190 - a)) >> 12;                           // Q12
		int32_t t = xfade - 2048;                             // -2048..2047
		int32_t max_fb_q12 = 4096 + ((t * t) >> 10);          // 1.0..~3.0
		int32_t injected = x +
			((((feedback_sample_ * max_fb_q12) >> 12) * a) >> 12);
		x += (a * (SoftLimit(injected << 1) - x)) >> 12;

		// Two allpass paths, 90 degrees apart. Path B is additionally
		// delayed one sample to complete the quadrature relationship.
		int32_t i_path = x;
		for (auto& s : path_a_) i_path = s.Process(i_path);

		int32_t q_path = x;
		for (auto& s : path_b_) q_path = s.Process(q_path);
		int32_t q_delayed = q_z1_;
		q_z1_ = q_path;

		// Quadratic bipolar shift amount.
		int32_t d = parameter - 2048;               // -2048..2047
		bool up = d >= 0;
		if (d < 0) d = -d;
		uint32_t increment = static_cast<uint32_t>(d * d * 21); // ~1kHz max
		phase_ += up ? increment : 0u - increment;

		int32_t sin_v = SineQ14(phase_);
		int32_t cos_v = SineQ14(phase_ + 0x40000000u);

		int32_t ring_cos = (i_path * cos_v) >> 14;
		int32_t ring_sin = (q_delayed * sin_v) >> 14;
		int32_t up_band = ring_cos - ring_sin;
		int32_t down_band = ring_cos + ring_sin;

		// Equal-power sideband crossfade (same curve as Xfade()).
		int32_t fade_in = (xfade * (8190 - xfade)) >> 12;     // Q12
		int32_t u = kParamMax - xfade;
		int32_t fade_out = (u * (8190 - u)) >> 12;            // Q12
		int32_t main = (up_band * fade_in + down_band * fade_out) >> 12;
		aux_ = Clip((down_band * fade_in + up_band * fade_out) >> 12);

		// One-pole LP (~0.2) on the feedback tap to tame high frequencies.
		feedback_sample_ += ((main - feedback_sample_) * 819) >> 12;

		return Clip(main);
	}

	// Opposite sideband mix of the previous Process() call.
	int32_t aux() const { return aux_; }

private:
	static constexpr int kSineSize = 1024;

	int32_t SineQ14(uint32_t phase) const
	{
		// Top 10 bits index the table; next 8 bits linearly interpolate.
		uint32_t index = phase >> 22;
		int32_t frac = static_cast<int32_t>((phase >> 14) & 0xFF);
		int32_t a = sine_q14_[index];
		int32_t b = sine_q14_[(index + 1) & (kSineSize - 1)];
		return a + (((b - a) * frac) >> 8);
	}

	// Hilbert phase-difference network coefficients, Q14.
	AllpassSection path_a_[4] = {
		AllpassSection(11344),  // 0.6923878
		AllpassSection(15336),  // 0.9360654
		AllpassSection(16191),  // 0.9882295
		AllpassSection(16363),  // 0.9987488
	};
	AllpassSection path_b_[4] = {
		AllpassSection(6590),   // 0.4021921
		AllpassSection(14027),  // 0.8561711
		AllpassSection(15930),  // 0.9722910
		AllpassSection(16307),  // 0.9952885
	};

	int32_t q_z1_ = 0;
	int32_t feedback_sample_ = 0;
	int32_t aux_ = 0;
	uint32_t phase_ = 0;
	int16_t sine_q14_[kSineSize];
};

} // namespace xmod

#endif // ALLOY_DSP_FREQUENCY_SHIFTER_H_
