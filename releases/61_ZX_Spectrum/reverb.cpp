// reverb.cpp — fixed-point Freeverb-style reverb (see reverb.h).

#include "reverb.h"
#include <cstring>

namespace zx {

// out-of-line constexpr array definitions (C++14)
constexpr int Reverb::kCombLen[];
constexpr int Reverb::kAllpassLen[];

// Fixed-point coefficients (Q15). Freeverb defaults: room ~0.84, damp ~0.2.
static constexpr int32_t kFeedback = 27525; // ~0.84 * 32768 (comb feedback)
static constexpr int32_t kDamp1    = 6554;  // ~0.20 * 32768
static constexpr int32_t kDamp2    = 32768 - kDamp1;
static constexpr int32_t kApFeed   = 16384; // 0.5 allpass feedback

void Reverb::Reset()
{
	for (int c = 0; c < kNumCombs; c++)
	{
		memset(comb_[c], 0, sizeof(comb_[c]));
		combStore_[c] = 0;
		combIdx_[c] = 0;
	}
	for (int a = 0; a < kNumAllpass; a++)
	{
		memset(allpass_[a], 0, sizeof(allpass_[a]));
		apIdx_[a] = 0;
	}
}

int16_t Reverb::Process(int16_t in)
{
	// Gain staging: the input is ~12-bit (±2048). We keep the delay-line samples
	// as int16 too, but the KEY fix is that the comb feedback gain (<1) plus the
	// per-comb input means each comb settles at roughly input/(1-feedback). With
	// feedback 0.84 that's ~6x the input — so we must (a) attenuate the input into
	// the combs, and (b) NEVER let internal values approach the int16 clip point,
	// or the clipping itself becomes the grit. We scale the input DOWN by 1/4 on
	// the way in and back UP by the same on the way out, giving ~4x headroom.

	int32_t input = in >> 2;   // attenuate into the reverb network (headroom)
	int32_t out = 0;

	// 8 parallel comb filters, each with a damping lowpass in the feedback path.
	for (int c = 0; c < kNumCombs; c++)
	{
		int len = kCombLen[c];
		int32_t y = comb_[c][combIdx_[c]];
		out += y;
		// Damping lowpass on the feedback: store = y*damp2 + store*damp1 (Q15).
		combStore_[c] = (y * kDamp2 + combStore_[c] * kDamp1) >> 15;
		int32_t v = input + ((combStore_[c] * kFeedback) >> 15);
		if (v > 8191) v = 8191; else if (v < -8192) v = -8192;
		comb_[c][combIdx_[c]] = int16_t(v);
		if (++combIdx_[c] >= len) combIdx_[c] = 0;
	}
	out >>= 3;   // average the 8 combs

	// 4 series allpass filters (more diffusion -> less metallic).
	//   out = buf - in;  buf = in + buf*g
	for (int a = 0; a < kNumAllpass; a++)
	{
		int len = kAllpassLen[a];
		int32_t buf = allpass_[a][apIdx_[a]];
		int32_t y = buf - out;
		int32_t v = out + ((buf * kApFeed) >> 15);
		if (v > 8191) v = 8191; else if (v < -8192) v = -8192;
		allpass_[a][apIdx_[a]] = int16_t(v);
		if (++apIdx_[a] >= len) apIdx_[a] = 0;
		out = y;
	}

	// Output level: halve again so even a loud sustained source can't push the
	// wet signal into the ±2047 clip (that clip was the "grit above 50%"). Net
	// reverb gain ≈ ÷8 vs input — plenty audible, always clean.
	out >>= 1;
	if (out > 2047) out = 2047; else if (out < -2048) out = -2048;
	return int16_t(out);
}

} // namespace zx
