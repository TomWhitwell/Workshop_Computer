// Mode 5 — Synesthesia Voice.
//
// A complete voice: 1V/oct on Audio In 1 (0V = C3), gate on Pulse In 1.
// Oscillator -> wavefolder -> low-pass -> AD envelope -> both audio outs.
// Colour sets timbre; by default Red = cutoff, Green = fold depth,
// Blue = decay length. CV Out 2 carries the envelope.
// Main: shape morph sine -> saw -> square.
// Down (tap): rotate the colour roles (R/G/B -> G/B/R -> B/R/G).

#pragma once
#include "engine.h"
#include "osc.h"
#include "svf.h"

namespace lp {

class SynesthesiaMode : public Engine
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
	uint32_t inc_ = 0;
	int32_t  pitchIn_ = 0;     // Audio In 1, smoothed, Q6
	int32_t  drive_ = 256;     // fold drive, Q8, 1x..8x
	Svf      svf_;

	int32_t  env_ = 0;         // Q30
	int32_t  decayK_ = 4;
	bool     attacking_ = false;

	int      rotation_ = 0;
	bool     rotationChanged_ = false;
};

} // namespace lp
