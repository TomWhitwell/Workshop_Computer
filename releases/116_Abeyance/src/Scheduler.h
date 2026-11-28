#pragma once
#include <cstdint>
#include "Config.h"
#include "Random.h"
#include "Score.h"
#include "Tables.h"

namespace ab
{

// When each loop plays, once per block on core 1.
//
// Each loop has a length, the time its next pass starts, a pan position and
// its notes in the current piece. Time is counted in samples as a 64-bit
// number: a 32-bit counter would wrap after about 25 hours.
//
// A new piece picks a root, one of the 9 scales (never the one playing), and
// a tempo from 0.7x to 1.4x; turns every loop's degrees into notes in the
// new key; re-cuts every loop to its original length / tempo x a random
// 0.88..1.12; and works out every loop's stereo position again from its new
// pitch. (Control fades the old piece out and silences its voices first.)
class Scheduler
{
public:
	struct Note
	{
		int32_t loop;
		int32_t layer; // kPiano / kChoir
		int32_t note;  // MIDI
	};

	// The original piece: D-flat major, tempo 1, the guide's lengths, the
	// loops coming in one by one.
	void Opening(int64_t now)
	{
		root_ = kOpeningRoot;
		scale_ = kMajor;
		tempoQ12_ = 4096;
		for (int i = 0; i < kLoops; i++)
		{
			LoopState &l = loop_[i];
			l.length = (int32_t)kLoopDefs[i].lengthCs * (kSampleRate / 100);
			l.nextPass = now + (int64_t)kLoopDefs[i].openMs * (kSampleRate / 1000);
			l.passLeft = 0;
		}
		Place();
	}

	void NewPiece(int64_t now, Random &rng)
	{
		root_ = kLowestRoot + rng.Below(kRoots);
		int32_t s = rng.Below(kScales - 1);
		scale_ = s >= scale_ ? s + 1 : s; // never the scale just playing
		// Log-uniform between 0.7 and 1.4 (an octave of tempo): as likely to
		// be a little slower as a little faster. 0.7 = 2867 / 4096.
		tempoQ12_ = (2867 * Exp2(rng.Below(4097))) >> 16;

		// Every loop re-cut, and brought in again at a random point. One of
		// them, chosen at random, plays at once, so a new piece is heard
		// straight away. (Whole-number maths throughout: core 1 never
		// touches the slow software floating point.)
		int32_t first = rng.Below(kLoops);
		for (int i = 0; i < kLoops; i++)
		{
			LoopState &l = loop_[i];
			// original / tempo x 0.88..1.12 (3604..4588 / 4096)
			int64_t len = ((int64_t)kLoopDefs[i].lengthCs * (kSampleRate / 100) * 4096) / tempoQ12_;
			l.length = (int32_t)((len * rng.Between(3604, 4588)) >> 12);
			// The rest come in somewhere in their first 60%, after 0.5s.
			int32_t offset = i == first ? kSampleRate / 4
										: kSampleRate / 2 + (int32_t)(((int64_t)l.length * 2458 * rng.Below(4096)) >> 24);
			l.nextPass = now + offset;
			l.passLeft = 0; // drop the rest of any phrase in progress
		}
		Place();
	}

	// One block starting at sample `now`: calls play(Note) for each note
	// that starts in it. Notes start on the block boundary (0.67ms grid).
	// driftQ12: the Drift setting (with CV In 2), 0..4096.
	template <class F>
	void Tick(int64_t now, int32_t driftQ12, Random &rng, F &&play)
	{
		int64_t end = now + kBlock;
		for (int i = 0; i < kLoops; i++)
		{
			LoopState &l = loop_[i];
			if (l.nextPass < end)
			{
				StartPass(i, driftQ12, rng);
				l.nextPass += l.length;
			}
			while (l.passLeft > 0 && l.nextNote < end)
			{
				play(Note{i, kLoopDefs[i].layer, l.pass[l.passCount - l.passLeft]});
				l.passLeft--;
				l.nextNote += l.spacing;
			}
		}
	}

	uint32_t Skips() const { return skips_; }			  // for tests
	uint32_t OctaveJumps() const { return octaveJumps_; } // for tests

	// Pan positions: -32768 (left) .. 32767 (right).
	int32_t Pan(int i) const { return loop_[i].pan; }
	int32_t TempoQ12() const { return tempoQ12_; }
	int32_t Root() const { return root_; }
	int32_t ScaleIndex() const { return scale_; }
	int32_t BaseNote(int i, int n) const { return loop_[i].base[n]; } // for tests
	int32_t Length(int i) const { return loop_[i].length; }			   // for tests

private:
	struct LoopState
	{
		int64_t nextPass = 0; // sample the next pass starts
		int64_t nextNote = 0; // sample the next note of this pass starts
		int32_t length = 0;	  // samples
		int32_t spacing = 0;  // samples between phrase notes
		int32_t passLeft = 0; // notes of this pass still to play
		int32_t passCount = 0; // notes in this pass
		int32_t pan = 0;
		int32_t base[3] = {0, 0, 0}; // this piece's notes
		int32_t pass[3] = {0, 0, 0}; // this pass's notes
	};

