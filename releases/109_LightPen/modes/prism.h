// Mode 8 — Prism Voice.
//
// Mode 5's shape — 1V/oct on Audio In 1 (0V = C3), gate on Pulse In 1, voice
// to both audio outs, envelope on CV Out 2 — wired to a different set of
// parameters, and a different character:
//
//   Red   = FM depth, a second oscillator an octave up bent into this one
//   Green = sample-rate crush, from clean down to 1/64th rate
//   Blue  = low-pass cutoff
//
// Where Mode 5 plucks (its envelope decays whatever the gate does), this one
// SUSTAINS: it holds while the gate is high and releases when it falls, so
// the same patch reads as an instrument rather than a percussion voice.
//
// Main: shape morph sine -> triangle -> square.
// Down (tap): rotate the colour roles, as Mode 5.

#pragma once
#include "engine.h"
#include "osc.h"
#include "svf.h"

namespace lp {

class PrismMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;

private:
	static constexpr int kBaseNote = 48;   // C3 at 0V

	Morph    morph_;
	uint32_t phase_ = 0;
	uint32_t modPhase_ = 0;
	uint32_t inc_ = 0;
	int32_t  pitchIn_ = 0;     // Audio In 1, smoothed, Q6
	int32_t  fmDepth_ = 0;
	int32_t  crushN_ = 1;
	int32_t  crushCount_ = 0;
	int32_t  held_ = 0;
	Svf      svf_;

	int32_t  env_ = 0;         // Q30
	bool     attacking_ = false;

	int      rotation_ = 0;
	bool     rotationChanged_ = false;
};

} // namespace lp
