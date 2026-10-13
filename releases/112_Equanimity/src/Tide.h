#pragma once
#include <cstdint>
#include "Config.h"
#include "Random.h"
#include "Tables.h"

namespace eq
{

// Phase step per sample for a sine with this period: 2^32 / (seconds x 48000).
constexpr uint32_t TideInc(int32_t seconds) { return (uint32_t)(4294967296ull / ((uint64_t)seconds * kSampleRate)); }

// The tide: one slow value, 0 to 1, that drives the whole card -- how often
// notes are born, how bright they are, how busy the tide triggers are, how
// fast the wander CVs move, how bright the drone is. It's why the parts
// breathe together instead of wandering separately.
//
// Three very slow sine waves with unrelated periods (47s, 113s and 271s),
// summed and scaled to 0..1. Each starts at a random phase, so no two
// power-ons rise and fall the same way; the whole pattern takes hours to
// repeat.
class Tide
{
public:
	void Seed(Random &rng)
	{
		for (uint32_t &p : phase0_)
			p = rng.Next();
	}

	// The tide at a moment (samples since power-on), Q12: 0..4096.
	//
	// Each sine's phase is worked out from the time directly, rather than
	// added up step by step, so it never drifts. (The sample counter wraps
	// after about a day; the phase arithmetic wraps with it, seamlessly.)
	int32_t At(uint32_t samples) const
	{
		int32_t sum = 0;
		for (int i = 0; i < 3; i++)
			sum += Sine(phase0_[i] + samples * kInc[i]);
		int32_t average = ((sum >> 2) * 21845) >> 14; // sum / 3, +/-32767
		return (average + 32768) >> 4;
	}

private:
	static constexpr uint32_t kInc[3] = {TideInc(kTidePeriodSeconds[0]), TideInc(kTidePeriodSeconds[1]),
										 TideInc(kTidePeriodSeconds[2])};

	uint32_t phase0_[3] = {0, 0, 0};
};

} // namespace eq
