/*
 * Copied from Temperance 0.1.0 by Matt Allison.
 * Copyright (c) 2026 Matt Allison. Original code: MIT license.
 * Source: releases/115_Temperance/src/Params.h
 * Experimental derivative: 0.1.0-flash-exp.1, 2026-11-04.
 * See ../ATTRIBUTION.md and ../LICENSES/Temperance-MIT.txt.
 */
#pragma once
#include <cstdint>

namespace tem
{

// The card is split across the RP2040's two cores:
//
//   core 0 (the audio interrupt, ~20us per sample): the clock, both
//     loopers, the crusher, the plate reverb and the dry mix.
//   core 1 (once a millisecond): everything slow -- the knobs, switch and
//     pickup, the CV ins, the Wash macro, the reverb's LFOs and the LEDs.
//     Every division lives here.
//
// Core 1 hands its results to core 0 in an EngineParams block, and core 0
// reports what it sees and plays in a Status block. Temperance.h shows how
// the two cores take turns so a block is never read half-written.

constexpr int32_t kTickSamples = 48;		 // core 1 updates once a millisecond
constexpr uint32_t kSettleSamples = 4800; // 100ms for the knobs to settle at power-on

// Switch positions, numbered the same as ComputerCard's Switch enum.
constexpr int32_t kSwitchDown = 0;
constexpr int32_t kSwitchMiddle = 1;
constexpr int32_t kSwitchUp = 2;

// ---- The clock
// Ten steps from 48kHz down to 2kHz (see Clock.h). Step 0 is the fastest.
constexpr int32_t kClockSteps = 10;
constexpr int32_t kClockHz[kClockSteps] = {48000, 32000, 24000, 16000, 12000, 8000, 6000, 4000, 3000, 2000};

// ---- The loopers
// Six speed zones per knob, left to right: reverse x2, x1, x1/2, then
// forward x1/2, x1, x2. In 16.16 buffer samples per clock tick.
constexpr int32_t kSpeedZones = 6;
constexpr int32_t kZoneSpeed[kSpeedZones] = {-131072, -65536, -32768, 32768, 65536, 131072};
constexpr int32_t kDefaultZone = 3; // forward at half speed: the middle of the knob

// Record taps act 20ms after they happen, and the loopers hear their
// input 20ms late to match, so a loop still starts and ends exactly where
// it was tapped. The wait lets the normalisation probe settle (see Clock.h).
constexpr int32_t kEventDelay = 20 * 48;

// Two taps closer than this clear the loop instead of stopping it.
constexpr uint32_t kDoubleTap = 300 * 48;

// Compile-time options from the brief, both off.
//
// kSmooth: a one-pole low-pass on the inputs, at half the clock rate, so
// slow clocks alias less. Off, because the aliasing is the sound.
constexpr bool kSmooth = false;
// kCrushFollowsClock: the crusher loses one bit for every two clock steps
// down, on top of the knob.
constexpr bool kCrushFollowsClock = false;

struct EngineParams
{
	bool running = false; // false for the first 100ms, while knobs settle

	int32_t clockStep = 0;					   // 0 (48kHz) .. 9 (2kHz)
	int32_t zone[2] = {kDefaultZone, kDefaultZone}; // Loop A, B speed zone
	uint32_t tapSerial = 0;					   // +1: the switch was tapped (both loops)
	bool gate = false;						   // Pulse ins record as gates, not triggers

	int32_t crushBits = 12; // 12 (clean) down to 4

	int32_t dryQ12 = 4096; // the dry input's level, 0 (loops only) .. 4096 (unity)

	// The CV outs and Pulse out 1's random triggers (see Ui.h).
	int32_t cvMv[2] = {0, 0}; // CV out 1 and 2, millivolts
	uint32_t pulseSerial = 0; // +1: fire Pulse out 1

	// The plate (Wash macro, see Ui.h)
	int32_t sendQ15 = 0;	// how much of the loops goes into the reverb
	int32_t decayQ15 = 0;	// tank loop gain
	int32_t dampQ15 = 0;	// the tank's low-pass coefficient: 1 - damping
	int32_t lfoQ8[2] = {0, 0}; // tank allpass modulation, 1/256 samples
};

// What core 0 reports to core 1 about one looper.
struct LoopStatus
{
	int32_t state; // Looper::kEmpty / kRecording / kPlaying
	int32_t pos;   // the read head, in buffer samples
	int32_t len;   // the loop's length, in buffer samples
	uint32_t starts; // counts recordings started
	uint32_t stops;	 // counts recordings stopped (playback started)
	uint32_t passes; // counts the loop's starts: playback, then every wrap
};

// What core 0 reports to core 1. Core 0 writes these every sample.
struct Status
{
	uint32_t samples; // samples since power-on: core 1's clock
	int32_t knob[3];  // Main, X, Y: 0..4095
	int32_t sw;		  // kSwitchDown / Middle / Up
	int32_t cv[2];	  // CV in 1 and 2: -2048..2047
	int32_t audio[2]; // what each looper hears: -2048..2047
	uint32_t clips[2]; // counts samples where Audio in 1 / 2 is at full scale
	LoopStatus loop[2];
};

} // namespace tem