	// A new pass of loop i, with drift's changes. At full drift each pass
	// has a 30% chance of being skipped and a 20% chance of jumping an
	// octave; a piano phrase of two or three notes a 35% chance of playing
	// backwards and 35% of losing its last note. Lower drift scales every
	// chance down in proportion; at zero the loops play exactly as written.
	void StartPass(int i, int32_t driftQ12, Random &rng)
	{
		LoopState &l = loop_[i];
		const LoopDef &d = kLoopDefs[i];
		l.nextNote = l.nextPass;
		l.passCount = d.count;
		for (int n = 0; n < d.count; n++)
			l.pass[n] = l.base[n];
		l.passLeft = 0;

		if (rng.Chance((driftQ12 * kSkipQ12) >> 12))
		{
			skips_++;
			return; // a skipped pass: silence until the next
		}
		if (d.layer == kPiano && d.count > 1)
		{
			if (rng.Chance((driftQ12 * kReverseQ12) >> 12))
				for (int a = 0, b = d.count - 1; a < b; a++, b--)
				{
					int32_t t = l.pass[a];
					l.pass[a] = l.pass[b];
					l.pass[b] = t;
				}
			if (rng.Chance((driftQ12 * kShortenQ12) >> 12))
				l.passCount--;
		}
		if (rng.Chance((driftQ12 * kOctaveQ12) >> 12))
		{
			// Up or down at random; if that would leave the layer's range,
			// the other way; if both would, no jump.
			int32_t lo = d.layer == kPiano ? kPianoLow : kChoirLow;
			int32_t hi = d.layer == kPiano ? kPianoHigh : kChoirHigh;
			int32_t first = rng.Chance(2048) ? 12 : -12;
			const int32_t shifts[2] = {first, -first};
			for (int32_t shift : shifts)
			{
				bool fits = true;
				for (int n = 0; n < l.passCount; n++)
					fits = fits && l.pass[n] + shift >= lo && l.pass[n] + shift <= hi;
				if (fits)
				{
					for (int n = 0; n < l.passCount; n++)
						l.pass[n] += shift;
					octaveJumps_++;
					break;
				}
			}
		}
		l.passLeft = l.passCount;
	}

	// Drift's chances at full drift, Q12.
	static constexpr int32_t kSkipQ12 = 1229;	 // 30%
	static constexpr int32_t kOctaveQ12 = 819;	 // 20%
	static constexpr int32_t kReverseQ12 = 1434; // 35%
	static constexpr int32_t kShortenQ12 = 1434; // 35%
	uint32_t skips_ = 0, octaveJumps_ = 0;

	// The piece's notes, phrase spacing and pan positions.
	void Place()
	{
		const Scale &sc = kScaleDefs[scale_];
		int32_t pitch6[kLoops]; // each loop's average pitch, x 6
		int32_t lowest = 127, highest = 0;
		for (int i = 0; i < kLoops; i++)
		{
			LoopState &l = loop_[i];
			const LoopDef &d = kLoopDefs[i];
			int32_t sum = 0;
			for (int n = 0; n < d.count; n++)
			{
				int32_t note = DegreeToNote(d.degree[n], root_, sc);
				l.base[n] = note;
				sum += note;
				lowest = note < lowest ? note : lowest;
				highest = note > highest ? note : highest;
			}
			pitch6[i] = sum * 6 / d.count; // a phrase sits at its average
			l.spacing = (int32_t)(((int64_t)d.spacingMs * (kSampleRate / 1000) * 4096) / tempoQ12_);
		}
		Spread(pitch6, lowest * 6, highest * 6);
	}

	// Stereo spot by pitch: the lowest loop in the centre, higher loops
	// further out, alternating sides from low to high. Distance from the
	// centre is 0.9 x ((pitch - lowest note) / (highest note - lowest
	// note))^1.15, a curve fitted to the guide's table (every row within
	// 3%): the highest loop sits at 90% right or left, never hard over.
	void Spread(const int32_t pitch6[kLoops], int32_t lo, int32_t hi)
	{
		for (int i = 0; i < kLoops; i++)
		{
			// Rank: how many loops sit lower (ties go to the earlier loop).
			int rank = 0;
			for (int j = 0; j < kLoops; j++)
				if (pitch6[j] < pitch6[i] || (pitch6[j] == pitch6[i] && j < i))
					rank++;
			// Position along the range, 0..65536, then the curve's table.
			int32_t x = hi > lo ? (int32_t)(((int64_t)(pitch6[i] - lo) << 16) / (hi - lo)) : 0;
			int32_t k = x >> 10, f = x & 1023;
			int32_t amount = k >= 64 ? Tables::panCurve[64]
									 : Tables::panCurve[k] + (((Tables::panCurve[k + 1] - Tables::panCurve[k]) * f) >> 10);
			if (rank == 0)
				amount = 0;
			loop_[i].pan = (rank & 1) ? -amount : amount; // odd ranks left
		}
	}

	int32_t root_ = kOpeningRoot;
	int32_t scale_ = kMajor;
	int32_t tempoQ12_ = 4096;
	LoopState loop_[kLoops];
};

} // namespace ab
