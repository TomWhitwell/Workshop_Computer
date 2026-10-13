#pragma once
#include <cstdint>
#include "Config.h"
#include "Envelope.h"
#include "Notes.h"
#include "Random.h"
#include "Tables.h"
#include "Voice.h"

namespace constancy
{

// The lead (voice 0), on core 1, once a millisecond: phrases, not loops.
//
// A phrase is three to six notes, mostly moving by step through the scale,
// ending on a long held note; then silence. Some notes glide in from the
// last (portamento), vibrato eases in as a note holds, the brightness
// swells after each attack (the voice does both), and now and then the
// last note sinks slowly as it fades, like easing off the CS-80's ribbon.
//
// The sun calls it: before sunrise it never starts a phrase; after, the
// gaps between phrases shorten as the sun climbs. Scrub back to night and
// it stops mid-phrase, letting its note fade.
class Lead
{
public:
	// One millisecond. `sunUp` is how far the sun is above the horizon
	// (Q12 of the way to the top of the window), or < 0 for below it.
	// Returns true when a note starts (for Pulse Out 1).
	bool Tick(Random &rng, VoiceControl &v, int32_t root, int32_t scale, int32_t tide, int32_t sunUp, bool allowed)
	{
		if (state_ == kResting)
		{
			if (restMs_ > 0)
				restMs_--;
			bool begin = requested_;
			if (!begin && allowed && sunUp >= 0 && restMs_ == 0)
			{
				// On average one phrase per gap: long gaps just after
				// sunrise, shorter as the sun climbs.
				int32_t gap = LogBlend(kPhraseGapSunriseMs, kPhraseGapMorningMs, sunUp);
				begin = rng.Chance24((1 << 24) / gap);
			}
			if (!allowed)
				requested_ = false;
			// (A requested phrase waits for the last one's note to fade.)
			if (begin && allowed && v.Silent())
			{
				forced_ = requested_;
				requested_ = false;
				Plan(rng, root, scale);
				return Next(rng, v, tide);
			}
			return false;
		}

		// The sun has set again (Time scrubbed back): stop, and let the note
		// fade. (Not a phrase Pulse In 2 asked for.)
		if (sunUp < -kSunHysteresisQ12 && !forced_)
		{
			Stop(v);
			return false;
		}

		if (state_ == kPlaying)
		{
			if (--noteMs_ <= 0)
				return Next(rng, v, tide);
		}
		else if (state_ == kHolding)
		{
			if (noteMs_ > 0 && --noteMs_ == 0 && fall_)
				v.Fall(fallQ12_, kFallMs); // the release starts now; sink with it
			if (noteMs_ == 0 && v.Silent())
			{
				state_ = kResting;
				restMs_ = kPhraseRestMs;
			}
		}
		return false;
	}

	// Pulse In 2: a phrase now, whatever the sun is doing (if one isn't
	// already playing).
	void Request()
	{
		if (state_ == kResting)
			requested_ = true;
	}

	// End the phrase: no more notes, and the current one fades.
	void Stop(VoiceControl &v)
	{
		if (state_ == kResting)
			return;
		v.Release();
		state_ = kHolding;
		noteMs_ = 0;
		fall_ = false;
	}

	bool Resting() const { return state_ == kResting; }
	bool Holding() const { return state_ == kHolding; }
	uint32_t Phrases() const { return phrases_; }
	uint32_t Notes() const { return notes_; }

private:
	static constexpr int32_t kResting = 0, kPlaying = 1, kHolding = 2;
	// The sun must sink this far (Q12) below the horizon to stop a phrase,
	// so knob jitter at sunrise can't.
	static constexpr int32_t kSunHysteresisQ12 = 40;
	// The least silence after a phrase before the next.
	static constexpr int32_t kPhraseRestMs = 1500;

	// Choose the phrase: its notes, as positions in the scale.
	void Plan(Random &rng, int32_t root, int32_t scale)
	{
		phrases_++;
		count_ = MelodyNotes(root, scale, scaleNotes_);
		length_ = rng.Between(kPhraseNotesMin, kPhraseNotesMax);
		// Start in the upper middle of the range: the lead sings above the pad.
		int32_t pos = rng.Between(count_ * 2 / 5, count_ * 4 / 5);
		for (int32_t k = 0; k < length_; k++)
		{
			if (k > 0)
				pos = Step(rng, pos);
			// The flat 2nd and flat 6th rub against the drone: usually step
			// past them, and never end on one.
			bool last = k == length_ - 1;
			if (IsRub(root, scaleNotes_[pos]) && (last || !rng.Chance(kRubNoteChanceQ12)))
				pos = pos > 0 ? pos - 1 : pos + 1;
			phrase_[k] = pos;
		}
		index_ = 0;
		velocityQ12_ = rng.Between(3100, 4096);
		fall_ = rng.Chance(kFallChanceQ12);
		fallQ12_ = rng.Between(kFallMinQ12, kFallMaxQ12);
	}

	// One move through the scale: mostly a step, sometimes two or three,
	// leaning back towards the middle of the range.
	int32_t Step(Random &rng, int32_t pos) const
	{
		int32_t roll = rng.Below(4096);
		int32_t size = roll < kStepOneQ12 ? 1 : (roll < kStepOneQ12 + kStepTwoQ12 ? 2 : 3);
		int32_t middle = count_ / 2;
		int32_t downChance = pos > middle ? 2700 : (pos < middle ? 1400 : 2048);
		int32_t next = rng.Chance(downChance) ? pos - size : pos + size;
		if (next < 0)
			next = pos + size;
		if (next >= count_)
			next = pos - size;
		return next < 0 ? 0 : (next >= count_ ? count_ - 1 : next);
	}

	// Play the phrase's next note.
	bool Next(Random &rng, VoiceControl &v, int32_t tide)
	{
		bool last = index_ == length_ - 1;
		NoteOn n;
		n.note = scaleNotes_[phrase_[index_]];
		// The first note a little accented.
		n.velocityQ12 = index_ == 0 ? velocityQ12_ : (velocityQ12_ * 3700) >> 12;
		n.env = TideEnvelope(kLeadEnvelope, tide);
		noteMs_ = last ? rng.Between(kLeadHoldMinMs, kLeadHoldMaxMs) : rng.Between(kLeadNoteMinMs, kLeadNoteMaxMs);
		n.gateMs = last ? noteMs_ : -1;
		// Some notes glide in, legato; the rest are struck afresh.
		if (index_ > 0 && rng.Chance(kGlideChanceQ12))
		{
			n.glideMs = rng.Between(kGlideMinMs, kGlideMaxMs);
			n.retrigger = false;
		}
		v.Play(n);
		notes_++;
		index_++;
		state_ = last ? kHolding : kPlaying;
		return true;
	}

	int32_t state_ = kResting;
	int32_t restMs_ = 0;
	bool requested_ = false, forced_ = false;
	int32_t scaleNotes_[kMelodySpan + 1] = {};
	int32_t count_ = 0;
	int32_t phrase_[kPhraseNotesMax] = {};
	int32_t length_ = 0, index_ = 0;
	int32_t noteMs_ = 0;
	int32_t velocityQ12_ = 4096;
	bool fall_ = false;
	int32_t fallQ12_ = 0;
	uint32_t phrases_ = 0, notes_ = 0;
};

} // namespace constancy
