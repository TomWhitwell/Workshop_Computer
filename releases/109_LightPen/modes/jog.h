// Mode 7 — Jog Wheel.
//
// Same take as Mode 4, played the other way round. Instead of the wand
// placing the head, the loop runs and the wand sets its SPEED. A TAP of Down
// cycles how:
//
//   NUDGE    Rest is normal speed; the wand bends it either way, through a
//            standstill and into reverse if you push far enough.
//   PLATTER  No motor. Mid-grey is a true standstill and the hand drives it
//            entirely, like a palm on vinyl — silent unless you are moving.
//   BRAKE    Rest is normal speed, but covering the sensor ramps it to a halt
//            and uncovering spins it back up. Slowing is quicker than
//            starting, which is what sells it as a motor rather than a fader.
//
// HOLD Down instead to record a new take, as Mode 4.
// Main: how hard the wand pushes. Blue: the platter's weight, in all three.
// Red: low-pass cutoff.
// CV Out 2 is the play position, Pulse Out 1 fires at the loop start, and
// Pulse Out 2 is high while it runs backwards.

#pragma once
#include "engine.h"
#include "svf.h"

namespace lp {

class JogMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;
	void OnDownRelease(int ticks) override;

private:
	enum class Act : uint8_t { Nudge, Platter, Brake, kCount };

	static constexpr int32_t kMaxRate = 4 * kQ16One;   // +/-4x
	static constexpr int32_t kDeadZone = 2048;         // of +/-32768: rest is exactly 1x
	static constexpr int32_t kJogRes = 12000;
	static constexpr int32_t kBrakeThresh = 12000;     // ~18% light: covered
	static constexpr int32_t kMuteRate = 4096;         // 1/16x: below this, silence

	int32_t  rate_ = kQ16One;
	int32_t  rateTarget_ = kQ16One;
	int32_t  idx_ = 0;
	int32_t  frac_ = 0;
	uint8_t  inertia_ = 6;
	int      trig_ = 0;
	bool     wrapped_ = false;
	Act      act_ = Act::Nudge;
	bool     actChanged_ = false;
	bool     armed_ = false;
	Svf      svf_;
};

} // namespace lp
