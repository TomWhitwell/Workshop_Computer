/*
 * placeholder_reverb.h — Placeholder (EB) reverb (feedback delay network)
 *
 * Digital emulation of the Fairfield Circuitry Placeholder (EB revision), per
 * EB_Specifications.pdf.
 *
 * Three delay lines, each with an out-of-phase nested feedback path, plus a
 * fourth in-phase path carrying the tilt-filtered sum of all three. All four
 * share the DECAY gain, so each line's own contribution cancels and what
 * remains is pure cross-feedback — "every delay line feeds back only to every
 * other delay line". TONE sits inside that fourth path as well as on the wet
 * output, and a 1st-order hi-cut follows it in the output section.
 */

#pragma once
#include <stdint.h>
#include <string.h>

class PlaceholderReverb
{
public:
	enum class ModMode : uint8_t
	{
		None      = 0,
		Cyclical  = 1,
		Random    = 2,
		Both      = 3,
	};

	static constexpr int     BUF_SIZE  = 8192;
	static constexpr int     BUF_MASK  = BUF_SIZE - 1;

	static constexpr int32_t TIME_MIN  = 576;   // 12 ms @ 48 kHz
	static constexpr int32_t TIME_MAX  = 7680;  // 160 ms

	// One-pole LP coefficient for ~500 Hz pivot (EB TONE spec)
	static constexpr int32_t TONE_LP_K = 30720;

	// DECAY ceiling. The matrix below gives the in-phase mode a gain of
	// 2*decay_/32768, so unity is at 16384 — three quarters of the way up,
	// i.e. the "2-3 o'clock" onset of self-oscillation the EB spec describes.
	// Full CW reaches a loop gain of 1.33 and sings, as it should.
	static constexpr int32_t DECAY_MAX = 21845;

	// ~20 Hz one-pole on the modulation signal (Q15)
	static constexpr int32_t MOD_SMOOTH_K = 86;

	// Wet hi-cut alpha anchors: 1 / 2 / 4 / 8 kHz, then open
	static constexpr int32_t kHiCutAlpha[5] = { 4021, 7546, 13356, 21270, 32768 };

	void __not_in_flash_func(resetToneFilter)()
	{
		lpState_    = 0;
		hiCutState_ = 0;
	}

	PlaceholderReverb()
	{
		memset(buf_, 0, sizeof(buf_));
		for (int i = 0; i < 3; ++i)
		{
			writePos_[i]  = 0;
			len_[i]       = 4800;
			targetLen_[i] = 4800;
			modPhase_[i]  = 0;
			modRand_[i]   = 0;
			modRandT_[i]  = 0;
			modSmooth_[i] = 0;
			// Distinct non-zero xorshift seeds — "each delay-line gets its own
			// randomness" (EB spec, MOD SWITCHES).
			modRandState_[i] = 0x9E3779B9u * (uint32_t)(i + 1);
		}
		decay_       = 12000;
		ratio_       = 0;
		spread_      = 2048;
		tone_        = 0;
		modDepth_    = 0;
		modMode_     = ModMode::None;
		hpState_     = 0;
		hpPrev_      = 0;
		lpState_     = 0;
		hiCutKnob_   = -1;      // force the first setHiCut() to take effect
		hiCutAlpha_  = 32768;   // open
		hiCutState_  = 0;
		updateRatioTargets();
		for (int i = 0; i < 3; ++i)
			len_[i] = targetLen_[i];
	}

	void __not_in_flash_func(setTime)(int32_t samples)
	{
		if (samples < TIME_MIN)  samples = TIME_MIN;
		if (samples > TIME_MAX)  samples = TIME_MAX;
		if (samples == targetLen_[0]) return;   // called every sample; skip no-ops
		targetLen_[0] = samples;
		updateRatioTargets();
	}

	void __not_in_flash_func(setRatio)(int32_t ratio)
	{
		if (ratio < -4096) ratio = -4096;
		if (ratio >  4096) ratio =  4096;
		if (ratio == ratio_) return;            // called every sample; skip no-ops
		ratio_ = ratio;
		updateRatioTargets();
	}

