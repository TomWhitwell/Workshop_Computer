#pragma once
#include <cstdint>
#include "Config.h"
#include "Dsp.h"
#include "Random.h"
#include "SmoothRandom.h"

namespace ab
{

// The noise of the medium the music is "on": the Tape knob's macro, run on
// core 1 a block at a time. Core 0 adds it to the mix after the reverb
// (it's on the tape, not in the room), so the master volume, and the fade
// between pieces, turn it down too.
//
//   Tape 0..100%   hiss, rising with the square of Tape (and tape wow and
//                  flutter on the voices, in Control). The hiss is never
//                  quite still: its level drifts a few dB either way, and
//                  its brightness wanders, each on a slow random glide.
//   Tape 50..100%  the hiss darkens and narrows, from a tape's bright hiss
//                  (a band around 3.5-8.5kHz) to the duller, filtered
//                  surface noise of a record (about 0.9-3.2kHz).
//   Tape 60..100%  a worn record's crackle fades in: fine ticks and, now and
//                  then, a soft pop, both growing with the square of how
//                  far past 60% Tape is -- at full, about 25 ticks and one
//                  pop a second.
//
// Ticks are single-sample clicks through a low-pass at about 5kHz; pops
// are short bursts (0.25-0.8ms) through one at about 1kHz, so they thump
// softly rather than snap. Most are small: their sizes are drawn so big
// ones are rare. Each lands at its own place in the stereo field. A
// high-pass at about 60Hz takes out the pops' low thud. (The crackle runs
// in mono and is panned as each tick or pop starts: half the filters.)
class Surface
{
public:
	void Seed(uint32_t seed)
	{
		rng_.Seed(seed);
		level_.Configure(2600, 5800, kWanderMinBlocks, kWanderMaxBlocks, kWanderStageBlocks); // -4..+3dB, Q12
		colour_.Configure(3500, 4700, kWanderMinBlocks, kWanderMaxBlocks, kWanderStageBlocks); // +/-15%, Q12
	}

	// One block of noise, 16-bit scale, into outL/outR. tapeQ12: the Tape
	// setting, 0..4096.
	void Render(int32_t tapeQ12, int32_t *outL, int32_t *outR)
	{
		// The hiss: square law, drifting in level.
		int32_t wander = level_.Tick(rng_, 4096);
		int32_t hiss = (((tapeQ12 * tapeQ12) >> 12) * kHissMaxQ16) >> 12;
		hiss = (hiss * wander) >> 12;

		// Its band: from tape hiss to record noise over the top half of
		// Tape, the top edge wandering.
		int32_t dark = tapeQ12 <= 2048 ? 0 : (tapeQ12 - 2048) * 2; // 0..4096
		int32_t hiK = kHissHighQ15 + (((kRecordHighQ15 - kHissHighQ15) * dark) >> 12);
		int32_t loK = kHissLowQ15 + (((kRecordLowQ15 - kHissLowQ15) * dark) >> 12);
		hiK = (hiK * colour_.Tick(rng_, 4096)) >> 12;

		// The crackle's amount: the square of how far past 60% (Q12).
		int32_t past = tapeQ12 <= kCrackleStart ? 0 : ((tapeQ12 - kCrackleStart) * 4096) / (4096 - kCrackleStart);
		past = past > 4096 ? 4096 : past;
		int32_t amount = (past * past) >> 12;
		// At most one tick and one pop start in a block (they're far rarer
		// than that): roll once for each, and pick the sample it lands on.
		int32_t tickAt = -1, popAt = -1;
		if (amount > 0)
		{
			if (rng_.Next() < (uint32_t)(((uint64_t)amount * kTickChance) >> 12))
				tickAt = rng_.Below(kBlock);
			if (rng_.Next() < (uint32_t)(((uint64_t)amount * kPopChance) >> 12))
				popAt = rng_.Below(kBlock);
		}

		for (int i = 0; i < kBlock; i++)
		{
			uint32_t n = rng_.Next();
			int32_t whiteL = (int32_t)(int16_t)(n >> 16), whiteR = (int32_t)(int16_t)n;
			int32_t bandL = OnePole(hiL_, whiteL, hiK) - OnePole(loL_, whiteL, loK);
			int32_t bandR = OnePole(hiR_, whiteR, hiK) - OnePole(loR_, whiteR, loK);
			outL[i] = (bandL * hiss) >> 16;
			outR[i] = (bandR * hiss) >> 16;
		}

		// The crackle, in mono, each tick and pop panned as it starts. Skipped
		// altogether while nothing is sounding (every filter settles exactly
		// on zero).
		if (tickAt < 0 && popAt < 0 && popLeft_ == 0 && (tickLp_ | popLp_ | hpX_ | hpY_) == 0)
			return;
		for (int i = 0; i < kBlock; i++)
		{
			int32_t tick = 0, pop = 0;
			if (i == tickAt)
			{
				tick = Size(kTickMax);
				int32_t pan = rng_.Below(4097);
				tickPanL_ = 4096 - pan;
				tickPanR_ = pan;
			}
			if (i == popAt && popLeft_ == 0)
			{
				popSize_ = Size(kPopMax);
				int32_t pan = rng_.Between(1024, 3072); // pops sit nearer the middle
				popPanL_ = (4096 - pan) * 2;
				popPanR_ = pan * 2;
				popLeft_ = rng_.Between(kPopMinSamples, kPopMaxSamples);
			}
			if (popLeft_ > 0)
			{
				pop = popSize_;
				popLeft_--;
			}
			int32_t t = OnePole(tickLp_, tick, kTickLpQ15);
			int32_t p = HighPass(hpX_, hpY_, OnePole(popLp_, pop, kPopLpQ15));
			outL[i] += (t * tickPanL_ + p * popPanL_) >> 12;
			outR[i] += (t * tickPanR_ + p * popPanR_) >> 12;
		}
	}

private:
	// Hiss at full Tape, Q16 of the band-passed noise: about -42dBFS at the
	// start-up volume, some 17dB under the music. At the start-up 40% it's
	// 16dB lower: a little hiss, as on a quiet tape.
	static constexpr int32_t kHissMaxQ16 = 1300;
	// The hiss band: the difference of two one-poles, at about 8.5kHz and
	// 3.5kHz (tape), moving to about 3.2kHz and 0.9kHz (record). Q15. The
	// narrower band is only 0.2dB quieter, so the level needs no help.
	static constexpr int32_t kHissHighQ15 = 21270, kHissLowQ15 = 12400;
	static constexpr int32_t kRecordHighQ15 = 11200, kRecordLowQ15 = 3640;
	// The drift: a new level and brightness every 1-4s, gliding there.
	static constexpr int32_t kWanderMinBlocks = BlocksFromMs(1000);
	static constexpr int32_t kWanderMaxBlocks = BlocksFromMs(4000);
	static constexpr int32_t kWanderStageBlocks = BlocksFromMs(600);

