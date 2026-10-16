#pragma once
#include <cstdint>
#include "Config.h"
#include "Notes.h"
#include "Random.h"

namespace constancy
{

// A Turing machine, after Tom Whitwell's Music Thing Modular Turing Machine
// (and his Workshop Computer card of it): a loop of eight random bits read
// as a melody, which can be left to change, let slip now and then, or
// locked. On core 1, stepped by the clock on Pulse In 1; its notes go out
// of CV Out 2 (1V per octave) and its bits out of Pulse Out 2.
//
// Each step the playhead moves to the next bit. The eight bits read from
// the playhead onwards make a number, 0 to 255, which picks a note of the
// card's current key across two octaves up from the root. On the way, the
// bit under the playhead may flip: half the time with Main (on the Levels
// page) at the centre, which makes every step random; less often towards
// either end, where the loop slips only now and then; never in the last
// bit of travel, where it's locked. Fully clockwise the locked loop plays
// forwards, round and round; fully anticlockwise it plays as a pendulum,
// forwards then backwards (1-2-...-8-7-...-2-1-2...).
//
// The pattern is the bits, not the notes: a reseed changes the key, and the
// same pattern carries on in the new one from the next step.
class TuringMachine
{
public:
	void Seed(Random &rng) { bits_ = rng.Next() & 0xFF; }

	// One clock step. `knob` is Main on the Levels page (0..4095).
	void Step(Random &rng, int32_t knob)
	{
		// Move: clockwise of the centre forwards, anticlockwise as a pendulum.
		if (knob >= 2048)
		{
			pos_ = (pos_ + 1) & (kTuringSteps - 1);
			dir_ = 1;
		}
		else
		{
			int32_t next = pos_ + dir_;
			if (next >= kTuringSteps)
			{
				dir_ = -1;
				next = kTuringSteps - 2;
			}
			else if (next < 0)
			{
				dir_ = 1;
				next = 1;
			}
			pos_ = next;
		}
		// Maybe flip the bit under the playhead.
		if (rng.Chance(FlipChance(knob)))
			bits_ ^= 1u << pos_;
	}

	// How likely (Q12) a step is to flip its bit: 2048 (a coin toss: fully
	// random) at the centre, falling in a straight line to nothing within
	// kTuringLockZone of either end.
	static int32_t FlipChance(int32_t knob)
	{
		int32_t d = knob > 2048 ? knob - 2048 : 2048 - knob; // 0..2048
		constexpr int32_t kSpan = 2048 - kTuringLockZone;
		if (d >= kSpan)
			return 0;
		return 2048 - d * 2048 / kSpan;
	}

	// The eight bits from the playhead on, as a number 0..255.
	int32_t Value() const { return ((bits_ >> pos_) | (bits_ << (kTuringSteps - pos_))) & 0xFF; }

	// The bit under the playhead: Pulse Out 2 fires on a 1.
	bool Gate() const { return (bits_ >> pos_) & 1; }

	// This step's note (MIDI) in `scale` on `root`: two octaves from the
	// root in octave 3 (MIDI 48 + root, 1V below middle C's 0V and up).
	int32_t Note(int32_t root, int32_t scale) const
	{
		int32_t notes[kTuringSpan + 1];
		int32_t count = ScaleNotes(root, scale, kTuringBottom + root, kTuringSpan, notes);
		return notes[(Value() * count) >> 8];
	}

	int32_t Pos() const { return pos_; }
	uint32_t Bits() const { return bits_; }

private:
	uint32_t bits_ = 0;
	int32_t pos_ = 0;
	int32_t dir_ = 1;
};

} // namespace constancy
