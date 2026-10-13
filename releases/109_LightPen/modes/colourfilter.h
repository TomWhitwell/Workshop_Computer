// Mode 9 — Colour Filter.
//
// Audio In 1 goes through a state-variable filter and out of both audio outs,
// with the wand holding the three filter controls at once:
//
//   Red   = cutoff
//   Green = resonance
//   Blue  = filter type, a continuous walk LP -> BP -> HP
//
// Down (tap) rotates those three roles, as Modes 5 and 8 do, so any one colour
// can be the one you sweep.
//
// Unlike every other mode, this one is a PROCESSOR: with nothing patched into
// Audio In 1 the normalisation probe holds it at zero and the mode is silent.
// That is correct, not a fault.
//
// Main is drive into the filter, 1x to 8x into a cubic soft clip. It is what
// makes the resonance sing, and it puts back the level a narrow band-pass
// setting takes away. At the bottom of the travel a full-scale input still
// loses about 1.4dB to the knee — a drive knob at minimum, not a bypass.
//
// The two jacks a filter leaves idle carry an envelope follower on the
// output: CV Out 2 is the level, Pulse Out 1 is high while there is signal.

#pragma once
#include "engine.h"
#include "svf.h"

namespace lp {

class ColourFilterMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;

private:
	// Gate thresholds on the follower, in output units. Well clear of the
	// converter's noise floor, and far enough apart not to chatter on a decay.
	static constexpr int32_t kGateOn  = 40;
	static constexpr int32_t kGateOff = 18;

	// The bottom of the cutoff sweep, as a Q16 colour handed to Svf::Set.
	// 20480 is ~228Hz; 0 would be ~40Hz, which is inaudible on most material
	// and is what made this mode look silent at rest. See colourfilter.cpp.
	static constexpr int32_t kCutFloor = 20480;

	Svf      svf_;
	SvfBlend blend_;
	int32_t  drive_ = 5461;    // Q12; 5461 is unity through the soft clip
	int32_t  env_   = 0;       // rectified output, one-pole
	bool     gate_  = false;

	int      rotation_ = 0;
	bool     rotationChanged_ = false;
};

} // namespace lp