	// Crackle starts at 60% Tape.
	static constexpr int32_t kCrackleStart = 2458;
	// At full Tape: 25 ticks and 0.8 pops a second (chances per block, of
	// 2^32).
	static constexpr uint32_t kTickChance = (uint32_t)(4294967296.0 * 25 / kBlocksPerSecond);
	static constexpr uint32_t kPopChance = (uint32_t)(4294967296.0 * 0.8 / kBlocksPerSecond);
	// The biggest tick and pop, 16-bit scale: at the start-up volume, about
	// -24dBFS and -20dBFS at their peaks -- above the hiss, under the music.
	static constexpr int32_t kTickMax = 2000;
	static constexpr int32_t kPopMax = 1500;
	static constexpr int32_t kPopMinSamples = 12, kPopMaxSamples = 40;
	// Their low-passes: about 5kHz (crisp ticks) and 1kHz (soft pops).
	static constexpr int32_t kTickLpQ15 = 15700;
	static constexpr int32_t kPopLpQ15 = 4000;

	// A tick or pop's size, up to `most`, random sign. Cubed, so most are
	// small and a big one is rare.
	int32_t Size(int32_t most)
	{
		int32_t u = rng_.Below(4097);
		int32_t a = (((u * u) >> 12) * u) >> 12;
		a = 205 + ((a * (4096 - 205)) >> 12); // never quite nothing: 5% at least
		a = (a * most) >> 12;
		return (rng_.Next() & 1) ? a : -a;
	}

	// About 60Hz: y = x - x[-1] + 0.992 y[-1].
	static int32_t HighPass(int32_t &x1, int32_t &y1, int32_t x)
	{
		int32_t y = x - x1 + ScaleTowardZero(y1, 4063);
		x1 = x;
		y1 = y;
		return y;
	}

	Random rng_;
	SmoothRandom level_, colour_;
	int32_t hiL_ = 0, loL_ = 0, hiR_ = 0, loR_ = 0;
	int32_t tickLp_ = 0, popLp_ = 0, hpX_ = 0, hpY_ = 0;
	int32_t tickPanL_ = 0, tickPanR_ = 0, popPanL_ = 0, popPanR_ = 0; // Q12
	int32_t popSize_ = 0, popLeft_ = 0;
};

} // namespace ab
