// Mode 6 — Hue-to-Pitch Quantizer ("Colour Organ").
//
// The wand's hue picks a note: two octaves of the chosen scale spread evenly
// around the colour wheel, so scanning a printed rainbow plays a scale.
// A note must hold for ~30ms before it counts, so a stripe is one note, not
// a flurry at its edges.
// CV Out 2: the note (calibrated 1V/oct). Pulse Out 1: trigger per new note.
// Pulse Out 2: high while a colour is seen. Audio outs: a simple organ voice.
// Main: scale (chromatic / major / minor / pentatonic, in quarters).
// Down: tap = transpose +1 semitone (wraps at an octave), hold 1s = back to C.

#pragma once
#include "engine.h"

namespace lp {

class HueOrganMode : public Engine
{
public:
	void OnEnter() override;
	void ControlTick(const SensorFrame &f, const Ctrl &c, EngineOut &out) override;
	void AudioTick(const SensorFrame &f, const Inputs &in, EngineOut &out) override;
	void OnDownRelease(int ticks) override;
	void OnDownHold() override;

	/// Hue of unipolar Q16 r/g/b as Q16 around the wheel (0 = red), or -1
	/// for grey. Public for tools/lpsim.py's mirror of it.
	static int32_t HueQ16(int32_t r, int32_t g, int32_t b);

private:
	static constexpr int kBaseNote = 48;   // C3

	int      scale_ = 1;
	int      transpose_ = 0;
	bool     scaleChanged_ = false;
	bool     synced_ = false;

	int      degree_ = -1;       // confirmed; -1 = none yet
	int      pending_ = -1;
	int      dwell_ = 0;
	int      trig_ = 0;

	uint32_t phase_ = 0;
	uint32_t inc_ = 0;
	int32_t  amp_ = 0;           // Q15
	int32_t  ampTarget_ = 0;
};

} // namespace lp
