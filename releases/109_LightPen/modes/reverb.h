// Mode 11 — Reverb.
//
// Eight damped combs in parallel into four allpass diffusers in series — the
// Freeverb topology, with its delay lengths scaled from 44.1kHz to 48kHz and
// kept mutually prime-ish so the tank does not ring on one pitch. It fits on
// this card only because Modes 10-14 share one buffer (fx.h).
//
//   Red   = size, a small bright box up to a ~2.4s hall
//   Green = brightness of the tail, dark and roomy up to bright and metallic
//   Blue  = dry/wet
//
// Main is pre-delay, 0 to 85ms: the gap before the tail arrives, which is what
// separates a voice from its own reverb and makes a big setting readable.
//
// All three rest states are deliberately the useful end of nothing: with the
// wand in the dark it is a small, dark, entirely dry room, so arriving in the
// mode passes your input through rather than drowning it.
//
// CV Out 2 is an envelope follower on the output. Down taps through rotations.

#pragma once
#include "engine.h"
#include "fx.h"

namespace lp {

class ReverbMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;

private:
	static constexpr int kCombs = 8;
	static constexpr int kAps   = 4;

	// Freeverb's lengths at 44.1kHz, scaled by 48/44.1. Still coprime enough
	// that the tank's modes do not pile up on one frequency.
	static constexpr uint32_t kCombLen[kCombs] =
		{ 1214, 1293, 1390, 1476, 1548, 1623, 1694, 1760 };   // 11998
	static constexpr uint32_t kApLen[kAps] = { 605, 480, 371, 245 };   // 1701
	static constexpr uint32_t kPreLen = 4096;                          // 85ms

	// Feedback tops out at 0.92, not 0.98. Above that the parallel combs sum to
	// more than full scale for a sustained input and the tail clips; 0.92 still
	// gives a ~2.4s tail and keeps the loudest setting about 2dB under the dry
	// signal. See tools/lpsim.py, which measures both.
	static constexpr int32_t kFbMin = 22938;   // 0.70
	static constexpr int32_t kFbMax = 30146;   // 0.92

	Comb     comb_[kCombs];
	Allpass  ap_[kAps];
	Line     pre_;
	int32_t  preTap_ = 0;
	int32_t  mix_ = 0;
	int32_t  env_ = 0;
	uint32_t clearPos_ = 0;

	int      rotation_ = 0;
	bool     rotationChanged_ = false;
};

} // namespace lp
