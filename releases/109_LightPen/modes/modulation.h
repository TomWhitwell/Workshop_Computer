// Mode 13 — Modulation.
//
// One LFO, two topologies, and the Main knob walks between them:
//
//   Main bottom third   a four-stage PHASER (allpass sweep, no delay line)
//   Main middle         a FLANGER (1ms delay, so the comb is high and metallic)
//   Main top            a CHORUS (up to 40ms, so it detunes rather than combs)
//
// Both run every sample and the knob CROSSFADES between them, the same trick
// SvfBlend uses on the filter taps: there is no discrete switch, so nothing to
// chatter and no hysteresis to tune. The delay time also lengthens across the
// upper two thirds, which is the only real difference between a flanger and a
// chorus once the LFO is doing the work.
//
//   Red   = LFO rate, 0.05Hz to 8Hz
//   Green = depth
//   Blue  = feedback, which is what makes a flanger ring and a phaser bite
//
// Down taps through the colour rotations. CV Out 2 follows the LFO, so the rest
// of the rack can move with it; Pulse Out 1 is high for the LFO's rising half.

#pragma once
#include "engine.h"
#include "fx.h"

namespace lp {

class ModulationMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;

private:
	static constexpr int      kStages  = 4;
	static constexpr uint32_t kLineLen = 2400;    // 50ms, all the chorus needs
	static constexpr int32_t  kMinBase = 48;      // 1ms, flanger
	static constexpr int32_t  kMaxBase = 1920;    // 40ms, chorus
	/// Where the phaser has faded out and the delay owns the sound.
	static constexpr int32_t  kPhaseEnd = 1365;   // a third of the knob

	/// First-order allpass: y = a*(x + y1) - x1. Four of these in series with a
	/// swept coefficient are a phaser; the notches move with `a`.
	struct Ap1
	{
		int32_t x1 = 0, y1 = 0;

		int32_t __attribute__((always_inline)) Process(int32_t x, int32_t aQ15)
		{
			int32_t y = (((x + y1) * aQ15) >> 15) - x1;
			x1 = x;
			y1 = y;
			return y;
		}
	};

	Ap1      ap_[kStages];
	Line     line_;
	uint32_t lfoPhase_ = 0;
	uint32_t lfoInc_ = 0;
	int32_t  depth_ = 0;
	int32_t  fb_ = 0;
	int32_t  base_ = kMinBase;
	int32_t  phaseW_ = 32767;
	int32_t  fbState_ = 0;
	int32_t  lfoQ15_ = 0;
	uint32_t clearPos_ = 0;

	int      rotation_ = 0;
	bool     rotationChanged_ = false;
};

} // namespace lp
