#pragma once
#include <cstdint>
#include "Config.h"
#include "Params.h"
#include "Random.h"
#include "Tables.h"

namespace eq
{

// The drone's slow life, on core 1: four layers on the set's root, each
// with a volume cycle and a timbre cycle of its own, plus a cycle for the
// filter -- nine slow sine waves, all of unrelated lengths (Config.h). Each
// layer rises and sinks, brightens and darkens, on its own schedule, so the
// drone's colour keeps shifting without it ever changing note. Every set
// starts the cycles at fresh random points.
//
// Its pitch only changes with a new set: it fades out with the old set and
// swells back in on the new root.
class Drone
{
public:
	// A new set (or power-on): this root, cycles at random points, swelling in.
	void Start(int32_t root, Random &rng)
	{
		root_ = root;
		for (int i = 0; i < kDroneLayers; i++)
		{
			volumePhase_[i] = rng.Next();
			timbrePhase_[i] = rng.Next();
		}
		filterPhase_ = rng.Next();
		sounding_ = true;
	}

	// A new set is coming: fade out and stay silent until Start().
	void FadeOut() { sounding_ = false; }

	// Once a millisecond: move the cycles on and fill in the drone's part of
	// the parameter block. tide is Q12.
	void Tick(EngineParams &p, int32_t tide)
	{
		constexpr int32_t kPeak = kDronePeakQ12 * 4096; // envelope units
		p.droneLevel = sounding_ ? kPeak : 0;
		p.droneSwellStep = kPeak / (kDroneSwellMs * 48) + 1;
		p.droneReleaseStep = kPeak / (kDroneReleaseMs * 48) + 1;

		int32_t indexMax = kDroneIndexMaxLowTideQ12 + (((kDroneIndexMaxHighTideQ12 - kDroneIndexMaxLowTideQ12) * tide) >> 12);
		for (int i = 0; i < kDroneLayers; i++)
		{
			const DroneLayer &layer = kDroneLayer[i];
			volumePhase_[i] += CycleInc(layer.volumeSecsX10);
			timbrePhase_[i] += CycleInc(layer.timbreSecsX10);
			int32_t volume = Cycle(volumePhase_[i]);
			int32_t timbre = Cycle(timbrePhase_[i]);

			// Pitch: the layer's interval above the root, a few cents off.
			uint32_t inc = Tables::noteInc[kDroneBottom + root_ + layer.interval];
			p.droneLayer[i].inc = inc + (uint32_t)((int32_t)(inc >> 16) * layer.detuneQ16);
			// Level: between its floor and its full weight, with its cycle.
			int32_t level = layer.floorQ12 + (((4096 - layer.floorQ12) * volume) >> 12);
			p.droneLayer[i].gain = (layer.weightQ12 * level) >> 12;
			// Brightness: between dull and bright with its cycle; the bright
			// end rises with the tide (same units as a melody strike).
			p.droneLayer[i].index = (kDroneIndexMinQ12 + (((indexMax - kDroneIndexMinQ12) * timbre) >> 12)) * 1304;
		}

		// The filter: its own cycle, and the tide, each opening it by
		// octaves. The filter wants 2 sin(pi fc / 48000), which for these
		// cutoffs is 2 pi fc / 48000 to within 0.5%: x 32768 for Q15 is
		// fc x 4.289 (4392 / 1024).
		filterPhase_ += CycleInc(kDroneFilterSecsX10);
		int32_t octaves = ((kDroneFilterCycleOctavesQ12 * Cycle(filterPhase_)) >> 12) +
						  ((kDroneFilterTideOctavesQ12 * tide) >> 12);
		// (In 64 bits, so the coefficient moves in its own fine steps rather
		// than whole hertz.)
		p.droneCutoff = (int32_t)(((int64_t)kDroneFilterBaseHz * 4392 * Exp2(octaves)) >> 26);
	}

	bool Sounding() const { return sounding_; }
	int32_t Root() const { return root_; }

private:
	// Phase step per millisecond for a cycle this long (seconds x 10).
	static constexpr uint32_t CycleInc(int32_t secsX10) { return (uint32_t)(4294967296ull / ((uint64_t)secsX10 * 100)); }

	// A slow sine, 0..4096: starts at 0, rises to 4096, falls back.
	static int32_t Cycle(uint32_t phase) { return (4096 - (Sine(phase) >> 3)) >> 1; }

	int32_t root_ = 0;
	bool sounding_ = false;
	uint32_t volumePhase_[kDroneLayers] = {};
	uint32_t timbrePhase_[kDroneLayers] = {};
	uint32_t filterPhase_ = 0;
};

} // namespace eq
