// Mode 14 — Mangle.
//
// The two destructive effects, in series, with no buffer at all:
//
//   Red   = crush, bit depth and sample rate together (12 bits and clean, down
//           to 3 bits held for 32 samples)
//   Green = fold, drive into the triangle wavefolder
//   Blue  = ring modulation depth
//
// Main is the ring modulator's carrier pitch, 20Hz to about 2kHz — low for a
// tremolo, mid for the classic metallic clang, high for sidebands that read as
// a new timbre rather than as an effect.
//
// Bit crush and rate crush move together on one colour on purpose. They are the
// same gesture musically — "make it cheaper" — and separating them would spend
// a whole sensor on a distinction you cannot hear independently at these
// depths, leaving nothing for the ring modulator.
//
// Crush first, then fold, then ring: each stage wants the one before it to have
// already happened. Folding a crushed signal keeps the staircase audible, while
// crushing a folded one just samples the folds.
//
// Down taps through the colour rotations. CV Out 2 is an envelope follower.

#pragma once
#include "engine.h"

namespace lp {

class MangleMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;

private:
	uint32_t carrPhase_ = 0;
	uint32_t carrInc_ = 0;
	int32_t  holdN_ = 1;        // sample-rate crush, 1..32 samples held
	int32_t  holdCount_ = 0;
	int32_t  held_ = 0;
	int32_t  mask_ = ~0;        // bit-depth crush
	int32_t  drive_ = 256;      // Q8 into the folder, 1x..8x
	int32_t  ring_ = 0;         // Q15 depth
	int32_t  env_ = 0;

	int      rotation_ = 0;
	bool     rotationChanged_ = false;
};

} // namespace lp
