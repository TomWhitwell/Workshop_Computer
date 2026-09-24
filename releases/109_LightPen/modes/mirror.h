// Mode 1 — RGB CV Mirror & Gate Tracker.
//
// Continuous: CV Out 2 = Red, Audio Out 1 = Green, Audio Out 2 = Blue, 0..~5V.
// Gates: Pulse Out 1 high while Red is over the threshold, Pulse Out 2 for Blue.
// Main: gate threshold. Down (held): freeze the three continuous outputs.

#pragma once
#include "engine.h"

namespace lp {

/// A level gate with a hysteresis band either side of the threshold.
struct ThresholdGate
{
	static constexpr int32_t kHalfBand = 1000;   // ~1.5% of Q16 each side

	bool on = false;

	bool Update(int32_t u, int32_t thresholdQ16)
	{
		if (on) { if (u < thresholdQ16 - kHalfBand) on = false; }
		else    { if (u > thresholdQ16 + kHalfBand) on = true;  }
		return on;
	}
};

class MirrorMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;

private:
	ThresholdGate gateR_, gateB_;
	bool frozen_ = false;
};

} // namespace lp
