// Mode 2 — Triad Drone.
//
// Three oscillators, root/third/fifth. Red is the root's VCA, Green the
// third's, Blue the fifth's. Root is A2, offset by 1V/oct on Audio In 1
// (unused otherwise in this mode; unpatched reads ~0V).
// Main: shape morph sine -> triangle -> saw -> square.
// Down (tap): chord flavour Major -> Minor -> Sus2 -> Sus4.

#pragma once
#include "engine.h"
#include "osc.h"

namespace lp {

class TriadMode : public Engine
{
public:
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownPress() override;

private:
	static constexpr int kRootNote = 45;   // A2, 110Hz
	static constexpr int kFlavours = 4;

	Morph    morph_;
	uint32_t phase_[3] = { 0, 0, 0 };
	uint32_t inc_[3]   = { 0, 0, 0 };
	int32_t  pitchIn_  = 0;             // Audio In 1, smoothed, Q6
	int      flavour_  = 0;
	bool     flavourChanged_ = false;
};

} // namespace lp
