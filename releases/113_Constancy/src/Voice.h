#pragma once
#include <cstdint>
#include "Config.h"
#include "Envelope.h"
#include "Params.h"
#include "Random.h"
#include "Tables.h"
#include "Wander.h"

namespace constancy
{

// A note for a voice to play.
struct NoteOn
{
	int32_t note = 60;		   // MIDI
	int32_t velocityQ12 = 4096;
	EnvelopeTimes env;
	int32_t gateMs = -1;	   // hold this long, then release (< 0: until Release())
	int32_t glideMs = 0;	   // portamento from the last note (0: jump)
	bool retrigger = true;	   // restart the envelope (false: legato)
};

// Everything slow about one melodic voice, on core 1, once a millisecond:
// its pitch (with glide, vibrato, the ribbon-style fall, and each saw's
// detune and drift), its envelope and its pan. Tick() turns all of that
// into what core 0 needs: a phase step per saw and a gain per side.
class VoiceControl
{
public:
	void Seed(Random &rng)
	{
		for (Drift &d : drift_)
			d.Seed(rng);
	}

	// The lead's extras: more saws, delayed vibrato and the brightness swell.
	void SetLead()
	{
		expressive_ = true;
		saws_ = kLeadSaws;
	}

	void Play(const NoteOn &n)
	{
		note_ = n.note;
		target_ = n.note << 16;
		bool fromSilence = env_.Idle();
		if (n.glideMs > 0 && !fromSilence)
		{
			glideMsLeft_ = n.glideMs;
			glideStep_ = (target_ - pitch_) / n.glideMs;
		}
		else
		{
			glideMsLeft_ = 0;
			pitch_ = target_;
		}
		fallQ16_ = 0;
		fallMsLeft_ = 0;
		sinceNoteMs_ = 0;
		velocityQ12_ = n.velocityQ12;
		if (n.retrigger || fromSilence)
		{
			env_.Trigger(n.env, n.gateMs);
			sinceAttackMs_ = 0;
		}
		else
		{
			// Legato: the envelope carries on; this note sets when it ends.
			env_.SetGate(n.gateMs);
		}
	}

	void Release() { env_.Release(); }

	// Ease the pitch down by `semitonesQ12` over `ms`, from now.
	void Fall(int32_t semitonesQ12, int32_t ms)
	{
		fallMsLeft_ = ms;
		fallStep_ = -(semitonesQ12 << 4) / ms;
	}

	bool Silent() const { return env_.Idle(); }
	bool Gliding() const { return glideMsLeft_ > 0; }
	bool Releasing() const { return env_.Releasing(); }
	int32_t Note() const { return note_; }
	int32_t Level() const { return env_.Level(); }

	// The brightness swell after the attack, in octaves (Q12).
	int32_t SwellQ12() const
	{
		if (!expressive_)
			return 0;
		if (sinceAttackMs_ < kSwellRiseMs)
			return kSwellOctavesQ12 * sinceAttackMs_ / kSwellRiseMs;
		int32_t after = sinceAttackMs_ - kSwellRiseMs;
		if (after >= kSwellFallMs)
			return 0;
		return kSwellOctavesQ12 * (kSwellFallMs - after) / kSwellFallMs;
	}

	// One millisecond. `levelQ14` is the voice's level at full velocity.
	void Tick(Random &rng, VoiceParams &vp, int32_t levelQ14)
	{
		if (glideMsLeft_ > 0)
		{
			pitch_ += glideStep_;
			if (--glideMsLeft_ == 0)
				pitch_ = target_;
		}
		if (fallMsLeft_ > 0)
		{
			fallQ16_ += fallStep_;
			fallMsLeft_--;
		}
		if (sinceNoteMs_ < 1 << 30)
			sinceNoteMs_++;
		if (sinceAttackMs_ < 1 << 30)
			sinceAttackMs_++;

		// Delayed vibrato: none at first, then easing in as the note holds.
		int32_t vibrato = 0;
		if (expressive_ && sinceNoteMs_ > kVibratoDelayMs)
		{
			int32_t in = sinceNoteMs_ - kVibratoDelayMs;
			int32_t depth = in >= kVibratoRiseMs ? 4096 : (in << 12) / kVibratoRiseMs; // Q12
			vibPhase_ += (uint32_t)(kVibratoHzQ12 * 1049);
			vibrato = (((Sine(vibPhase_) * kVibratoCents * 655) >> 15) * depth) >> 12;
		}

		int32_t level = env_.Tick(); // Q15
		int32_t gain = (((level * levelQ14) >> 15) * velocityQ12_) >> 12;
		int32_t pan = (pan_.Tick(rng, 4000) * kPanWanderQ12) >> 12;
		vp.gainL = (gain * (4096 - pan)) >> 12;
		vp.gainR = (gain * (4096 + pan)) >> 12;

		int32_t pitch = pitch_ + fallQ16_ + vibrato;
		for (int s = 0; s < saws_; s++)
		{
			int32_t p = pitch + kSawSpreadCents[s] * 655 + drift_[s].Tick(rng, kDriftCents);
			SetSaw(vp.saw[s], PitchToInc(p));
		}
	}

	// A saw's phase step, and the reciprocal its anti-aliasing needs.
	static void SetSaw(SawParams &s, uint32_t inc)
	{
		s.inc = inc;
		uint32_t coarse = inc >> 8;
		s.rinc = coarse ? (1u << 31) / coarse : 0;
	}

private:
	Envelope env_;
	Drift drift_[kLeadSaws];
	int saws_ = kSaws;
	Wander pan_;
	bool expressive_ = false;

	int32_t note_ = 60;
	int32_t pitch_ = 60 << 16, target_ = 60 << 16; // MIDI x 65536
	int32_t glideStep_ = 0, glideMsLeft_ = 0;
	int32_t fallQ16_ = 0, fallStep_ = 0, fallMsLeft_ = 0;
	int32_t sinceNoteMs_ = 0, sinceAttackMs_ = 1 << 30;
	uint32_t vibPhase_ = 0;
	int32_t velocityQ12_ = 4096;
};

} // namespace constancy
