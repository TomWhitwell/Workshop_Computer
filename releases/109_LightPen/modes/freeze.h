// Mode 12 — Freeze.
//
// Audio In 1 runs continuously into the shared buffer, so the last 426ms is
// always there. Hold Down and the writing stops: what was in the buffer at that
// instant becomes the source for three granular voices, and you get a sustained
// pad out of a moment that has already gone.
//
//   Red   = grain size, 20ms to 400ms
//   Green = pitch, an octave down through unity at mid grey to an octave up
//   Blue  = density, two voices overlapping up to three
//
// Main is SCATTER: at the bottom the grains march forward through the buffer in
// order, which reads as a time-stretch of the captured moment; at the top each
// grain starts somewhere random in it, which reads as a cloud.
//
// FREEZING LATCHES. A hold toggles it on, another hold toggles it off, so you
// are not pinning the switch down with the hand you need for the wand. A TAP
// rotates the colour roles instead — the same tap-versus-hold split the
// recording modes use, and for the same reason.
//
// While frozen the output is entirely wet. Blending the live input back in
// sounds like nothing is happening, which is exactly how the first version of
// this mode failed.
//
// The three voices ALTERNATE between the two audio outs, each panned 3:1 rather
// than hard, so the pad is wide but still sums to mono. Every other mode sends
// the same signal to both outs; a granular cloud is the one place that width is
// worth more than the redundancy.
//
// Pulse Out 1 fires as each grain starts. CV Out 2 is an envelope follower.

#pragma once
#include "engine.h"
#include "fx.h"

namespace lp {

class FreezeMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;
	void OnDownRelease(int ticks) override;

private:
	static constexpr int     kVoices = 3;
	// 20ms, not 2ms. At 2ms a grain is 96 samples with a 24-sample ramp, which
	// is a click generator rather than a pad — and since it was the value a dark
	// wand asked for, it was what the mode sounded like at rest.
	static constexpr int32_t kMinGrain = 960;     // 20ms
	static constexpr int32_t kMaxGrain = 19200;   // 400ms
	// Pan weights, 3:1. Wide, and still mono-safe.
	static constexpr int32_t kNear = 24576;
	static constexpr int32_t kFar  = 8192;

	struct Grain
	{
		bool     active = false;
		bool     right = false;   // which out it leans towards
		uint32_t readIdx = 0;
		int32_t  readFrac = 0;
		int32_t  pos = 0;
		int32_t  dur = 1;
	};

	Grain    voice_[kVoices];
	uint32_t writeIdx_ = 0;
	uint32_t nextStart_ = 0;
	int32_t  dur_ = kMinGrain;
	int32_t  rateQ16_ = 65536;
	int32_t  spawnEvery_ = kMinGrain;
	int32_t  spawnCount_ = 0;
	int32_t  rampInv_ = 0;
	int32_t  ramp_ = 1;
	int32_t  scatter_ = 0;
	int32_t  env_ = 0;
	uint32_t clearPos_ = 0;
	uint32_t rng_ = 0x9E3779B9;
	int      trig_ = 0;
	bool     nextRight_ = false;

	bool     frozen_ = false;
	bool     armed_ = false;
	bool     toggled_ = false;   // this hold has already flipped frozen_
	int      rotation_ = 0;
	bool     rotationChanged_ = false;
};

} // namespace lp
