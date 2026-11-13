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
// Gate mode: the loop records while the gate is high. Both edges matter
// here, and a gate can be shorter than the 20ms wait, so instead of
// holding one edge at a time (as RecordPulse does) this delays the gate
// itself by 20ms, a bit per sample, and finds its edges on the way out.
//
// The probe is handled the same way as before: a rise counts only if the
// jack is still patched when it comes out of the delay, so the burst after
// an unplug never starts a recording. A fall always counts: pulling the
// cable out of a high gate ends it. What goes in is the gate AND "patched",
// so once ComputerCard declares the jack empty the delayed gate is low,
// whatever the probe is doing to the jack.
class RecordGate
{
public:
	static constexpr int32_t kRise = 1, kFall = -1;

	// `high`: the jack's level now. `patched`: the jack has a cable in it,
	// as ComputerCard sees it now. `r`: the shared position in the 20ms
	// delay (0 .. kEventDelay - 1). Returns kRise, kFall or 0, 20ms late.
	int32_t Process(bool high, bool patched, int32_t r)
	{
		uint32_t &word = bits_[r >> 5];
		uint32_t bit = 1u << (r & 31);
		bool late = (word & bit) != 0;
		if (high && patched)
			word |= bit;
		else
			word &= ~bit;
		if (late == last_)
			return 0;
		last_ = late;
		if (!late)
			return kFall;
		return patched ? kRise : 0;
	}

private:
	bool last_ = false;
	uint32_t bits_[(kEventDelay + 31) / 32] = {}; // 20ms of the gate, a bit a sample
};

} // namespace tem
