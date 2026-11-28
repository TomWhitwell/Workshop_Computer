#pragma once
#include <cmath>
#include <cstdint>

namespace ab
{

// Lookup tables, filled in once at power-on by Tables::Init(), before the
// audio starts. Init uses the chip's (slow, software) floating point, which
// is fine for a one-off job; everything that runs while the card plays uses
// these tables and whole-number maths instead.
struct Tables
{
	// One cycle of a sine wave, +/-32767, 2048 points, plus a copy of the
	// first point at the end so interpolation never has to wrap. Shared by
	// the piano's partials, the choir's wavetable recipe, the LFOs and the
	// pan law.
	static constexpr int kSineBits = 11;
	static constexpr int kSineSize = 1 << kSineBits;
	static inline int16_t sine[kSineSize + 1];

	// How far a 32-bit phase moves per sample for each MIDI note (A4 = 440Hz),
	// at 48kHz.
	static inline uint32_t noteInc[128];

	// 2^(i/32) for i = 0..32, Q16: for log-uniform random tempos.
	static inline int32_t exp2[33];

	// The pan curve, 0.9 x (i/64)^1.15, Q15, i = 0..64 (see Scheduler.h).
	static inline int32_t panCurve[65];

	static void Init()
	{
		const float kTwoPi = 6.2831853f;
		for (int i = 0; i <= kSineSize; i++)
			sine[i] = (int16_t)lrintf(32767.0f * sinf(kTwoPi * (float)i / (float)kSineSize));

		for (int n = 0; n < 128; n++)
		{
			float hz = 440.0f * exp2f((float)(n - 69) / 12.0f);
			noteInc[n] = (uint32_t)(hz * (4294967296.0f / 48000.0f));
		}
		for (int i = 0; i <= 32; i++)
			exp2[i] = (int32_t)lrintf(65536.0f * exp2f((float)i / 32.0f));
		for (int i = 0; i <= 64; i++)
			panCurve[i] = (int32_t)lrintf(0.9f * 32767.0f * powf((float)i / 64.0f, 1.15f));
	}
};

// Sine of a 32-bit phase (a whole cycle is 2^32), +/-32767. The top 11 bits
// pick a table entry; the next 16 slide linearly to the one after it.
// Always inlined: it's called dozens of times a sample, and a function call
// costs nearly as much as the lookup itself.
__attribute__((always_inline)) inline int32_t Sine(uint32_t phase)
{
	uint32_t i = phase >> (32 - Tables::kSineBits);
	int32_t frac = (int32_t)((phase >> (16 - Tables::kSineBits)) & 0xFFFF);
	int32_t a = Tables::sine[i];
	int32_t b = Tables::sine[i + 1];
	return a + (((b - a) * frac) >> 16);
}

// 2^(x/4096), Q16, for x = 0..4096 (so Exp2(0) = 65536, Exp2(4096) = 131072).
inline int32_t Exp2(int32_t x)
{
	int32_t i = x >> 7, f = x & 127;
	if (i >= 32)
		return Tables::exp2[32];
	return Tables::exp2[i] + (((Tables::exp2[i + 1] - Tables::exp2[i]) * f) >> 7);
}

// Equal-power pan. pan is -32768 (hard left) .. 0 (centre) .. 32767 (hard
// right); gains are Q15. At the centre both sides get 0.707, so a loop keeps
// the same loudness wherever it sits.
inline void PanGains(int32_t pan, int32_t &left, int32_t &right)
{
	// Angle 0..quarter cycle as a 32-bit phase: (pan + 32768) / 65536 x 2^30.
	uint32_t angle = (uint32_t)(pan + 32768) << 14;
	right = Sine(angle);
	left = Sine(angle + 0x40000000u); // cosine
	if (left < 0)
		left = 0;
	if (right < 0)
		right = 0;
}

} // namespace ab
