#pragma once
#include <cstdint>
#include "Config.h"
#include "Dsp.h"
#include "Params.h"
#include "Tables.h"

namespace eq
{

// The two delay lines, on core 0.
//
//   Short  1.1s at 24kHz, 16-bit samples          53KB
//   Long   9.7s at  8kHz, 8-bit mu-law samples    78KB
//
// Storing fewer, smaller samples is what fits 10.8 seconds of echo into the
// RP2040's 264KB. The cost is bandwidth: the short line keeps everything up
// to ~10kHz, the long line only ~3.5kHz, and mu-law adds a little grain.
// Both suit distant repeats. The 1.1 : 9.7 ratio isn't a whole number, so
// the two lines' repeats drift past each other rather than stacking up.
//
// Each line is a circle of samples: write at one point, read back from a
// point further behind it. Two read points per line -- one for each
// output -- each drift by their own tape wobble, so left and right drift
// slightly apart. Reads land between stored samples, so they blend the two
// either side (linear interpolation).
//
// Inside each loop, the echo goes through a one-pole lowpass and a soft
// saturator before it is written back, so every repeat is a touch softer
// and duller, and with feedback near the top the loop settles instead of
// climbing.
class Delays
{
public:
	// One sample. `in` is the delay input (+/-32767); `p` supplies feedback
	// and read positions. Returns the wet signal for each output.
	__attribute__((noinline)) void Process(int32_t in, const EngineParams &p, int32_t &wetL, int32_t &wetR)
	{
		// Read positions glide towards the ones core 1 asks for (which only
		// change once a millisecond), so the wobble moves smoothly.
		for (int i = 0; i < 2; i++)
		{
			shortDelay_[i] += (p.shortDelay[i] - shortDelay_[i]) >> 5;
			longDelay_[i] += (p.longDelay[i] - longDelay_[i]) >> 5;
		}

		// ---- Short line -------------------------------------------------
		// Lowpass at ~10kHz, then keep every other sample (24kHz), averaging
		// each pair. The line's clock runs at half the output rate, so on
		// odd samples "now" is half a stored sample further on.
		int32_t shortIn = OnePole(shortInLp_, in, kShortInLpQ15);
		int32_t now = (shortWrite_ << 12) + (half_ ? 2048 : 0);
		int32_t sL = ReadShort(now - shortDelay_[0]);
		int32_t sR = ReadShort(now - shortDelay_[1]);
		if (half_)
		{
			// Feedback from the left read point (the right one is a second
			// view of the same echoes, wobbling on its own).
			int32_t fb = OnePole(shortLoopLp_, sL, kShortLoopLpQ15);
			int32_t w = Saturate(((shortIn + shortInPrev_) >> 1) + ScaleTowardZero(fb, p.shortFeedback));
			short_[shortWrite_] = (int16_t)w;
			if (shortWrite_ == 0)
				short_[kShortSize] = (int16_t)w; // the copy past the end, for interpolation
			if (++shortWrite_ == kShortSize)
				shortWrite_ = 0;
		}
		shortInPrev_ = shortIn;
		half_ = !half_;

		// ---- Long line --------------------------------------------------
		// Lowpass at ~3.5kHz (twice, for a steeper slope), then keep one
		// sample in six (8kHz). Anything above 4kHz would alias.
		int32_t longIn = OnePole(longInLp2_, OnePole(longInLp1_, in, kLongInLpQ15), kLongInLpQ15);
		now = (longWrite_ << 12) + longPhase_ * 683; // 683 = 4096 / 6
		int32_t lL = OnePole(longOutLp_[0], ReadLong(now - longDelay_[0]), kLongOutLpQ15);
		int32_t lR = OnePole(longOutLp_[1], ReadLong(now - longDelay_[1]), kLongOutLpQ15);
		if (++longPhase_ == 6)
		{
			longPhase_ = 0;
			int32_t fb = OnePole(longLoopLp_, lL, kLongLoopLpQ15);
			int32_t w = Saturate(longIn + ScaleTowardZero(fb, p.longFeedback));
			uint8_t code = Tables::muEncode[(w < 0 ? -w : w) >> 4] | (w < 0 ? 0x80 : 0);
			long_[longWrite_] = code;
			if (longWrite_ == 0)
				long_[kLongSize] = code;
			if (++longWrite_ == kLongSize)
				longWrite_ = 0;
		}

		wetL = sL + lL;
		wetR = sR + lR;
	}

private:
	// Room for the longest delay plus its wobble, plus a little.
	static constexpr int32_t kShortSize = kShortDelaySamples + kShortWobbleMs * 24 + 64;
	static constexpr int32_t kLongSize = kLongDelaySamples + kLongWobbleMs * 8 + 64;

	// `t` is a position in line samples, Q12, possibly a lap behind.
	int32_t ReadShort(int32_t t) const
	{
		if (t < 0)
			t += kShortSize << 12;
		int32_t i = t >> 12, f = t & 4095;
		int32_t a = short_[i], b = short_[i + 1];
		return a + (((b - a) * f + 2048) >> 12); // rounded: no drift into DC
	}

	int32_t ReadLong(int32_t t) const
	{
		if (t < 0)
			t += kLongSize << 12;
		int32_t i = t >> 12, f = t & 4095;
		int32_t a = Tables::muDecode[long_[i]], b = Tables::muDecode[long_[i + 1]];
		return a + (((b - a) * f + 2048) >> 12);
	}

	// Per-sample state first (short reach on the M0+), buffers last.
	int32_t shortWrite_ = 0, longWrite_ = 0;
	int32_t longPhase_ = 0;
	bool half_ = false;
	int32_t shortDelay_[2] = {kShortDelaySamples << 12, kShortDelaySamples << 12};
	int32_t longDelay_[2] = {kLongDelaySamples << 12, kLongDelaySamples << 12};
	int32_t shortInLp_ = 0, shortInPrev_ = 0, shortLoopLp_ = 0;
	int32_t longInLp1_ = 0, longInLp2_ = 0, longLoopLp_ = 0;
	int32_t longOutLp_[2] = {0, 0};
	int16_t short_[kShortSize + 1] = {};
	uint8_t long_[kLongSize + 1] = {};
};

} // namespace eq
