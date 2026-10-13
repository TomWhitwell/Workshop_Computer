#pragma once
#include <cstdint>
#include "Config.h"

namespace constancy
{

// The card is split across the RP2040's two cores:
//
//   core 0 (the audio interrupt, ~20us per sample): only audio-rate work --
//     the saws, the filters, the Icarus loop, Water, the mix, the jacks.
//   core 1 (once a millisecond): everything slow -- knobs and switch, the
//     morning's data, the notes, envelopes, pitch drift, CVs, LEDs.
//
// Core 1 hands its results to core 0 in an EngineParams block, and core 0
// reports what it sees on the panel in a Status block. Constancy.h shows
// how the two cores take turns so a block is never read half-written.

// Water's modes (see Water.h).
constexpr int32_t kWaterOpen = 0, kWaterLowpass = 1, kWaterHighpass = 2;

// One melodic voice: a phase step per saw, and the reciprocal each saw's
// anti-aliasing needs (worked out here so core 0 never divides).
struct SawParams
{
	uint32_t inc = 0;  // phase step per sample (2^32 is a whole cycle)
	uint32_t rinc = 0; // 2^31 / (inc >> 8)
};

struct VoiceParams
{
	SawParams saw[kLeadSaws]; // loops use the first kSaws
	// Envelope x pan x level, Q14, for each side. Core 0 slides to these
	// over the millisecond.
	int32_t gainL = 0, gainR = 0;
};

struct EngineParams
{
	bool running = false; // false for the first 100ms, while knobs settle

	VoiceParams voice[kVoices];
	// One-pole coefficients (Q15) of the melody's lowpass (the loops) and
	// the lead's own, which swells.
	int32_t loopsCoef = 32767;
	int32_t leadCoef = 32767;

	SawParams drone[kDroneSaws];
	int32_t droneGain = 0; // Q14, including the drone level
	int32_t droneCoef = 32767;

	// The Icarus loop.
	int32_t delayQ8 = (kDelaySunriseMs * 24) << 8; // read point, loop (24kHz) samples x 256
	int32_t feedbackQ12 = 0;
	int32_t driveQ12 = 5120;
	int32_t ladderCoef = 32767;
	int32_t ladderResQ12 = 0;
	int32_t destructQ15 = 32768; // the dropouts: 32768 = none, 16384 = half

	// The mix (Q12): equal-power dry/wet.
	int32_t dryGain = 4096;
	int32_t wetGain = 0;

	// Water: which filter is working, its coefficient (Q15), and how much of
	// it is heard (Q12; it fades in from the centre).
	int32_t waterMode = kWaterOpen;
	int32_t waterCoef = 32767;
	int32_t waterMixQ12 = 0;

	int32_t cv1 = 0;				  // CV Out 1, CVOutPrecise units (19-bit)
	int32_t turingNote = 60;		  // CV Out 2, as a MIDI note (60 is 0V)
	bool pulse[2] = {false, false};	  // Pulse Out 1 and 2
};

// What core 0 reports to core 1. Core 0 writes these every sample.
struct Status
{
	uint32_t samples;		// samples since power-on: core 1's clock
	int32_t knob[3];		// Main, X, Y: 0..4095
	int32_t sw;				// kSwitchDown / Middle / Up
	int32_t cv[2];			// CV In 1 and 2, -2048..2047
	uint32_t pulseEdges[2]; // rising edges seen on Pulse In 1 and 2
	bool pulseConnected[2]; // jack detection
};

// Switch positions, numbered the same as ComputerCard's Switch enum.
constexpr int32_t kSwitchDown = 0;
constexpr int32_t kSwitchMiddle = 1;
constexpr int32_t kSwitchUp = 2;

} // namespace constancy
