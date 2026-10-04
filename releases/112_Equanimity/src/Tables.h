#pragma once
#include <cmath>
#include <cstdint>

namespace eq
{

// Lookup tables, filled in once at power-on by Tables::Init(), before the
// audio starts. Init uses the chip's (slow, software) floating point, which
// is fine for a one-off job; everything that runs while the card plays uses
// these tables and whole-number maths instead.
struct Tables
{
	// One cycle of a sine wave, +/-32767, plus a copy of the first point at
	// the end so interpolation never has to wrap.
	static constexpr int kSineBits = 10;
	static constexpr int kSineSize = 1 << kSineBits;
	static inline int16_t sine[kSineSize + 1];

	// How far a 32-bit phase moves per sample for each MIDI note (A4 = 440Hz).
	static inline uint32_t noteInc[128];

	// Mu-law companding for the long delay line: 8 bits per sample, with
	// fine steps for quiet sounds and coarse steps for loud ones (the same
	// trick telephones use). Code = sign bit + 7-bit magnitude.
	static inline int16_t muDecode[256];
	static inline uint8_t muEncode[2048]; // indexed by |sample| >> 4

	// 2^(i/32) for i = 0..32, Q16: for rates that rise in octaves.
	static inline int32_t exp2[33];

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

		// Mu-law with mu = 255 over a full scale of 32768.
		const float kMu = 255.0f;
		for (int c = 0; c < 128; c++)
		{
			float mag = 32768.0f * (powf(1.0f + kMu, (float)c / 127.0f) - 1.0f) / kMu;
			if (mag > 32767.0f)
				mag = 32767.0f;
			muDecode[c] = (int16_t)lrintf(mag);
			muDecode[c + 128] = (int16_t)-lrintf(mag);
		}
		for (int i = 0; i < 2048; i++)
		{
			float mag = (float)(i * 16 + 8); // middle of this table entry's range
			int c = (int)lrintf(127.0f * logf(1.0f + kMu * mag / 32768.0f) / logf(1.0f + kMu));
			muEncode[i] = (uint8_t)(c > 127 ? 127 : c);
		}
		muEncode[0] = 0; // true silence stays silent

		for (int i = 0; i <= 32; i++)
			exp2[i] = (int32_t)lrintf(65536.0f * exp2f((float)i / 32.0f));
	}
};

// Sine of a 32-bit phase (a whole cycle is 2^32), +/-32767. The top 10 bits
// pick a table entry; the next 16 slide linearly to the one after it.
// Always inlined: it's called a dozen times a sample, and a function call
// costs nearly as much as the lookup itself.
__attribute__((always_inline)) inline int32_t Sine(uint32_t phase)
{
	uint32_t i = phase >> (32 - Tables::kSineBits);
	int32_t frac = (int32_t)((phase >> 6) & 0xFFFF);
	int32_t a = Tables::sine[i];
	int32_t b = Tables::sine[i + 1];
	return a + (((b - a) * frac) >> 16);
}

// 2^(x/4096), Q16 (so Exp2(0) = 65536 and Exp2(4096) = 131072). x may be
// negative, down to a few octaves.
inline int32_t Exp2(int32_t x)
{
	int32_t octave = x >> 12; // rounds down, so the fraction is always positive
	int32_t frac = x & 4095;
	int32_t i = frac >> 7;
	int32_t f = frac & 127;
	int32_t v = Tables::exp2[i] + (((Tables::exp2[i + 1] - Tables::exp2[i]) * f) >> 7);
	return octave >= 0 ? v << octave : v >> -octave;
}

} // namespace eq