	void __not_in_flash_func(setFeedback)(int32_t fb)
	{
		if (fb < 0)         fb = 0;
		if (fb > DECAY_MAX) fb = DECAY_MAX;
		decay_ = fb;
	}

	void __not_in_flash_func(setTone)(int32_t tone)
	{
		if (tone < -4096) tone = -4096;
		if (tone >  4096) tone =  4096;
		tone_ = tone;
	}

	// MOD DEPTH — subtle delay-time modulation (0 = off).
	void __not_in_flash_func(setModDepth)(int32_t depth)
	{
		if (depth < 0)    depth = 0;
		if (depth > 4096) depth = 4096;
		modDepth_ = depth;
	}

	// MOD TYPE — knob 0..4095 maps to none / cyclical / random / both.
	void __not_in_flash_func(setModType)(int32_t typeKnob)
	{
		if (typeKnob < 0) typeKnob = 0;
		if (typeKnob > 4095) typeKnob = 4095;
		if (typeKnob < 1024)
			modMode_ = ModMode::None;
		else if (typeKnob < 2048)
			modMode_ = ModMode::Cyclical;
		else if (typeKnob < 3072)
			modMode_ = ModMode::Random;
		else
			modMode_ = ModMode::Both;
	}

	// Wet-path hi-cut — the pedal's F switch, which sits in the OUTPUT section
	// between the tilt filter and the MIX pot ("LOW-PASS 1st ORDER FILTER
	// CUTOFF: 2 more 2kHz / 1 slight 4kHz / 0 open 8kHz"). The pedal gives three
	// positions; this sweeps continuously through them and on to fully open.
	void __not_in_flash_func(setHiCut)(int32_t cutoffKnob)
	{
		if (cutoffKnob < 0)    cutoffKnob = 0;
		if (cutoffKnob > 4095) cutoffKnob = 4095;
		if (cutoffKnob == hiCutKnob_) return;   // called every sample
		hiCutKnob_ = cutoffKnob;

		// One-pole alpha = 1 - exp(-2*pi*fc/48000), Q15, anchored at
		// 1 / 2 / 4 / 8 kHz, then wide open at the top of the knob.
		int32_t scaled = (cutoffKnob * 1024) / 4095;   // Q8 across 4 segments
		int32_t seg    = scaled >> 8;
		if (seg >= 4)
		{
			hiCutAlpha_ = 32768;                       // open (filter bypassed)
			return;
		}
		int32_t frac = scaled & 0xFF;
		hiCutAlpha_  = kHiCutAlpha[seg]
		             + (((kHiCutAlpha[seg + 1] - kHiCutAlpha[seg]) * frac) >> 8);
	}

	void __not_in_flash_func(process)(
		int32_t inL, int32_t inR,
		int32_t &outL, int32_t &outR)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (len_[i] < targetLen_[i]) ++len_[i];
			else if (len_[i] > targetLen_[i]) --len_[i];
		}

		int32_t modOff[3];
		computeModOffsets(modOff);

		int32_t d[3];
		for (int i = 0; i < 3; ++i)
		{
			int32_t readLen = len_[i] + modOff[i];
			if (readLen < TIME_MIN) readLen = TIME_MIN;
			if (readLen > TIME_MAX) readLen = TIME_MAX;
			d[i] = readDelay(i, readLen);
		}

		int32_t mono = (inL + inR) >> 1;
		int32_t hpIn = mono - hpPrev_ + shrTowardZero(hpState_ * 32639, 15);
		hpPrev_  = mono;
		hpState_ = hpIn;

		// Inject at -6 dB. The in-phase mode of the matrix below has a gain of
		// 2*decay_, so the network builds well past the input level before the
		// soft limiter takes over; this keeps normal settings out of it.
		int32_t inj = hpIn >> 1;

		// Fourth feedback path: the sum of the three delay outputs through the
		// TONE tilt, in phase. Per the EB spec, "Not only is the TONE output
		// used as the WET output, it's also fed back through the fourth
		// feedback path, simulating various damping factors within a place."
		int32_t tilt = applyTone(d[0] + d[1] + d[2]);

		// Each line's own nested path is out of phase and shares the same DECAY
		// VCA, so the self term cancels and what is left is the other two lines
		// ("every delay line feeds back only to every other delay line").
		// In-phase gain is therefore 2*decay_, and DECAY is scaled so that
		// crossing unity — the onset of self-oscillation — lands around 2-3
		// o'clock on the knob, as the pedal documents.
		for (int i = 0; i < 3; ++i)
			writeDelay(i, softLimit(inj + shrTowardZero((tilt - d[i]) * decay_, 15)));

		int32_t wet   = applyHiCut((tilt * 21845) >> 16);   // tilt / 3
		int32_t width = ((d[0] - d[2]) * spread_) >> 13;
		outL = softLimit(wet + width);
		outR = softLimit(wet - width);
	}

