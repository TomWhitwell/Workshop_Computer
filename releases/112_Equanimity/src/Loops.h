#pragma once
#include <climits>
#include <cstdint>
#include "Config.h"
#include "Notes.h"
#include "Params.h"
#include "Random.h"

namespace eq
{

// The melody: up to eight note loops, on core 1. Each loop repeats its own
// bell note on its own cycle, a little quieter each time, until it's done.
// Loops of different lengths drift past each other, and the overlaps make
// the melody -- the idea behind Eno and Chilvers' Bloom.
//
// Loop n always plays melody voice n. Every strike takes the tone selected
// at that moment, so a change of tone is heard from each loop's next repeat.
class Loops
{
public:
	// Once a millisecond: strike any loop whose cycle has come round.
	void Tick(EngineParams &p, const Tone &tone)
	{
		for (int i = 0; i < kVoices; i++)
		{
			if (freshMs_[i] > 0)
				freshMs_[i]--;
			Loop &l = loop_[i];
			if (!l.active || --l.countdownMs > 0)
				continue;
			Strike(p, i, tone);
		}
	}

	// A new note loop, struck straight away, in `scale` on `root`. `tide`
	// (Q12) sets how bright it will be for its whole life. `voiceAmp` is how
	// loud each voice is right now, from core 0.
	void Birth(EngineParams &p, Random &rng, int32_t root, int32_t scale, const Tone &tone, int32_t tide,
			   const int32_t *voiceAmp)
	{
		int slot = ChooseSlot(voiceAmp);
		Loop &l = loop_[slot];
		l.note = ChooseNote(rng, root, scale);
		l.periodMs = rng.Between(kLoopPeriodMinMs, kLoopPeriodMaxMs);
		l.repeatsLeft = rng.Between(kRepeatsMin, kRepeatsMax);
		l.fadeQ12 = kRepeatFadeQ12[l.repeatsLeft];
		l.levelQ12 = rng.Between(kVelocityMinQ12, kVelocityMaxQ12);
		l.tideQ12 = tide;
		l.old = false;
		l.active = true;
		oldVoices_ &= ~(1u << slot); // this voice belongs to the new set now
		Strike(p, slot, tone);
	}

	// Pulse In 2: let the current set go. Each loop plays at most two more
	// repeats, fading faster and no more than 9s apart, then ends. (Births are paused meanwhile; see
	// Control.)
	void BeginNewSet()
	{
		fading_ = true;
		for (int i = 0; i < kVoices; i++)
		{
			Loop &l = loop_[i];
			oldVoices_ |= 1u << i;
			if (!l.active)
				continue;
			l.old = true;
			if (l.repeatsLeft > kNewSetRepeats)
				l.repeatsLeft = kNewSetRepeats;
			if (l.periodMs > kNewSetPeriodMs)
				l.periodMs = kNewSetPeriodMs;
			if (l.countdownMs > l.periodMs)
				l.countdownMs = l.periodMs;
			l.fadeQ12 = (l.fadeQ12 * l.fadeQ12) >> 12;
			l.levelQ12 = (l.levelQ12 * l.fadeQ12) >> 12;
		}
	}

	bool Fading() const { return fading_; }

	// Once the old set's loops have ended and its notes have rung out:
	// returns true (once), and the fade is over.
	bool OldSetDone(const int32_t *voiceAmp)
	{
		if (!fading_)
			return false;
		for (int i = 0; i < kVoices; i++)
		{
			if (loop_[i].active && loop_[i].old)
				return false;
			if ((oldVoices_ >> i) & 1)
			{
				// Just struck: core 0 may not have started the note yet, so
				// its loudness still reads zero. Count it as busy.
				if (voiceAmp[i] != 0 || freshMs_[i] > 0)
					return false;
				oldVoices_ &= ~(1u << i);
			}
		}
		fading_ = false;
		return true;
	}

	int32_t ActiveCount() const
	{
		int32_t n = 0;
		for (const Loop &l : loop_)
			n += l.active ? 1 : 0;
		return n;
	}

	// For tests.
	bool Active(int i) const { return loop_[i].active; }
	int32_t Note(int i) const { return loop_[i].note; }
	int32_t Period(int i) const { return loop_[i].periodMs; }
	// Strikes since power-on: each one sends a trigger from Pulse Out 1.
	uint32_t Strikes() const { return strikes_; }

private:
	struct Loop
	{
		bool active = false;
		bool old = false; // belongs to the set that Pulse In 2 is fading out
		int32_t note = 60;
		int32_t periodMs = 0;
		int32_t countdownMs = 0; // until the next strike
		int32_t repeatsLeft = 0; // strikes still to come, including the next
		int32_t levelQ12 = 0;	 // loudness of the next strike
		int32_t fadeQ12 = 4096;	 // each strike is this much quieter than the last
		int32_t tideQ12 = 0;	 // the tide when this note was born
	};

	void Strike(EngineParams &p, int i, const Tone &tone)
	{
		Loop &l = loop_[i];
		VoiceCmd c = MakeStrike(l.note, l.levelQ12, l.tideQ12, tone);
		c.seq = p.voice[i].seq + 1;
		p.voice[i] = c;
		p.strikeEpoch++;
		strikes_++;
		freshMs_[i] = kFreshMs;

		l.levelQ12 = (l.levelQ12 * l.fadeQ12) >> 12;
		l.countdownMs = l.periodMs;
		// That was the last one: the slot is free again (the note itself
		// rings on, near silent by now).
		if (--l.repeatsLeft <= 0)
			l.active = false;
	}

	// A free loop if there is one -- the one whose bell has died away the
	// most. If all eight are busy, retire the quietest: the one whose next
	// repeat would be softest, which is nearest the end of its life anyway.
	int ChooseSlot(const int32_t *voiceAmp) const
	{
		// (A voice struck in the last few ms may still read silent: treat it
		// as loud, so its last repeat isn't cut off.)
		auto loudness = [&](int i) { return freshMs_[i] > 0 ? INT32_MAX : voiceAmp[i]; };
		int best = -1;
		for (int i = 0; i < kVoices; i++)
		{
			if (!loop_[i].active && (best < 0 || loudness(i) < loudness(best)))
				best = i;
		}
		if (best >= 0)
			return best;
		best = 0;
		for (int i = 1; i < kVoices; i++)
		{
			if (loop_[i].levelQ12 < loop_[best].levelQ12)
				best = i;
		}
		return best;
	}

	// A note of the scale, from the root in octave 4 to the root in octave
	// 6. Tries once more if another loop is already playing it, so the
	// melody doesn't pile up on one pitch.
	int32_t ChooseNote(Random &rng, int32_t root, int32_t scale) const
	{
		int32_t notes[kMelodySpan + 1];
		int32_t count = 0;
		for (int32_t n = kMelodyBottom + root; n <= kMelodyBottom + root + kMelodySpan; n++)
		{
			if (InScale(scale, root, n))
				notes[count++] = n;
		}
		int32_t note = notes[rng.Below(count)];
		for (const Loop &l : loop_)
		{
			if (l.active && l.note == note)
				return notes[rng.Below(count)];
		}
		return note;
	}

	// How long a voice counts as sounding after a strike, whatever core 0
	// reports: long enough for core 0 to pick the strike up and report back.
	static constexpr int32_t kFreshMs = 10;

	Loop loop_[kVoices];
	int32_t freshMs_[kVoices] = {};
	uint32_t strikes_ = 0;
	bool fading_ = false;
	uint32_t oldVoices_ = 0; // voices of the old set still ringing
};

} // namespace eq
