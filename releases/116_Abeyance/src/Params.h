#pragma once
#include <cstdint>
#include "Config.h"
#include "Plate.h"

namespace ab
{

// The card is split across the RP2040's two cores:
//
//   core 1 renders the voices -- 12 piano, 8 choir -- a block of 32 samples
//     at a time, and runs everything control-rate once per block: knobs,
//     the scheduler, LEDs, random CVs, pulse outs.
//   core 0 (ComputerCard's 48kHz audio interrupt, ~20us per sample) plays
//     those blocks back one sample at a time, adding the piano echo, the
//     reverb, the audio inputs and the hiss, and drives every jack.
//
// Blocks pass from core 1 to core 0 through a small queue (see Abeyance.h).
// Each block carries its own settings for core 0 -- gains, CV and pulse
// outs -- so core 0 never needs a second hand-over, and the settings always
// change exactly in step with the audio they belong to.

// One sample from core 1, already panned and at its layer's level. 16-bit
// scale (+/-32767 is full; the jacks are +/-2047).
struct Frame
{
	int16_t dryL, dryR;		// piano + choir
	int16_t pianoL, pianoR; // the piano alone, for the echo
	int16_t noiseL, noiseR; // hiss and crackle (Surface.h), added after the reverb
};

struct Block
{
	// Q12 gains for core 0's mix.
	int32_t dryGain = 0;  // reverb mix: equal-power crossfade, dry side
	int32_t wetGain = 0;  //   ...and the reverb side
	int32_t volume = 0;	  // master volume (squared curve already applied)
	int32_t lfoQ8[2] = {0, 0}; // the reverb's two slow swings, 1/256 samples
	// The reverb's shape (the Main knob's macro), as Plate::Shape takes it.
	int32_t plateDecayQ15 = Plate::kDecayQ15;
	int32_t plateDampQ15 = Plate::kDampQ15;
	int32_t plateOutQ10 = Plate::kOutQ10;
	int32_t cvMv[2] = {0, 0}; // CV Out 1 and 2, millivolts (calibrated)
	bool pulse[2] = {false, false}; // Pulse Out 1 (trigger) and 2 (gate)
	// Between pieces, with the output silent: core 0 empties the echo and
	// reverb instead of running them, so the new piece starts in a clean
	// room.
	bool flush = false;
	Frame frame[kBlock] = {};
};

// What core 0 reports about the panel. Core 0 writes these every sample;
// core 1 reads them once per block.
struct Status
{
	uint32_t samples;	   // samples since power-on
	int32_t knob[3];	   // Main, X, Y: 0..4095
	int32_t sw;			   // kSwitchDown / Middle / Up
	int32_t cv[2];		   // CV In 1 and 2, -2048..2047
	uint32_t pulseEdges;   // rising edges seen on Pulse In 1, ever
	bool pulseConnected;   // Pulse In 1 jack detection
	uint32_t noise;		   // the low bits of every reading, stirred: entropy
};

// Switch positions, numbered the same as ComputerCard's Switch enum.
constexpr int32_t kSwitchDown = 0;
constexpr int32_t kSwitchMiddle = 1;
constexpr int32_t kSwitchUp = 2;

} // namespace ab
