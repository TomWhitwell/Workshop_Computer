#pragma once
#include <cstdint>

namespace eq
{

// Small building blocks shared by the audio code.

// Soft saturator for the delay loops, in the delays' +/-32767 range.
// Below half scale it does nothing at all. Above that it bends smoothly
// over (a parabola, so there's no corner at the knee) and flattens out at
// full scale, which it reaches for an input 1.5x full scale. Louder than
// that is simply held at full scale.
inline int32_t Saturate(int32_t x)
{
	constexpr int32_t kKnee = 16384;
	int32_t a = x < 0 ? -x : x;
	if (a <= kKnee)
		return x;
	int32_t d = a - kKnee; // how far past the knee
	if (d >= 32767)
		a = 32767;
	else
	{
		a = kKnee + d - ((d * d) >> 16); // slope 1 at the knee, 0 at the top
		if (a > 32767)
			a = 32767;
	}
	return x < 0 ? -a : a;
}

// x * gain (Q12), rounded towards zero. In a feedback loop the usual
// shift-right rounds towards minus infinity, which can keep a single -1
// going round the loop forever; rounding towards zero lets every echo die
// all the way to silence.
inline int32_t ScaleTowardZero(int32_t x, int32_t gainQ12)
{
	int32_t y = x * gainQ12;
	return y >= 0 ? y >> 12 : -((-y) >> 12);
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

// One-pole lowpass: move a fraction (coef, Q15) of the way towards the
// input each sample. Inputs within +/-32767.
//
// The step is rounded away from zero, so the filter always moves at least
// one unit and settles exactly on its input. With the usual round-down it
// gets stuck a unit or two below -- and in front of a feedback loop that
// small leftover is fed in forever, so the echoes never reach silence.
inline int32_t OnePole(int32_t &state, int32_t in, int32_t coefQ15)
{
	int32_t d = in - state;
	state += ((d * coefQ15) >> 15) + (d > 0 ? 1 : 0);
	return state;
}

// DC blocker: takes away anything slower than about 2Hz. The delays' DC
// gain at full feedback is ~33x, so even a small offset going in -- a few
// counts from an audio input, or rounding in the voices -- would build up
// in the loops and eat their headroom. The melody has nothing below C4
// (262Hz) and the drone nothing below C2 (65Hz), so nothing audible is lost.
struct DcBlocker
{
	int32_t average = 0; // Q12

	int32_t Process(int32_t x)
	{
		// A ~4096-sample (85ms) running average, its step rounded away from
		// zero (like OnePole) so it settles exactly: a leftover unit here
		// would go round the delay loops for ever.
		int32_t d = x * 4096 - average;
		average += (d >> 12) + (d > 0 ? 1 : 0);
		return x - (average >> 12);
	}
};

} // namespace eq
