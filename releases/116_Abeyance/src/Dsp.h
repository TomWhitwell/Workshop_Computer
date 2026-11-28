#pragma once
#include <cstdint>

namespace ab
{

// Small building blocks shared by the audio code. Inside the card, audio is
// carried at 16-bit scale (+/-32767); the jacks are 12-bit (+/-2047), so the
// last step divides by 16.

inline int32_t Clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Keep a value inside int16, for writing into delay lines.
inline int32_t Sat16(int32_t x) { return x > 32767 ? 32767 : (x < -32768 ? -32768 : x); }

// x * gain (Q12), rounded towards zero. In a feedback loop the usual
// shift-right rounds towards minus infinity, which can keep a single -1
// going round the loop for ever; rounding towards zero lets every echo die
// all the way to silence.
inline int32_t ScaleTowardZero(int32_t x, int32_t gainQ12)
{
	int32_t y = x * gainQ12;
	return y >= 0 ? y >> 12 : -((-y) >> 12);
}

// Output limiter for the DAC's +/-2047, in place of a compressor: unchanged
// up to 3/4 of full scale, then squashed 4:1, then a hard stop. Cheaper than
// a compressor, and it keeps the peaks inside the 12-bit outputs.
// (From Impermanence.)
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
// input each step. Inputs within +/-32767.
//
// The step is rounded away from zero, so the filter always moves at least
// one unit and settles exactly on its input. With the usual round-down it
// gets stuck a unit or two below -- and in front of a feedback loop that
// small leftover is fed in for ever, so the echoes never reach silence.
// (From Equanimity.)
inline int32_t OnePole(int32_t &state, int32_t in, int32_t coefQ15)
{
	int32_t d = in - state;
	state += ((d * coefQ15) >> 15) + (d > 0 ? 1 : 0);
	return state;
}

} // namespace ab
