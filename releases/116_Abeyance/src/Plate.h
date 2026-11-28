#pragma once
#include <cstdint>
#include "Dsp.h"

namespace ab
{

// The long stereo reverb: Jon Dattorro's figure-of-eight plate ("Effect
// Design, Part 1", JAES 1997), in whole numbers at a fixed 24kHz. Adapted
// from Temperance's Plate, made bigger: the tank's lines are 2.3 times as
// long (a bigger room, with a longer, slower wash) and the input diffusers
// 1.5 times.
//
//   in -> 4 allpass diffusers --+--> L: mod allpass -> delay -> damp -> x decay -> allpass -> delay --+
//                               |                                                                    |
//                               +--> R: mod allpass -> delay -> damp -> x decay -> allpass -> delay --+
//                                    ^ (each side is fed by the other side's end, x decay)
//
// The diffusers smear the input into a wash. The tank is two halves fed
// round into each other, each half darker (the damping low-pass) and
// quieter (the decay gain) every time round. The first allpass in each
// half is swung by a slow LFO (from core 1): the tank's echoes drift
// against each other, which is most of the plate's washiness and stops it
// ringing at fixed pitches. Stereo comes from Dattorro's output taps: each
// side is a sum of seven points inside the tank, mostly from the other half.
//
// Timing: this runs inside the audio interrupt, at half the sample rate,
// split so every sample costs about the same:
//
//   even samples  average the last two inputs (an anti-alias filter for the
//                 drop to 24kHz), the diffusers, the left half of the tank
//   odd samples   the right half, and both outputs
//
// All the lines share one buffer with one write position, so each access
// is (position + constant), wrapped. 41,195 int16s: 82KB.
//
// Safety: every write into the tank is saturated, every multiply rounds
// towards zero, and anything below 2 units is flushed to zero, so the tail
// always dies away to true silence (Impermanence measured a single unit
// circulating for ever without that; so did this card's tests, until the
// multiplies rounded towards zero).
class Plate
{
public:
	// `in`: mono, at the lines' scale (16-bit audio / 4). `lfoQ8`: the two
	// allpass swings, 1/256 samples. Writes the wet signal at 16-bit scale.
	__attribute__((noinline)) void Process(int32_t in, const int32_t *lfoQ8, int32_t &outL, int32_t &outR)
	{
		if (!phase_)
		{
			// A new frame: the write position moves on one.
			w_ = w_ == 0 ? kSize - 1 : w_ - 1;

			int32_t x = (in + lastIn_) >> 1;
			x = Allpass<kIn1, kIn1Len>(x, kInDiffuse1);
			x = Allpass<kIn2, kIn2Len>(x, kInDiffuse1);
			x = Allpass<kIn3, kIn3Len>(x, kInDiffuse2);
			x = Allpass<kIn4, kIn4Len>(x, kInDiffuse2);
			diffused_ = x;

			// Left half, fed by the right half's end.
			int32_t t = x + ScaleQ15(Read<kRd2>(kRd2Len), decay_);
			t = ModAllpass<kLap1, kLap1Len>(t, lfoQ8[0]);
			WriteLoop<kLd1>(t);
			Damp(dampL_, Read<kLd1>(kLd1Len), damp_);
			t = Allpass<kLap2, kLap2Len>(ScaleQ15(dampL_, decay_), kDecayDiffuse2);
			WriteLoop<kLd2>(t);

			// Output: halfway between the last two frames.
			outL = (prevL_ + curL_) >> 1;
			outR = (prevR_ + curR_) >> 1;
		}
		else
		{
			// Right half, fed by the left half's end.
			int32_t t = diffused_ + ScaleQ15(Read<kLd2>(kLd2Len), decay_);
			t = ModAllpass<kRap1, kRap1Len>(t, lfoQ8[1]);
			WriteLoop<kRd1>(t);
			Damp(dampR_, Read<kRd1>(kRd1Len), damp_);
			t = Allpass<kRap2, kRap2Len>(ScaleQ15(dampR_, decay_), kDecayDiffuse2);
			WriteLoop<kRd2>(t);

			// Dattorro's output taps (his positions, x 2.3 with the tank).
			int32_t l = Read<kRd1>(495) + Read<kRd1>(5515) - Read<kRap2>(3549) + Read<kRd2>(3703) -
						Read<kLd1>(3692) - Read<kLap2>(347) - Read<kLd2>(1978);
			int32_t r = Read<kLd1>(656) + Read<kLd1>(6728) - Read<kLap2>(2277) + Read<kLd2>(4959) -
						Read<kRd1>(3915) - Read<kRap2>(621) - Read<kRd2>(225);

			// The output runs one frame behind, so it can step through the
			// frames in order: the previous frame now, the midpoint on the
			// next (even) sample -- straight-line interpolation from 24kHz
			// back up to 48kHz. Back to 16-bit scale (x 4) at kOutQ10 of
			// the tap sum.
			prevL_ = curL_;
			prevR_ = curR_;
			curL_ = (l * out_) >> 8; // x out / 1024 x 4
			curR_ = (r * out_) >> 8;
			outL = prevL_;
			outR = prevR_;
		}
		lastIn_ = in;
		phase_ ^= 1;
	}

