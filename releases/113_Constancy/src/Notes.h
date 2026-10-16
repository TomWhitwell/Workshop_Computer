#pragma once
#include <cstdint>
#include "Config.h"
#include "Random.h"

namespace constancy
{

// Keys and scales: Equanimity's system, unchanged. Each reseed picks a
// random root and one of these scales. None has Locrian's flat fifth or
// the whole-tone scale's missing fifth, so the drone's pure fifth always
// belongs.
constexpr int kScales = 6;

// Semitones above the root in each scale, as a bit mask (bit 0 = root,
// bit 11 = a semitone below the octave).
constexpr uint16_t kScaleMask[kScales] = {
	0b101010110101, // Major:            1 2 3 4 5 6 7
	0b010110101101, // Minor:            1 2 b3 4 5 b6 b7
	0b001010010101, // Major pentatonic: 1 2 3 5 6
	0b010010101001, // Minor pentatonic: 1 b3 4 5 b7
	0b011010101101, // Dorian:           1 2 b3 4 5 6 b7
	0b101011010101, // Lydian:           1 2 3 #4 5 6 7
};

// Is `note` in `scale` built on `root` (0 = C .. 11 = B)?
inline bool InScale(int32_t scale, int32_t root, int32_t note)
{
	return (kScaleMask[scale] >> ((note - root + 120) % 12)) & 1;
}

// Does `note` sit a semitone above one of the drone's notes -- the flat
// 2nd (above the root) or the flat 6th (above the fifth)? Those rub. Of
// the six scales only Minor has one (its flat 6th).
inline bool IsRub(int32_t root, int32_t note)
{
	int32_t interval = (note - root + 120) % 12;
	return interval == 1 || interval == 8;
}

// The notes of the scale from `bottom` up `span` semitones, into `notes`
// (room for span + 1). Returns how many.
inline int32_t ScaleNotes(int32_t root, int32_t scale, int32_t bottom, int32_t span, int32_t *notes)
{
	int32_t count = 0;
	for (int32_t n = bottom; n <= bottom + span; n++)
	{
		if (InScale(scale, root, n))
			notes[count++] = n;
	}
	return count;
}

// The notes of the scale across the melody's two octaves, from the root in
// octave 4. Returns how many (at most 25).
inline int32_t MelodyNotes(int32_t root, int32_t scale, int32_t notes[kMelodySpan + 1])
{
	return ScaleNotes(root, scale, kMelodyBottom + root, kMelodySpan, notes);
}

// A note for a loop: any note of the scale, but the rub notes come up a
// third as often as the others, and not at all on the long swells of high
// tide. Tries once more if another loop is already playing the note (or a
// rub note comes up), so the pad doesn't pile up on one pitch.
inline int32_t LoopNote(Random &rng, int32_t root, int32_t scale, int32_t tideQ12, const int32_t *busy, int busyCount)
{
	int32_t notes[kMelodySpan + 1];
	int32_t count = MelodyNotes(root, scale, notes);
	int32_t note = notes[rng.Below(count)];
	for (int attempt = 0; attempt < 4; attempt++)
	{
		bool clash = false;
		for (int i = 0; i < busyCount; i++)
			clash = clash || busy[i] == note;
		bool rub = IsRub(root, note) && (tideQ12 > kRubNoteTideMaxQ12 || !rng.Chance(kRubNoteChanceQ12));
		if (!clash && !rub)
			break;
		note = notes[rng.Below(count)];
	}
	// Still a rub on a long swell: the note a semitone below is the root or
	// the fifth, which every scale has.
	if (IsRub(root, note) && tideQ12 > kRubNoteTideMaxQ12)
		note--;
	return note;
}

} // namespace constancy
