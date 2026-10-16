#pragma once
#include <cstdint>
#include "Config.h"
#include "Envelope.h"
#include "Notes.h"
#include "Random.h"
#include "Voice.h"

namespace constancy
{

// The pad: three note loops, on core 1, once a millisecond. As in
// Equanimity, each loop owns one note and repeats it on its own random
// cycle, a little quieter each time, until it's done; loops of different
// lengths drift past each other and the overlaps make the harmony.
//
// Unlike Equanimity, loop n always plays voice n + 1 and nothing else
// does: a repeat only restarts its own note (swelling up from wherever its
// release has got to), and a new loop waits for its voice to fall silent
// before it starts. No stealing, no clipped tails.
//
// Every strike takes its envelope from the tide at that moment: a pluck
// at low water, a long swell at high tide.
class Loops
{
public:
	// Power-on: the first loop starts at once, the others a little later,
	// so the pad builds rather than arriving as a chord.
	void Start(Random &rng)
	{
		for (int i = 0; i < kLoopVoices; i++)
			loop_[i].restMs = i == 0 ? 0 : rng.Between(1500, 7000);
	}

	// One millisecond. `births`: whether new loops may begin (not while a
	// reseed is fading the old key out).
	void Tick(Random &rng, VoiceControl *voices, int32_t root, int32_t scale, int32_t tide, bool births)
	{
		for (int i = 0; i < kLoopVoices; i++)
		{
			Loop &l = loop_[i];
			VoiceControl &v = voices[1 + i];
			if (l.active)
			{
				if (--l.countdownMs <= 0)
					Strike(v, l, tide);
			}
			else if (v.Silent())
			{
				if (l.restMs > 0)
					l.restMs--;
				else if (births)
					Birth(rng, i, voices, root, scale, tide);
			}
		}
	}

	// A reseed: every loop ends now, with no further repeats, and its note
	// is let go to fade as it would.
	void Stop(VoiceControl *voices)
	{
		for (int i = 0; i < kLoopVoices; i++)
		{
			loop_[i].active = false;
			voices[1 + i].Release();
		}
	}

	// After a reseed, once the drone is on the new root: the new key's
	// loops start after short rests, the first almost at once.
	void Restart(Random &rng)
	{
		for (int i = 0; i < kLoopVoices; i++)
			loop_[i].restMs = i == 0 ? rng.Between(0, 400) : rng.Between(800, 4000);
	}

	// No loop is playing and every loop voice has fallen silent.
	bool Quiet(const VoiceControl *voices) const
	{
		for (int i = 0; i < kLoopVoices; i++)
		{
			if (loop_[i].active || !voices[1 + i].Silent())
				return false;
		}
		return true;
	}

	// For tests.
	bool Active(int i) const { return loop_[i].active; }
	int32_t Note(int i) const { return loop_[i].note; }
	uint32_t Strikes() const { return strikes_; }

private:
	struct Loop
	{
		bool active = false;
		int32_t note = 60;
		int32_t periodMs = 0;
		int32_t countdownMs = 0; // until the next strike
		int32_t repeatsLeft = 0; // strikes still to come, including the next
		int32_t levelQ12 = 0;	 // loudness of the next strike
		int32_t fadeQ12 = 4096;	 // each strike is this much quieter than the last
		int32_t restMs = 0;		 // after its voice falls silent, before a new loop
	};

	void Birth(Random &rng, int i, const VoiceControl *voices, int32_t root, int32_t scale, int32_t tide)
	{
		// Notes the other loops are sounding, so this one picks another.
		int32_t busy[kLoopVoices] = {};
		int busyCount = 0;
		for (int j = 0; j < kLoopVoices; j++)
		{
			if (j != i && (loop_[j].active || !voices[1 + j].Silent()))
				busy[busyCount++] = loop_[j].note;
		}
		Loop &l = loop_[i];
		l.note = LoopNote(rng, root, scale, tide, busy, busyCount);
		l.periodMs = rng.Between(kLoopPeriodMinMs, kLoopPeriodMaxMs);
		l.repeatsLeft = rng.Between(kRepeatsMin, kRepeatsMax);
		l.fadeQ12 = kRepeatFadeQ12[l.repeatsLeft];
		l.levelQ12 = rng.Between(kVelocityMinQ12, kVelocityMaxQ12);
		l.active = true;
		l.countdownMs = 0;
		l.restMs = rng.Between(kLoopRestMinMs, kLoopRestMaxMs); // for after it ends
	}

	void Strike(VoiceControl &v, Loop &l, int32_t tide)
	{
		NoteOn n;
		n.note = l.note;
		n.velocityQ12 = l.levelQ12;
		n.env = TideEnvelope(kLoopEnvelope, tide);
		n.gateMs = LogBlend(kLoopGateLowMs, kLoopGateHighMs, tide);
		v.Play(n);
		strikes_++;

		l.levelQ12 = (l.levelQ12 * l.fadeQ12) >> 12;
		l.countdownMs = l.periodMs;
		// That was the last one: the loop is done (its note rings on).
		if (--l.repeatsLeft <= 0)
			l.active = false;
	}

	Loop loop_[kLoopVoices];
	uint32_t strikes_ = 0;
};

} // namespace constancy
