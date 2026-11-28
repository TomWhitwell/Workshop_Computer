#pragma once
#include <cstdint>
#include "Random.h"

namespace ab
{

// A slow, smooth random value: every so often pick a new random target and
// glide towards it through two smoothing stages, so it moves in soft
// S-curves with no corners. Equanimity's version (itself Impermanence's
// "wander"), here run once per audio block with a speed setting so a faster
// piece wanders faster.
//
// Each random CV out owns one of these and its own Random, so the two never
// move together.
class SmoothRandom
{
public:
	// Targets are picked between lo and hi (any units); a new one every
	// minTicks..maxTicks, approached through two stages of stageTicks each
	// (all at speed 1).
	void Configure(int32_t lo, int32_t hi, int32_t minTicks, int32_t maxTicks, int32_t stageTicks)
	{
		lo_ = lo;
		hi_ = hi;
		minTicks_ = minTicks;
		maxTicks_ = maxTicks;
		stageTicks_ = stageTicks;
		int64_t mid = (int64_t)((lo + hi) / 2) * 65536; // Q16 (a multiply: lo may be negative)
		target_ = stage1_ = stage2_ = mid;
		countdown_ = 0;
	}

	// One tick. speedQ12: 4096 is normal, 8192 twice as fast.
	int32_t Tick(Random &rng, int32_t speedQ12)
	{
		countdown_ -= speedQ12;
		if (countdown_ <= 0)
		{
			int64_t span = (int64_t)(hi_ - lo_);
			target_ = (int64_t)(lo_ + (int32_t)((span * (rng.Next() >> 8)) >> 24)) * 65536;
			countdown_ += (int64_t)rng.Between(minTicks_, maxTicks_) * 4096;
		}
		// Each stage closes 1/stageTicks of the gap per tick (Q16).
		int64_t k = (int64_t)speedQ12 * 16 / stageTicks_;
		stage1_ += ((target_ - stage1_) * k) >> 16;
		stage2_ += ((stage1_ - stage2_) * k) >> 16;
		return Value();
	}

	int32_t Value() const { return (int32_t)(stage2_ >> 16); }

private:
	int32_t lo_ = 0, hi_ = 0, minTicks_ = 1000, maxTicks_ = 1000, stageTicks_ = 700;
	int64_t countdown_ = 0;						   // ticks x 4096
	int64_t target_ = 0, stage1_ = 0, stage2_ = 0; // Q16
};

} // namespace ab
