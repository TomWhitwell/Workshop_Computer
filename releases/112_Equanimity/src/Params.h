#pragma once
#include <cstdint>
#include "Config.h"

namespace eq
{

// The card is split across the RP2040's two cores:
//
//   core 0 (the audio interrupt, ~20us per sample): only audio-rate work --
//     the eight melody voices, the drone voice, the two delay lines, the
//     mix, the jacks.
//   core 1 (once a millisecond): everything slow -- knobs and switch, the
//     tide, note loops and births, the drone's moves, the triggers, the
//     wander, LEDs.
//
// Core 1 hands its results to core 0 in an EngineParams block, and core 0
// reports what it sees on the panel in a Status block. Equanimity.h shows
// how the two cores take turns so a block is never read half-written.

// One melody strike, worked out in full on core 1 so the audio interrupt
// never has to divide. "Envelope units": 1 << 24 is full scale.
struct VoiceCmd
{
	uint32_t seq = 0;	  // bumps on every strike of this voice
	uint32_t carInc = 0;  // carrier: the note itself (phase step per sample)
	uint32_t modInc = 0;  // modulator: the note x the tone's ratio
	int32_t peakAmp = 0;  // loudness at the top of the strike (envelope units)
	int32_t attackInc = 0; // how far the attack climbs per 8 samples
	int32_t peakIndex = 0; // brightness at the strike (envelope units)
	int32_t ampDecay = 0;	// per-8-sample decay, as a fraction of 2^20
	int32_t indexDecay = 0;
	int32_t lpCoef = 0; // one-pole lowpass after the voice, Q15
};

struct EngineParams
{
	// Bumped whenever any voice has a new strike, so core 0 checks one
	// number per sample, not eight.
	uint32_t strikeEpoch = 0;
	VoiceCmd voice[kVoices];

	bool running = false; // false for the first 100ms, while knobs settle

	int32_t dryGain = 4096; // Q12, equal-power crossfade
	int32_t wetGain = 0;	// Q12
	int32_t melodyGain = 0; // Q12, melody volume
	int32_t droneGain = 0;	// Q12, drone volume
	int32_t tremoloDepth = 0; // Q12, the vibraphone's tremolo (0 = off)

	int32_t shortFeedback = 0; // Q12
	int32_t longFeedback = 0;  // Q12
	// Delay of each read point (L, R) in line samples, Q12: the base time
	// plus tape wobble.
	int32_t shortDelay[2] = {kShortDelaySamples << 12, kShortDelaySamples << 12};
	int32_t longDelay[2] = {kLongDelaySamples << 12, kLongDelaySamples << 12};

	// The drone: four layers (see Config.h), each with its pitch, its level
	// (Q12, already including its slow volume cycle) and its brightness
	// (envelope units, as VoiceCmd). Pitches only change once the drone has
	// faded to silence.
	struct
	{
		uint32_t inc = 0;
		int32_t gain = 0;
		int32_t index = 0;
	} droneLayer[kDroneLayers];
	int32_t droneCutoff = 0;	  // filter: 2 x sin(pi x fc / 48000), Q15
	int32_t droneLevel = 0;		  // where the overall loudness is heading (envelope units)
	int32_t droneSwellStep = 1;	  // per sample, swelling in
	int32_t droneReleaseStep = 1; // per sample, fading out

	int32_t cv[2] = {0, 0};				// CV Out 1 and 2, CVOutPrecise units (19-bit)
	bool trigger[2] = {false, false};	// Pulse Out 1 and 2
};

// What core 0 reports to core 1. Core 0 writes these every sample.
struct Status
{
	uint32_t samples;			// samples since power-on: core 1's clock
	int32_t knob[3];			// Main, X, Y: 0..4095
	int32_t sw;					// kSwitchDown / Middle / Up
	int32_t cv[2];				// CV In 1 and 2, -2048..2047
	uint32_t pulseEdges[2];		// rising edges seen on Pulse In 1 and 2
	bool pulseConnected[2];		// jack detection
	int32_t voiceAmp[kVoices];	// each voice's current loudness (envelope units)
};

// Switch positions, numbered the same as ComputerCard's Switch enum.
constexpr int32_t kSwitchDown = 0;
constexpr int32_t kSwitchMiddle = 1;
constexpr int32_t kSwitchUp = 2;

} // namespace eq
