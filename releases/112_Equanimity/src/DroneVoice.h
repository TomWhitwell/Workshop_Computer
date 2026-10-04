#pragma once
#include <cstdint>
#include "Config.h"
#include "Params.h"
#include "Tables.h"

namespace eq
{

// The drone's sound, on core 0: four layers -- root, 5th, octave, upper 5th
// -- each a 1:1 FM pair (the modulator runs at the note itself, a warm,
// harmonic tone). Core 1 sets each layer's level and brightness from its
// own slow cycles (see Config.h); here they are summed and passed through a
// resonant lowpass filter.
//
// The filter is a "state-variable" filter (Chamberlin's): two integrators
// in a loop. `band` and `low` chase the input; the cutoff sets how fast,
// the damping how much they overshoot -- that overshoot is the resonance.
//
// For a change of set the whole drone fades out and in, in straight lines
// at rates core 1 sets, and the layers only take up new pitches once it has
// faded to nothing, so a change of key never slides or clicks.
class DroneVoice
{
public:
	// One sample, before the drone volume. Full scale is +/-32768.
	__attribute__((noinline)) int32_t Process(const EngineParams &p)
	{
		// Silent: safe to change pitch -- before the level moves, so a swell
		// starts on the new pitches, not the old ones.
		if (amp_ == 0)
			for (int i = 0; i < kDroneLayers; i++)
				inc_[i] = p.droneLayer[i].inc;

		if (amp_ < p.droneLevel)
		{
			amp_ += p.droneSwellStep;
			if (amp_ > p.droneLevel)
				amp_ = p.droneLevel;
		}
		else if (amp_ > p.droneLevel)
		{
			amp_ -= p.droneReleaseStep;
			if (amp_ < p.droneLevel)
				amp_ = p.droneLevel;
		}
		if (amp_ == 0)
		{
			low_ = band_ = 0;
			return 0;
		}

		// The layers, each weighted by its level (Q12). With a 1:1 ratio the
		// modulator and carrier share one phase.
		int32_t sum = 0;
		for (int i = 0; i < kDroneLayers; i++)
		{
			int32_t m = Sine(phase_[i]);
			uint32_t deviation = (uint32_t)m * (uint32_t)(p.droneLayer[i].index >> 8);
			int32_t c = Sine(phase_[i] + deviation);
			phase_[i] += inc_[i];
			sum += c * p.droneLayer[i].gain;
		}
		sum >>= 12; // the levels add up to at most ~4096: back to +/-32768

		// x4 inside the filter, for finer steps at low cutoffs.
		int32_t x = (sum * (amp_ >> 9)) >> 13;
		int32_t f = p.droneCutoff;
		low_ += (f * band_) >> 15;
		int32_t high = x - low_ - ((kDroneFilterDampingQ15 * band_) >> 15);
		band_ += (f * high) >> 15;
		return low_ >> 2;
	}

	int32_t Amp() const { return amp_; }
	uint32_t Inc(int layer) const { return inc_[layer]; }

private:
	uint32_t phase_[kDroneLayers] = {};
	uint32_t inc_[kDroneLayers] = {};
	int32_t amp_ = 0;			 // envelope units: 1 << 24 is full scale
	int32_t low_ = 0, band_ = 0; // the filter (x4)
};

} // namespace eq
