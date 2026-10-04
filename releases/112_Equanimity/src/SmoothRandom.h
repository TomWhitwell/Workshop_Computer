#pragma once
#include <cstdint>
#include "Random.h"

namespace eq
{

// A slow, smooth random value: every so often pick a new random target and
// glide towards it through two smoothing stages, so it moves in soft
// S-curves with no corners. Impermanence's "wander" (its CV Out 1), here
// run once a millisecond with a speed setting so the tide can hurry it
// along or slow it down.
//
// Used for the wander CV on CV Out 2 and for the tape wobble on each of the
// four delay read points.
class SmoothRandom
{
public:
	// Targets are picked between lo and hi (any units); a new one every
	// minMs..maxMs, approached through two stages of stageMs each (all at
	// speed 1).
	void Configure(int32_t lo, int32_t hi, int32_t minMs, int32_t maxMs, int32_t stageMs)
	{
		lo_ = lo;
		hi_ = hi;
		minMs_ = minMs;
		maxMs_ = maxMs;
		stageMs_ = stageMs;
		int64_t mid = (int64_t)((lo + hi) / 2) << 16;
		target_ = stage1_ = stage2_ = mid;
		countdown_ = 0;
	}

	// One millisecond. speedQ12: 4096 is normal, 8192 twice as fast.
	int32_t Tick(Random &rng, int32_t speedQ12)
	{
		countdown_ -= speedQ12;
		if (countdown_ <= 0)
		{
			int64_t span = (int64_t)(hi_ - lo_);
			target_ = (int64_t)(lo_ + (int32_t)((span * (rng.Next() >> 8)) >> 24)) << 16;
			countdown_ += rng.Between(minMs_, maxMs_) * 4096;
		}
		// Each stage closes 1/stageMs of the gap per millisecond (Q16).
		int64_t k = (int64_t)speedQ12 * 16 / stageMs_;
		stage1_ += ((target_ - stage1_) * k) >> 16;
		stage2_ += ((stage1_ - stage2_) * k) >> 16;
		return Value();
	}

	int32_t Value() const { return (int32_t)(stage2_ >> 16); }
	// The same, with 7 more bits of detail (x 128): ComputerCard's
	// CVOutPrecise units, for a CV that moves without visible steps.
	int32_t ValueFine() const { return (int32_t)(stage2_ >> 9); }

private:
	int32_t lo_ = 0, hi_ = 0, minMs_ = 1000, maxMs_ = 1000, stageMs_ = 700;
	int32_t countdown_ = 0; // ms x 4096
	int64_t target_ = 0, stage1_ = 0, stage2_ = 0; // Q16
};

} // namespace eq
