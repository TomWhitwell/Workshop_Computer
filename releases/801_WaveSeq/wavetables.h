// Wavetable bank for Wave Sequencer
//
// 64 single-cycle waves, in eight families of eight, built at power-up.
// Each wave is defined in the time domain, analysed with a DFT, then
// resynthesised at three bandwidths (mip levels) so that higher notes can
// use a version with fewer harmonics, keeping aliasing down.
//
// Neighbouring waves are designed to morph smoothly, since a step's wave
// position is continuous and the oscillator interpolates between adjacent
// waves.

#ifndef WAVETABLES_H
#define WAVETABLES_H

#include <stdint.h>
#include <math.h>

static constexpr int kTableBits = 8;
static constexpr int kTableSize = 1 << kTableBits;
static constexpr int kNumWaves = 64;
static constexpr int kNumMips = 3;

// Harmonics kept at each mip level, and the highest fundamental (as a
// phase increment) that each level stays alias-free for at 48kHz.
static constexpr int kMipHarmonics[kNumMips] = {64, 24, 12};
static constexpr uint32_t kMipMaxInc[kNumMips - 1] = {
	uint32_t(375.0 * 4294967296.0 / 48000.0),  // 64 harmonics up to 375Hz
	uint32_t(1000.0 * 4294967296.0 / 48000.0), // 24 harmonics up to 1kHz; 12 above
};

// One guard sample per table, so interpolation never needs to wrap
static int16_t gWaves[kNumMips][kNumWaves][kTableSize + 1];

