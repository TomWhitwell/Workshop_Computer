// Mode 10 — Delay.
//
// Audio In 1 into a delay line the wand holds all three controls of:
//
//   Red   = time, 10ms to 416ms, exponentially
//   Green = feedback, one repeat up to a long sustain
//   Blue  = dry/wet
//
// The time is SLEWED at audio rate and read with interpolation, so sweeping it
// by hand bends the pitch of whatever is already in the line the way a tape
// delay does, rather than stepping to the new time. Sweeping red with green
// high is the dub gesture this mode exists for.
//
// Main is the tone of the feedback path: heavily damped at the bottom, so each
// repeat loses its top end and the tail darkens, through to undamped and
// digital at the top.
//
// Pulse Out 1 fires once per delay period — a clock at whatever time red is
// holding, so the rest of the rack can follow the hand. CV Out 2 is an
// envelope follower on the output. Down taps through the colour rotations.

#pragma once
#include "engine.h"
#include "fx.h"

namespace lp {

class DelayMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;

private:
	// The whole shared buffer, less a sample so the interpolator's further tap
	// can never reach past the write head and read what it just wrote.
	static constexpr uint32_t kLen     = kFxLen;
	static constexpr int32_t  kMinTime = 480;     // 10ms
	static constexpr int32_t  kMaxTime = kFxLen - 512;
	static constexpr int32_t  kMaxFb   = 32440;   // 0.99: sustains, but decays

	Line     line_;
	OnePole  tone_;
	int32_t  timeQ16_   = kMinTime * 65536;
	int32_t  targetQ16_ = kMinTime * 65536;
	int32_t  fb_  = 0;
	int32_t  mix_ = 0;
	int32_t  env_ = 0;
	int32_t  clockCount_ = 0;
	int      trig_ = 0;
	uint32_t clearPos_ = 0;

	int      rotation_ = 0;
	bool     rotationChanged_ = false;
};

} // namespace lp
