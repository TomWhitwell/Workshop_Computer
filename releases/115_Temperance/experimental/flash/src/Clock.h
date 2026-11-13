/*
 * Copied from Temperance 0.1.0 by Matt Allison.
 * Copyright (c) 2026 Matt Allison. Original code: MIT license.
 * Source: releases/115_Temperance/src/Clock.h
 * Experimental derivative: 0.1.0-flash-exp.1, 2026-11-04.
 * See ../ATTRIBUTION.md and ../LICENSES/Temperance-MIT.txt.
 */
#pragma once
#include <cstdint>
#include "Params.h"

namespace tem
{

// ---- The sample clock (core 0, every sample) --------------------------------
//
// The loopers and the crusher run at this clock instead of at 48kHz: the
// loopers record and play one sample per tick, and between ticks they hold
// still. That staircase is the grit at the slow end, and slowing the clock
// also stretches both loops, so they drop in pitch together -- by a fifth,
// then a fourth, then a fifth... down the ten steps, so every move is
// consonant.
//
// A phase accumulator: each sample adds clock / 48000 of a full turn, and
// every time the phase wraps is a tick. (A turn is 2^31 rather than 2^32
// so 48kHz, one tick every sample, still fits in 32 bits.)
class SampleClock
{
public:
	// True when this sample is a clock tick.
	bool Process(int32_t step)
	{
		phase_ += kInc[step];
		if (phase_ < kTurn)
			return false;
		phase_ -= kTurn;
		return true;
	}

private:
	static constexpr uint32_t kTurn = 1u << 31;
	// kClockHz[step] / 48000 x 2^31.
	static constexpr uint32_t kInc[kClockSteps] = {2147483648u, 1431655765u, 1073741824u, 715827883u, 536870912u,
												   357913941u,	268435456u,	 178956971u,  134217728u, 89478485u};
	uint32_t phase_ = 0;
};

// ---- A record pulse on a Pulse in (core 0, every sample) ---------------------
//
// The normalisation probe: ComputerCard finds out whether a jack is patched
// by sending a pseudo-random pattern into it and watching whether it comes
// back. For about 11ms after a cable is pulled out, the jack reads that
// pattern -- a burst of rising edges -- before ComputerCard declares it
// empty. Acted on, each unplug would start or stop a recording.
//
// So an edge waits 20ms before it counts, and counts only if the jack is
// still patched by then. The loopers hear their input 20ms late to match
// (see Temperance.h), so the loop still starts on the edge itself. Edges
// during the wait are ignored: a probe burst is one event at most, and
// that one is dropped.
class RecordPulse
{
public:
    void Reset() { wait_ = 0; }
	// `rise`: a rising edge this sample. `patched`: the jack has a cable in
	// it, as ComputerCard sees it now. True when a confirmed edge is due.
	bool Process(bool rise, bool patched)
	{
		if (wait_ > 0)
			return --wait_ == 0 && patched;
		if (rise)
			wait_ = kEventDelay;
		return false;
	}

private:
	int32_t wait_ = 0;
};

// ---- A record gate on a Pulse in (core 0, every sample) ----------------------
//
// Matt Allison's delayed gate approach, adapted to pack both inputs in
// one ring. Delays both edges by 20ms, so gates shorter than that survive.
// Rises require a still-patched jack; falls always count so unplugging ends
// recording. Each stored input is gated by its patch status.
// Both jacks share the delay index. Packing their levels together avoids
// calculating the same ring position twice in the 48kHz interrupt.
class RecordGates
{
public:
    // Bits 0/1: rises on A/B. Bits 2/3: falls on A/B.
    uint32_t Process(bool highA, bool patchedA, bool highB, bool patchedB, int32_t r)
    {
        uint32_t &word = bits_[r >> 4];
        uint32_t shift = (r & 15) * 2;
        uint32_t late = (word >> shift) & 3u;
        uint32_t patched = uint32_t(patchedA) | (uint32_t(patchedB) << 1);
        uint32_t high = (uint32_t(highA) | (uint32_t(highB) << 1)) & patched;
        if (high != late) word ^= (late ^ high) << shift;
        uint32_t changed = late ^ last_;
        if (!changed) return 0;
        last_ = late;
        return (changed & late & patched) | ((changed & ~late) << 2);
    }

private:
    uint32_t last_ = 0;
    uint32_t bits_[(kEventDelay + 15) / 16] = {};
};

} // namespace tem
