#pragma once
#include <cstdint>
#include "Dsp.h"

namespace tem
{

// Plate reverb: Jon Dattorro's figure-of-eight tank ("Effect Design, Part 1",
// JAES 1997), at a fixed 24kHz, outside the sample clock: the wash stays
// smooth however slow the clock is.
//
//   in -> 4 allpass diffusers --+--> L: mod allpass -> delay -> damp -> x decay -> allpass -> delay --+
//                               |                                                                    |
//                               +--> R: mod allpass -> delay -> damp -> x decay -> allpass -> delay --+
//                                    ^ (each side is fed by the other side's end, x decay)
//
// The diffusers smear the input into a wash. The tank is two halves fed
// round into each other, each half darker (the damping low-pass) and
// quieter (the decay gain) every time round. The first allpass in each
// half is modulated by a slow LFO (0.3 and 0.37Hz, +-8 samples, from core
// 1): the tank's echoes drift against each other, which is most of the
// plate's washiness and stops it ringing at fixed pitches. Stereo comes
// from Dattorro's output taps: each side is a sum of seven points inside
// the tank, mostly from the other half.
//
// The published delay lengths are for 29.76kHz; scaled by 24000 / 29761 here.
//
// Timing: this runs inside the audio interrupt, at half the sample rate,
// split so every sample costs about the same (the plan Recurrence's shimmer
// uses):
//
//   even samples  average the last two inputs (an anti-alias filter for the
//                 drop to 24kHz), the diffusers, the left half of the tank
//   odd samples   the right half, and both outputs
//
// All the lines share one buffer with one write position, so each access
// is (position + constant), wrapped. 18,171 int16s: 36KB.
//
// Safety: decay tops out at 0.93; every write into the tank is saturated,
// and anything below 2 units is flushed to zero, so the tail always dies
// away to true silence (Impermanence measured a single unit circulating for
// ever without that).
class Plate
{
public:
	// `in`: the two loops x the send, 12-bit scale (up to about +-3000). `decayQ15`, `dampQ15`:
	// from the Wash macro (dampQ15 is 1 - damping: the low-pass's coefficient).
	// `lfoQ8`: the two allpass modulations, 1/256 samples. Writes the wet
	// signal, 12-bit scale.
	__attribute__((noinline)) void Process(int32_t in, int32_t decayQ15, int32_t dampQ15, const int32_t *lfoQ8,
										   int32_t &outL, int32_t &outR)
	{
		if (!phase_)
		{
			// A new frame: the write position moves on one.
			w_ = w_ == 0 ? kSize - 1 : w_ - 1;

			// x 4 into the int16 lines (the average of two inputs, x 4):
			// headroom for two loud loops (up to about +-3000) and two
			// bits of resolution below the 12-bit input for the tail.
			int32_t x = (in + lastIn_) * 2;
			x = Allpass<kIn1, kIn1Len>(x, kInDiffuse1);
			x = Allpass<kIn2, kIn2Len>(x, kInDiffuse1);
			x = Allpass<kIn3, kIn3Len>(x, kInDiffuse2);
			x = Allpass<kIn4, kIn4Len>(x, kInDiffuse2);
			diffused_ = x;

			// Left half, fed by the right half's end.
			int32_t t = x + ScaleQ15(Read<kRd2>(kRd2Len), decayQ15);
			t = ModAllpass<kLap1, kLap1Len>(t, lfoQ8[0]);
			WriteLoop<kLd1>(t);
			Damp(dampL_, Read<kLd1>(kLd1Len), dampQ15);
			t = Allpass<kLap2, kLap2Len>(ScaleQ15(dampL_, decayQ15), kDecayDiffuse2);
			WriteLoop<kLd2>(t);

			// Output: halfway between the last two frames.
			outL = (prevL_ + curL_) >> 1;
			outR = (prevR_ + curR_) >> 1;
		}
		else
		{
			// Right half, fed by the left half's end.
			int32_t t = diffused_ + ScaleQ15(Read<kLd2>(kLd2Len), decayQ15);
			t = ModAllpass<kRap1, kRap1Len>(t, lfoQ8[1]);
			WriteLoop<kRd1>(t);
			Damp(dampR_, Read<kRd1>(kRd1Len), dampQ15);
			t = Allpass<kRap2, kRap2Len>(ScaleQ15(dampR_, decayQ15), kDecayDiffuse2);
			WriteLoop<kRd2>(t);

			// Dattorro's output taps.
			int32_t l = Read<kRd1>(215) + Read<kRd1>(2398) - Read<kRap2>(1543) + Read<kRd2>(1610) -
						Read<kLd1>(1605) - Read<kLap2>(151) - Read<kLd2>(860);
			int32_t r = Read<kLd1>(285) + Read<kLd1>(2925) - Read<kLap2>(990) + Read<kLd2>(2156) -
						Read<kRd1>(1702) - Read<kRap2>(270) - Read<kRd2>(98);

			// The output runs one frame behind, so it can step through the
			// frames in order: the previous frame now, the midpoint on the
			// next (even) sample -- straight-line interpolation from 24kHz
			// back up to 48kHz. Back to 12-bit scale from x 4, at about
			// 0.23 of the tap sum, which keeps a full wash near the dry
			// level. (Dattorro's own output gain, 0.6, would put it at 2.4
			// times the dry, measured in the offline renders.)
			prevL_ = curL_;
			prevR_ = curR_;
			curL_ = (l * 60) >> 10; // x 0.234 / 4
			curR_ = (r * 60) >> 10;
			outL = prevL_;
			outR = prevR_;
		}
		lastIn_ = in;
		phase_ ^= 1;
	}

private:
	// Line lengths at 24kHz.
	static constexpr int32_t kIn1Len = 115, kIn2Len = 86, kIn3Len = 306, kIn4Len = 223;
	static constexpr int32_t kLap1Len = 542, kLd1Len = 3591, kLap2Len = 1452, kLd2Len = 3000;
	static constexpr int32_t kRap1Len = 732, kRd1Len = 3401, kRap2Len = 2142, kRd2Len = 2551;
	static constexpr int32_t kExcursion = 8; // the modulated allpasses swing +-8 samples

