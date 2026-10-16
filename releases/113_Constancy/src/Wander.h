#pragma once
#include <cstdint>
#include "Random.h"
#include "Tables.h"

namespace constancy
{

// Icarus's pitch drift for one saw, on core 1: a slow sine whose speed
// jumps to a new random value every second (SuperCollider's
// SinOsc.kr(LFNoise0.kr(1)) in Engine_Icarus.sc). The speed can be
// negative, so the wobble sometimes turns back on itself.
class Drift
{
public:
	// One millisecond. Returns the pitch offset in 1/65536ths of a semitone.
	int32_t Tick(Random &rng, int32_t cents)
	{
		if (--countdownMs_ <= 0)
		{
			countdownMs_ = kDriftChangeMs;
			// Hz (Q12) to a 32-bit phase step per ms: 2^32 / 1000 / 4096.
			int32_t hz = rng.Between(-kDriftMaxHzQ12, kDriftMaxHzQ12);
			inc_ = (uint32_t)(hz * 1049);
		}
		phase_ += inc_;
		// A cent is 65536 / 100 = 655 of these units.
		return (Sine(phase_) * cents * 655) >> 15;
	}

	void Seed(Random &rng) { phase_ = rng.Next(); }

private:
	uint32_t phase_ = 0;
	uint32_t inc_ = 0;
	int32_t countdownMs_ = 0;
};

// A smooth random wobble, on core 1, once a millisecond: every so often it
// picks a new random target between -1 and +1 and glides towards it
// through two smoothing stages, so it moves in soft S-curves with no
// corners. The caller chooses the centre and the width each time, so both
// can move while it wanders. (Equanimity's SmoothRandom, with the range
// taken out so the morning can move it.)
class Wander
{
public:
	// One millisecond: a new target every intervalMs (on average), each
	// approached through two stages of intervalMs / 3. Returns -4096..4096.
	int32_t Tick(Random &rng, int32_t intervalMs)
	{
		if (--countdownMs_ <= 0)
		{
			target_ = rng.Between(-4096, 4096) * 4096;
			countdownMs_ = rng.Between(intervalMs / 2, intervalMs + intervalMs / 2) + 1;
		}
		int32_t stage = intervalMs / 3 + 1;
		s1_ += (target_ - s1_) / stage;
		s2_ += (s1_ - s2_) / stage;
		return s2_ >> 12;
	}

private:
	int32_t target_ = 0, s1_ = 0, s2_ = 0; // Q24
	int32_t countdownMs_ = 0;
};

} // namespace constancy