	// The reverb's shape, set once per block from core 1 (the Main knob's
	// macro): the decay gain and damping coefficient (Q15), and the output
	// level (of 1024). See kDecayQ15 and friends for the usual values.
	void Shape(int32_t decayQ15, int32_t dampQ15, int32_t outQ10)
	{
		decay_ = decayQ15;
		damp_ = dampQ15;
		out_ = outQ10;
	}

	// Empty the reverb a slice at a time, as Echo::ClearSome does.
	static constexpr int32_t kClearPerCall = 64;
	void ClearSome()
	{
		if (clear_ == 0)
			phase_ = lastIn_ = diffused_ = dampL_ = dampR_ = curL_ = curR_ = prevL_ = prevR_ = 0;
		for (int32_t i = 0; i < kClearPerCall; i++)
			buf_[clear_ + i] = 0;
		clear_ += kClearPerCall;
		if (clear_ >= kSize)
			clear_ = 0;
	}

	// The LFO swing, in samples, either way (for core 1's LFO).
	static constexpr int32_t kExcursion = 16;

private:
	// Line lengths at 24kHz: Temperance's (Dattorro's, scaled to 24kHz),
	// x 1.5 for the diffusers and x 2.3 for the tank.
	static constexpr int32_t kIn1Len = 173, kIn2Len = 129, kIn3Len = 459, kIn4Len = 335;
	static constexpr int32_t kLap1Len = 1247, kLd1Len = 8259, kLap2Len = 3340, kLd2Len = 6900;
	static constexpr int32_t kRap1Len = 1684, kRd1Len = 7822, kRap2Len = 4927, kRd2Len = 5867;

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
	static_assert(kSize * 2 <= 86 * 1024, "the guide's budget: about 86KB of reverb lines");

public:
	static constexpr int32_t kClearCalls = (kSize + 63) / 64; // see ClearSome()

private:

public:
	// The usual shape. A lap of the figure-of-eight is about 1.7s and
	// passes the decay gain four times (twice in each half, as in Dattorro's
	// design). At 0.72 the tail takes about 8.4 seconds to fall 60dB at
	// 300Hz, less in the treble thanks to the damping (measured in
	// test/host_tests.cpp).
	static constexpr int32_t kDecayQ15 = 23593; // 0.72
	// The damping low-pass's coefficient: about 3.5kHz at 24kHz.
	static constexpr int32_t kDampQ15 = 19661; // 0.60
	// Output level, of 1024: keeps a full wash near the dry level.
	static constexpr int32_t kOutQ10 = 180;

private:

	// Dattorro's diffusion coefficients, Q15.
	static constexpr int32_t kInDiffuse1 = 24576;	  // 0.75
	static constexpr int32_t kInDiffuse2 = 20480;	  // 0.625
	static constexpr int32_t kDecayDiffuse1 = -22938; // -0.70: the modulated allpasses
	// Dattorro sets this to decay + 0.15, kept within 0.25..0.5: 0.5 here.
	static constexpr int32_t kDecayDiffuse2 = 16384;

	// x * g (Q15), rounded towards zero. Inside the tank the usual
	// shift-right (which rounds towards minus infinity) holds a -1 going
	// round for ever; rounding towards zero lets the tail reach silence.
	__attribute__((always_inline)) static int32_t ScaleQ15(int32_t x, int32_t g)
	{
		int32_t y = x * g;
		return y >= 0 ? y >> 15 : -((-y) >> 15);
	}

	// The line accessors are forced inline: called out of line, each one
	// costs a call and return (measured in Temperance's bench).
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
		int32_t v = Sat16(in - ScaleQ15(d, gQ15));
		Write<base>(v);
		return d + ScaleQ15(v, gQ15);
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
		int32_t v = Sat16(in - ScaleQ15(d, kDecayDiffuse1));
		WriteLoop<base>(v);
		return d + ScaleQ15(v, kDecayDiffuse1);
	}

	// One-pole low-pass, its step rounded away from zero, so it always
	// settles exactly on its input.
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
	int32_t decay_ = kDecayQ15, damp_ = kDampQ15, out_ = kOutQ10;
	int32_t curL_ = 0, curR_ = 0, prevL_ = 0, prevR_ = 0;
	int32_t clear_ = 0;
	int16_t buf_[kSize + kClearPerCall] = {}; // room for a clear's last slice to run past the end
};

} // namespace ab