private:
	// Arithmetic right shift rounds towards -inf, so a decaying one-pole built
	// on >> never gets back to zero from below: at x = -100, (-100*32639)>>15
	// is -100 again, and the state latches forever. Round towards zero instead.
	// Branch-free: add (2^shift - 1) when the value is negative.
	static int32_t __not_in_flash_func(shrTowardZero)(int32_t x, int shift)
	{
		return (x + ((x >> 31) & ((1 << shift) - 1))) >> shift;
	}

	void __not_in_flash_func(updateRatioTargets)()
	{
		int32_t size = targetLen_[0];
		int32_t d2 = size + ((size * ratio_) >> 13);
		int32_t d3 = size + ((size * ratio_ * 3) >> 14);
		if (d2 < TIME_MIN) d2 = TIME_MIN;
		if (d3 < TIME_MIN) d3 = TIME_MIN;
		if (d2 > TIME_MAX) d2 = TIME_MAX;
		if (d3 > TIME_MAX) d3 = TIME_MAX;
		targetLen_[1] = d2;
		targetLen_[2] = d3;
	}

	void __not_in_flash_func(computeModOffsets)(int32_t *modOff)
	{
		modOff[0] = modOff[1] = modOff[2] = 0;
		if (modMode_ == ModMode::None || modDepth_ == 0)
			return;

		for (int i = 0; i < 3; ++i)
		{
			int32_t cyclic = 0;
			int32_t random = 0;

			if (modMode_ == ModMode::Cyclical || modMode_ == ModMode::Both)
			{
				// Rate ∝ 1/delay (EB cyclical mode): one LFO cycle per 64 delay
				// periods — 1.3 Hz at 12 ms, 0.1 Hz at 160 ms. modPhase_ is a
				// Q32 accumulator, so step = rate · 2^32 / 48000. The previous
				// `48000 / len` gave a step of ~9, i.e. one cycle every 2.8
				// hours, which left cyclical MOD frozen at a DC offset.
				uint32_t step = (uint32_t)((1ull << 32)
				                           / ((uint64_t)len_[i] * 64u));
				modPhase_[i] += step;
				// Triangle LFO, −8192..8191
				uint32_t p = modPhase_[i] >> 16;
				int32_t tri = (int32_t)(p & 0xFFFF);
				if (tri > 32767) tri = 65535 - tri;
				cyclic = (tri - 16384) >> 1;
			}

			if (modMode_ == ModMode::Random || modMode_ == ModMode::Both)
			{
				if (++modRandT_[i] > (uint32_t)(len_[i] * 4))
				{
					modRandT_[i] = 0;
					// xorshift32 per line. The previous version derived its
					// value from modPhase_, which only advances in the cyclical
					// branch above — so Random on its own drew one constant per
					// line at startup and then never moved again.
					uint32_t s = modRandState_[i];
					s ^= s << 13;
					s ^= s >> 17;
					s ^= s << 5;
					modRandState_[i] = s;
					modRand_[i] = (int32_t)(s & 0x3FFF) - 8192;
				}
				random = modRand_[i] / 4;
			}

			// Fixed ~20 Hz smoothing so the sample-and-hold in random mode does
			// not step the delay pointer. This is an implementation detail, not
			// a pedal control — the EB's only low-pass is the output hi-cut.
			int32_t raw = cyclic + random;
			modSmooth_[i] += (((raw * 16) - modSmooth_[i]) * MOD_SMOOTH_K) >> 15;
			raw = modSmooth_[i] / 16;

			// Subtle depth: ~4 % of delay length at full depth. Split into two
			// Q15 steps — raw · modDepth_ · len_ overflows int32 in one go.
			modOff[i] = (((raw * modDepth_) >> 15) * len_[i]) >> 15;
		}
	}

	int32_t __not_in_flash_func(readDelay)(int line, int32_t delayLen)
	{
		int rp = (writePos_[line] - delayLen) & BUF_MASK;
		return (int32_t)buf_[line][rp];
	}

	void __not_in_flash_func(writeDelay)(int line, int16_t sample)
	{
		buf_[line][writePos_[line]] = sample;
		writePos_[line] = (writePos_[line] + 1) & BUF_MASK;
	}

	static int16_t __not_in_flash_func(softLimit)(int32_t x)
	{
		// Gentle saturation before int16 delay storage (avoids hard clip hash).
		// The ceiling sits well above full scale so the network can build into
		// self-oscillation — this is what bounds it when DECAY is past 2-3
		// o'clock, standing in for the BBDs running out of headroom.
		if (x > 2047)
		{
			int32_t over = x - 2047;
			x = 2047 + (over >> 2);
			if (x > 4095)
				x = 4095;
		}
		else if (x < -2048)
		{
			int32_t over = -2048 - x;
			x = -2048 - (over >> 2);
			if (x < -4096)
				x = -4096;
		}
		return (int16_t)x;
	}

	int32_t __not_in_flash_func(applyHiCut)(int32_t x)
	{
		if (hiCutAlpha_ >= 32768)
			return x;                       // fully open
		hiCutState_ += (((x * 16) - hiCutState_) * hiCutAlpha_) >> 15;
		return hiCutState_ / 16;
	}

	int32_t __not_in_flash_func(applyTone)(int32_t x)
	{
		// ~500 Hz one-pole split (EB: tilt centred at 500 Hz).
		// State is held 16x oversized so the update keeps sub-count resolution;
		// without the (1 - k) factor this would be a leaky integrator with a DC
		// gain of 16, which made `hp` meaningless and collapsed the level.
		lpState_ += (((x * 16) - lpState_) * (32768 - TONE_LP_K)) >> 15;
		int32_t lp = lpState_ / 16;
		int32_t hp = x - lp;

		if (tone_ == 0)
			return x;

		// Shelving tilt: CW cuts lows + boosts highs, CCW the reverse. About
		// ±5 dB at the extremes — enough for the spec's "oscillations are more
		// likely when TONE is at either extreme" to fall out on its own, since
		// this filter sits inside the feedback path.
		int32_t t = tone_;
		int32_t gLo = 4096 - (t >> 1);
		int32_t gHi = 4096 + (t >> 1);
		if (gLo < 2048)
			gLo = 2048;
		if (gHi > 6144)
			gHi = 6144;

		// gLo/gHi are Q12 (4096 = unity), so the recombination shifts by 12.
		// Shifting by 13 dropped 6 dB the instant TONE left its centre detent.
		// No limiting here — this runs on the sum of all three lines, and the
		// bound belongs at writeDelay()/the output, not mid-filter.
		return ((lp * gLo) + (hp * gHi)) >> 12;
	}

	int16_t  buf_[3][BUF_SIZE];

	int      writePos_[3];
	int32_t  len_[3];
	int32_t  targetLen_[3];
	int32_t  decay_;
	int32_t  ratio_;
	int32_t  spread_;
	int32_t  tone_;

	int32_t  modDepth_;
	ModMode  modMode_;
	uint32_t modPhase_[3];
	int32_t  modRand_[3];
	uint32_t modRandT_[3];
	uint32_t modRandState_[3];
	int32_t  modSmooth_[3];

	int32_t  hpState_;
	int32_t  hpPrev_;
	int32_t  lpState_;

	int32_t  hiCutKnob_;
	int32_t  hiCutAlpha_;
	int32_t  hiCutState_;
};