namespace wavegen
{

static constexpr float kTwoPi = 6.28318530718f;

static float Fold(float y)
{
	y = (y + 1.0f) * 0.25f;
	y -= floorf(y);
	return 1.0f - 4.0f * fabsf(y - 0.5f);
}

static uint32_t rngState = 12345;
static float Rand()
{
	rngState = rngState * 1664525u + 1013904223u;
	return float(rngState >> 8) * (1.0f / 16777216.0f);
}

// Formants as harmonic numbers (as if the fundamental were ~110Hz)
static const float kVowels[8][2] = {
	{7.0f, 11.0f}, {6.0f, 16.0f}, {3.0f, 21.0f}, {5.0f, 9.0f},
	{3.0f, 7.0f},  {4.0f, 13.0f}, {8.0f, 12.0f}, {2.5f, 24.0f},
};

// Fill f[0..255] with one cycle of wave w (0-63)
static void Define(int w, float *f)
{
	int fam = w >> 3, j = w & 7;
	float t = float(j) / 7.0f;
	float amp[48], phs[48];

	for (int n = 0; n < kTableSize; n++)
	{
		float x = float(n) / float(kTableSize);
		float v = 0.0f;
		switch (fam)
		{
		case 0: // Additive build, sine to saw
		{
			static const int N[8] = {1, 2, 3, 4, 6, 10, 20, 64};
			for (int h = 1; h <= N[j]; h++) v += sinf(kTwoPi * float(h) * x) / float(h);
			break;
		}
		case 1: // Saw to square
		{
			float saw = 1.0f - 2.0f * x;
			float sq = x < 0.5f ? 1.0f : -1.0f;
			v = (1.0f - t) * saw + t * sq;
			break;
		}
		case 2: // Pulse width, 50% down to 4%
			v = x < (0.5f - 0.46f * t) ? 1.0f : -1.0f;
			break;
		case 3: // Hard-sync saw, slave ratio 1 to 4.5
		{
			float r = 1.0f + 0.5f * float(j);
			float y = x * r;
			v = 1.0f - 2.0f * (y - floorf(y));
			break;
		}
		case 4: // FM: ratio 1 then ratio 3, rising index
		{
			static const float idx[8] = {0.0f, 1.0f, 2.0f, 3.5f, 1.0f, 2.0f, 3.0f, 5.0f};
			float ratio = j < 4 ? 1.0f : 3.0f;
			v = sinf(kTwoPi * x + idx[j] * sinf(kTwoPi * ratio * x));
			break;
		}
		case 5: // Vowel formants
		{
			if (n == 0)
			{
				for (int h = 1; h <= 40; h++)
				{
					float d1 = (float(h) - kVowels[j][0]) / 1.5f;
					float d2 = (float(h) - kVowels[j][1]) / 2.5f;
					amp[h] = expf(-d1 * d1) + 0.6f * expf(-d2 * d2) + 0.3f / float(h);
				}
			}
			for (int h = 1; h <= 40; h++) v += amp[h] * sinf(kTwoPi * float(h) * x);
			break;
		}
		case 6: // Wavefolded sine, gain 1 to 6
			v = Fold((1.0f + 5.0f * t) * sinf(kTwoPi * x));
			break;
		default: // Digital: stepped sines, then random spectra
			if (j < 4)
			{
				static const float q[4] = {8.0f, 4.0f, 2.0f, 1.0f};
				v = roundf(sinf(kTwoPi * x) * q[j]) / q[j];
			}
			else
			{
				if (n == 0)
				{
					rngState = 777u + uint32_t(j) * 101u;
					for (int h = 1; h <= 24; h++)
					{
						float r = Rand();
						amp[h] = r * r / sqrtf(float(h));
						phs[h] = Rand();
					}
				}
				for (int h = 1; h <= 24; h++) v += amp[h] * sinf(kTwoPi * (float(h) * x + phs[h]));
			}
			break;
		}
		f[n] = v;
	}
}

// Build all tables.  Takes a fraction of a second at power-up.
static void Build()
{
	static float f[kTableSize];
	static int32_t s[kTableSize];
	static int32_t cosTab[kTableSize], sinTab[kTableSize];
	static int32_t re[65], im[65];
	static int32_t sigma[kNumMips][65];
	static int32_t acc[kTableSize];

	for (int n = 0; n < kTableSize; n++)
	{
		cosTab[n] = int32_t(32767.0f * cosf(kTwoPi * float(n) / float(kTableSize)));
		sinTab[n] = int32_t(32767.0f * sinf(kTwoPi * float(n) / float(kTableSize)));
	}

	// Lanczos sigma factors soften the truncation, reducing Gibbs ringing
	for (int m = 0; m < kNumMips; m++)
	{
		int H = kMipHarmonics[m];
		for (int h = 1; h <= H; h++)
		{
			float a = 3.14159265f * float(h) / float(H + 1);
			sigma[m][h] = int32_t(32767.0f * sinf(a) / a);
		}
	}

	for (int w = 0; w < kNumWaves; w++)
	{
		Define(w, f);

		float peak = 1e-6f;
		for (int n = 0; n < kTableSize; n++) peak = fmaxf(peak, fabsf(f[n]));
		for (int n = 0; n < kTableSize; n++) s[n] = int32_t(16384.0f * f[n] / peak);

		// DFT, harmonics 1-64 (DC is dropped)
		for (int h = 1; h <= 64; h++)
		{
			int32_t r = 0, i = 0;
			for (int n = 0; n < kTableSize; n++)
			{
				int k = (h * n) & (kTableSize - 1);
				r += (s[n] * cosTab[k]) >> 8;
				i += (s[n] * sinTab[k]) >> 8;
			}
			re[h] = r >> 14;
			im[h] = i >> 14;
		}

		float scale = 1.0f;
		for (int m = 0; m < kNumMips; m++)
		{
			for (int n = 0; n < kTableSize; n++) acc[n] = 0;
			for (int h = 1; h <= kMipHarmonics[m]; h++)
			{
				int32_t a = (re[h] * sigma[m][h]) >> 15;
				int32_t b = (im[h] * sigma[m][h]) >> 15;
				for (int n = 0; n < kTableSize; n++)
				{
					int k = (h * n) & (kTableSize - 1);
					acc[n] += (a * cosTab[k] + b * sinTab[k]) >> 8;
				}
			}

			// Normalise every mip by the full-bandwidth peak, so a wave
			// keeps the same level as notes cross mip boundaries
			if (m == 0)
			{
				int32_t pk = 1;
				for (int n = 0; n < kTableSize; n++)
				{
					int32_t v = acc[n] < 0 ? -acc[n] : acc[n];
					if (v > pk) pk = v;
				}
				scale = 30000.0f / float(pk);
			}
			for (int n = 0; n < kTableSize; n++)
			{
				float v = float(acc[n]) * scale;
				if (v > 32767.0f) v = 32767.0f;
				if (v < -32767.0f) v = -32767.0f;
				gWaves[m][w][n] = int16_t(v);
			}
			gWaves[m][w][kTableSize] = gWaves[m][w][0];
		}
	}
}

} // namespace wavegen

#endif
