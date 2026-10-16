#pragma once
#include <cstdint>

namespace constancy
{

// Small building blocks shared by the audio code (core 0). Audio inside the
// card runs at +/-32768 for full scale.

// One-pole lowpass: move a fraction (coef, Q15) of the way towards the
// input each sample. Inputs within +/-65535.
//
// The step is rounded away from zero, so the filter always moves at least
// one unit and settles exactly on its input. With the usual round-down it
// gets stuck a unit or two below -- and in front of a feedback loop that
// small leftover is fed in forever, so the echoes never reach silence.
// (From Equanimity.)
__attribute__((always_inline)) inline int32_t OnePole(int32_t &state, int32_t in, int32_t coefQ15)
{
	int32_t d = in - state;
	state += ((d * coefQ15) >> 15) + (d > 0 ? 1 : 0);
	return state;
}

// x * gain (Q12), rounded towards zero. In a feedback loop the usual
// shift-right rounds towards minus infinity, which can keep a single -1
// going round the loop forever; rounding towards zero lets every echo die
// all the way to silence. (From Equanimity.)
__attribute__((always_inline)) inline int32_t ScaleTowardZero(int32_t x, int32_t gainQ12)
{
	int32_t y = x * gainQ12;
	return y >= 0 ? y >> 12 : -((-y) >> 12);
}

// Hold a value inside a 16-bit sample.
__attribute__((always_inline)) inline int32_t Clamp16(int32_t x)
{
	return x > 32767 ? 32767 : (x < -32768 ? -32768 : x);
}

// Output limiter for the DAC's +/-2047: unchanged up to 3/4 of full scale,
// then squashed 4:1, then a hard stop. (From Impermanence.)
inline int32_t SoftClip(int32_t x)
{
	int32_t a = x < 0 ? -x : x;
	if (a > 1536)
	{
		a = 1536 + ((a - 1536) >> 2);
		if (a > 2047)
			a = 2047;
	}
	return x < 0 ? -a : a;
}

// DC blocker: takes away anything slower than about 48000 / (2 pi 2^kShift)
// Hz -- 30Hz for kShift = 8, like SuperCollider's LeakDC in Icarus's loop.
// A running average, its step rounded away from zero so it settles
// exactly: a leftover unit here would go round the loop for ever.
template <int kShift>
struct DcBlocker
{
	int32_t average = 0; // x 2^kShift

	__attribute__((always_inline)) int32_t Process(int32_t x)
	{
		int32_t d = x * (1 << kShift) - average;
		average += (d >> kShift) + (d > 0 ? 1 : 0);
		return x - (average >> kShift);
	}
};

// Gains that core 1 sets once a millisecond and core 0 slides to in
// straight lines, so envelopes and level changes move smoothly instead of
// in 1ms steps (which would buzz at 1kHz). To save time they move every
// other sample: 24 steps to each new value, landing exactly on it.
template <int N>
struct Ramps
{
	int32_t value[N] = {}, step[N] = {}, target[N] = {};
	int32_t left = 0;

	// A new target for ramp i. Values within +/-2^20.
	void Retarget(int i, int32_t t)
	{
		target[i] = t;
		step[i] = ((t - value[i]) * 2731) >> 16; // / 24
		left = 24;
	}

	// Every other sample.
	__attribute__((always_inline)) void Step()
	{
		if (left == 0)
			return;
		if (--left == 0)
		{
			for (int i = 0; i < N; i++)
				value[i] = target[i];
			return;
		}
		for (int i = 0; i < N; i++)
			value[i] += step[i];
	}
};

} // namespace constancy