	// Where each line starts in the shared buffer. A line of length L needs
	// L+1 slots (written at +0, read up to +L); a modulated one needs room
	// for its swing, and one more for the interpolation.
	static constexpr int32_t kIn1 = 0;
	static constexpr int32_t kIn2 = kIn1 + kIn1Len + 1;
	static constexpr int32_t kIn3 = kIn2 + kIn2Len + 1;
	static constexpr int32_t kIn4 = kIn3 + kIn3Len + 1;
	static constexpr int32_t kLap1 = kIn4 + kIn4Len + 1;
	static constexpr int32_t kLd1 = kLap1 + kLap1Len + kExcursion + 2;
	static constexpr int32_t kLap2 = kLd1 + kLd1Len + 1;
	static constexpr int32_t kLd2 = kLap2 + kLap2Len + 1;
	static constexpr int32_t kRap1 = kLd2 + kLd2Len + 1;
	static constexpr int32_t kRd1 = kRap1 + kRap1Len + kExcursion + 2;
	static constexpr int32_t kRap2 = kRd1 + kRd1Len + 1;
	static constexpr int32_t kRd2 = kRap2 + kRap2Len + 1;
	static constexpr int32_t kSize = kRd2 + kRd2Len + 1;
	static_assert(kSize * 2 <= 36 * 1024 + 512, "the brief's budget: 36KB of reverb lines");

	// Dattorro's diffusion coefficients, Q15.
	static constexpr int32_t kInDiffuse1 = 24576;	  // 0.75
	static constexpr int32_t kInDiffuse2 = 20480;	  // 0.625
	static constexpr int32_t kDecayDiffuse1 = -22938; // -0.70: the modulated allpasses
	// 0.5: Dattorro sets this to decay + 0.15, kept within 0.25..0.5; with
	// decay at 0.5 or more, as here, that is always 0.5.
	static constexpr int32_t kDecayDiffuse2 = 16384;

	// The line accessors are forced inline: called out of line, as the
	// compiler first chose, each one cost a call and return (measured in
	// tools/bench).
	__attribute__((always_inline)) int32_t Index(int32_t at) const
	{
		int32_t i = w_ + at;
		return i >= kSize ? i - kSize : i;
	}

	template <int32_t base>
	__attribute__((always_inline)) int32_t Read(int32_t delay) const { return buf_[Index(base + delay)]; }

	template <int32_t base>
	__attribute__((always_inline)) void Write(int32_t v) { buf_[Index(base)] = (int16_t)Sat16(v); }

	// Writing into the tank flushes anything below 2 units to zero, so no
	// single unit can circulate for ever.
	template <int32_t base>
	__attribute__((always_inline)) void WriteLoop(int32_t v)
	{
		// Branchless |v| < 2 -> 0: mask is 0 when v is -1, 0 or 1.
		int32_t mask = -(int32_t)((uint32_t)(v + 1) > 2u);
		buf_[Index(base)] = (int16_t)(Sat16(v) & mask);
	}

	// Schroeder allpass: passes every frequency at the same level but
	// smears it in time -- the building block of reverb diffusion.
	template <int32_t base, int32_t len>
	__attribute__((always_inline)) int32_t Allpass(int32_t in, int32_t gQ15)
	{
		int32_t d = Read<base>(len);
		int32_t v = Sat16(in - ((d * gQ15) >> 15));
		Write<base>(v);
		return d + ((v * gQ15) >> 15);
	}

	// The same, with its length swung by the LFO, read between two
	// samples so the swing is smooth.
	template <int32_t base, int32_t len>
	__attribute__((always_inline)) int32_t ModAllpass(int32_t in, int32_t lfoQ8)
	{
		int32_t at = len * 256 + lfoQ8;
		int32_t i = at >> 8, frac = at & 255;
		int32_t a = Read<base>(i);
		int32_t d = a + (((Read<base>(i + 1) - a) * frac) >> 8);
		int32_t v = Sat16(in - ((d * kDecayDiffuse1) >> 15));
		WriteLoop<base>(v);
		return d + ((v * kDecayDiffuse1) >> 15);
	}

	// One-pole low-pass, its step rounded away from zero, so it always
	// settles exactly on its input. (Recurrence's Damp, from Equanimity.)
	static void Damp(int32_t &state, int32_t in, int32_t kQ15)
	{
		int32_t d = in - state;
		state += ((d * kQ15) >> 15) + (d > 0 ? 1 : 0);
	}

	// Small, hot state first (short offsets from `this`), the buffer last.
	int32_t w_ = 0;
	int32_t phase_ = 0;
	int32_t lastIn_ = 0;
	int32_t diffused_ = 0; // carried from the even to the odd sample
	int32_t dampL_ = 0, dampR_ = 0;
	int32_t curL_ = 0, curR_ = 0, prevL_ = 0, prevR_ = 0;
	int16_t buf_[kSize] = {};
};

} // namespace tem
