// svf.h — Chamberlin state-variable filter, all three taps every sample.
//
// Update order from WorkshopNibbleDrum's fx.cpp: high-pass from the previous
// states, integrate band-pass, then low-pass from the fresh band-pass.
// Coefficients are Q14, which keeps every product inside 31 bits for inputs
// of +/-2048 and states clamped to +/-32767.
//
// The two integrator states carry 8 extra fractional bits. Without them, at
// low cutoff f*hp >> 14 rounds to zero for |hp| < ~190 and the integrators
// freeze: tools/lpsim.py measured up to 117 LSB of DC stuck on the output,
// and quiet signals near the cutoff distort.

#pragma once
#include <cstdint>
#include "fastmath.h"

namespace lp {

class Svf
{
public:
	/// Control rate. cutoffU and resU are unipolar Q16.
	void Set(int32_t cutoffU, int32_t resU)
	{
		// f = 2*sin(pi*fc/fs), Q14. 86 is ~40Hz; ~7.75 octaves above that is
		// capped at 16000 (~7.8kHz), where the Chamberlin form is still stable
		// with the heaviest damping below (f^2 + 2fq < 4).
		f_ = pow2_scale(86, (cutoffU * 31) >> 6);
		if (f_ > 16000) f_ = 16000;
		// Damping 1/Q, Q14: 1.4 (no resonance) down to 0.08 (rings hard).
		q_ = 22938 - ((resU * 21627) >> 16);
	}

	void Reset() { lp_ = bp_ = hp_ = 0; }

	void __attribute__((always_inline)) Process(int32_t in)
	{
		constexpr int32_t kMax = 32767 << 8;
		int32_t hp = in - (lp_ >> 8) - ((q_ * (bp_ >> 8)) >> 14);
		bp_ = clamp_i32(bp_ + ((f_ * hp) >> 6), -kMax, kMax);
		lp_ = clamp_i32(lp_ + ((f_ * (bp_ >> 8)) >> 6), -kMax, kMax);
		// Clamped for the blend below: two taps' difference must fit mul_q15.
		hp_ = clamp_i32(hp, -32767, 32767);
	}

	int32_t Lp() const { return lp_ >> 8; }
	int32_t Bp() const { return bp_ >> 8; }
	int32_t Hp() const { return hp_; }

private:
	int32_t f_ = 1000;
	int32_t q_ = 22938;
	int32_t lp_ = 0, bp_ = 0;   // Q8 over the signal
	int32_t hp_ = 0;
};

/// A continuous walk LP -> BP -> HP along a knob, with a pure zone at each end
/// and in the middle: 0-20% LP, 40-60% BP, 80-100% HP, crossfades between.
/// No discrete switch, so nothing to chatter and no hysteresis needed.
struct SvfBlend
{
	int     seg  = 0;   // 0 LP, 1 LP->BP, 2 BP, 3 BP->HP, 4 HP
	int32_t frac = 0;   // Q15, within a crossfade segment

	/// Control rate. knob 0..4095.
	void Set(int32_t knob)
	{
		constexpr int32_t kStep = 819;         // 20% of the travel
		int32_t z = knob / kStep;              // 0..4 (4095/819 = 5, folded below)
		if (z > 4) z = 4;
		seg = static_cast<int>(z);
		frac = (knob - z * kStep) * 40;        // 819*40 = 32760
		if (frac > 32767) frac = 32767;
	}

	int32_t __attribute__((always_inline)) Render(const Svf &s) const
	{
		switch (seg)
		{
		case 0:  return s.Lp();
		case 1:  return s.Lp() + mul_q15(s.Bp() - s.Lp(), frac);
		case 2:  return s.Bp();
		case 3:  return s.Bp() + mul_q15(s.Hp() - s.Bp(), frac);
		default: return s.Hp();
		}
	}
};

} // namespace lp
