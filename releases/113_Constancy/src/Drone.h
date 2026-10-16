#pragma once
#include <cstdint>
#include "Config.h"
#include "Params.h"
#include "Random.h"
#include "Tables.h"
#include "Voice.h"
#include "Wander.h"

namespace constancy
{

// The drone, on core 1, once a millisecond: root and fifth, two octaves
// below the melody, behind everything.
//
// Three saws: two on the root, a few cents apart and drifting (one leaning
// left, one right), and one on the fifth in the centre. The fifth is tuned
// pure -- exactly 3:2 above the root, rather than the equal-tempered fifth
// a keyboard plays, which is a shade flat of that and beats slowly against
// the root. Pure, it locks to the root and the drone stays still. It
// follows the root's centre, not its drift.
//
// It fades in at power-on, breathes very slowly, and on a reseed glides
// to the new root (whichever octave of it is nearer) while the melody is
// silent; the new key's notes wait for it to arrive.
class Drone
{
public:
	void Start(int32_t root)
	{
		pitch_ = target_ = (kDroneBottom + root) << 16;
		glideMsLeft_ = 0;
		fadeMs_ = 0;
	}

	void GlideTo(int32_t root)
	{
		int32_t note = kDroneBottom + root;
		int32_t now = pitch_ >> 16;
		if (note - now > 6)
			note -= 12;
		else if (now - note > 6)
			note += 12;
		target_ = note << 16;
		glideMsLeft_ = kDroneGlideMs;
		glideStep_ = (target_ - pitch_) / kDroneGlideMs;
	}

	void Tick(Random &rng, EngineParams &p, int32_t levelQ14)
	{
		if (glideMsLeft_ > 0)
		{
			pitch_ += glideStep_;
			if (--glideMsLeft_ == 0)
				pitch_ = target_;
		}

		// The two root saws, spread and drifting; the fifth, pure and still.
		for (int i = 0; i < 2; i++)
		{
			int32_t spread = (i == 0 ? -kDroneSpreadCents : kDroneSpreadCents) * 655;
			VoiceControl::SetSaw(p.drone[1 + i], PitchToInc(pitch_ + spread + drift_[i].Tick(rng, kDroneDriftCents)));
		}
		VoiceControl::SetSaw(p.drone[0], PitchToInc(pitch_) / 2 * 3);

		// Level: fade in at power-on, then a slow breath.
		if (fadeMs_ < kDroneFadeInMs)
			fadeMs_++;
		breathPhase_ += 0xFFFFFFFFu / kDroneBreathMs;
		int32_t breath = 4096 + ((Sine(breathPhase_) * kDroneBreathQ12) >> 15); // Q12
		int32_t fade = (fadeMs_ << 12) / kDroneFadeInMs;
		p.droneGain = (((levelQ14 * breath) >> 12) * fade) >> 12;
	}

	int32_t PitchQ16() const { return pitch_; }
	bool Gliding() const { return glideMsLeft_ > 0; }

private:
	int32_t pitch_ = kDroneBottom << 16, target_ = kDroneBottom << 16; // MIDI x 65536
	int32_t glideStep_ = 0, glideMsLeft_ = 0;
	int32_t fadeMs_ = 0;
	uint32_t breathPhase_ = 0;
	Drift drift_[2];
};

} // namespace constancy
