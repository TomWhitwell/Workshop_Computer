#pragma once
#include <cstdint>
#include "Config.h"
#include "Random.h"
#include "Tables.h"

namespace eq
{

// Tide triggers (Pulse Out 2), on core 1: random triggers whose density
// follows the tide. Because the tide is shared, they get busy at the same
// moments the melody does -- patched to an envelope or a second voice, the
// whole system rises and settles together.
//
//   Low tide   a trigger every 10-30 seconds or so, sometimes nothing for
//              longer.
//   High tide  clusters of triggers, a few per second at the peak.
//
// The rate rises in octaves rather than in a straight line, which keeps the
// low tide properly sparse and lets the high tide really fill up.
class TideTriggers
{
public:
	// Once a millisecond. tideCurve is the tide squared (Q12). Returns true
	// when a trigger should fire.
	bool Tick(Random &rng, int32_t tideCurve)
	{
		if (clusterLeft_ > 0)
		{
			// The rest of a cluster, at its own uneven spacing.
			if (--clusterGapMs_ > 0)
				return false;
			clusterLeft_--;
			clusterGapMs_ = rng.Between(kTideTriggerClusterGapMinMs, kTideTriggerClusterGapMaxMs);
			return true;
		}
		int32_t rate = (kTideTriggerRateLowTideQ24 * Exp2((kTideTriggerRateOctavesQ12 * tideCurve) >> 12)) >> 16;
		if (!rng.Chance24(rate))
			return false;
		// The higher the tide, the more triggers may follow this one.
		clusterLeft_ = rng.Below(1 + ((kTideTriggerClusterMax * tideCurve) >> 12));
		clusterGapMs_ = rng.Between(kTideTriggerClusterGapMinMs, kTideTriggerClusterGapMaxMs);
		return true;
	}

private:
	int32_t clusterLeft_ = 0;
	int32_t clusterGapMs_ = 0;
};

} // namespace eq
