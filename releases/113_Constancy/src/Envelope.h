#pragma once
#include <cstdint>
#include "Config.h"
#include "Tables.h"

namespace constancy
{

// One envelope's times, worked out from the tide when a note starts.
struct EnvelopeTimes
{
	int32_t attackMs = 15;
	int32_t decayMs = 500;
	int32_t sustainRootQ15 = 32768; // the cube root of the sustain level
	int32_t releaseMs = 1000;
};

// Icarus's envelope, on core 1, once a millisecond: SuperCollider's ADSR
// with the 'cubed' curve.
//
// The envelope keeps its level as a cube root, moves that in straight
// lines, and cubes it on the way out. So the attack starts gently and
// arrives fast, and the release drops quickly at first and then trails off
// for a long time -- the shape of a sound dying away in a hall.
//
// A new note starts its attack from wherever the level is: a repeat during
// a release swells back up from there, never jumping to zero first.
class Envelope
{
public:
	// Start a note, holding it for gateMs and then releasing. gateMs < 0
	// holds it until Release().
	void Trigger(const EnvelopeTimes &t, int32_t gateMs)
	{
		times_ = t;
		gateMs_ = gateMs;
		stage_ = kAttack;
		step_ = 32768 / (t.attackMs > 0 ? t.attackMs : 1) + 1;
	}

	// Change when the current note ends (ms from now; < 0: hold) without
	// restarting it: for legato.
	void SetGate(int32_t gateMs) { gateMs_ = gateMs; }

	void Release()
	{
		if (stage_ == kIdle || stage_ == kRelease)
			return;
		stage_ = kRelease;
		step_ = root_ / (times_.releaseMs > 0 ? times_.releaseMs : 1) + 1;
	}

	// One millisecond. Returns the level, Q15.
	int32_t Tick()
	{
		if (gateMs_ > 0 && --gateMs_ == 0)
			Release();
		switch (stage_)
		{
		case kAttack:
			root_ += step_;
			if (root_ >= 32768)
			{
				root_ = 32768;
				stage_ = kDecay;
				step_ = (32768 - times_.sustainRootQ15) / (times_.decayMs > 0 ? times_.decayMs : 1) + 1;
			}
			break;
		case kDecay:
			root_ -= step_;
			if (root_ <= times_.sustainRootQ15)
			{
				root_ = times_.sustainRootQ15;
				stage_ = kSustain;
			}
			break;
		case kRelease:
			root_ -= step_;
			if (root_ <= 0)
			{
				root_ = 0;
				stage_ = kIdle;
			}
			break;
		default:
			break;
		}
		return Level();
	}

	int32_t Level() const
	{
		int32_t sq = (root_ * root_) >> 15;
		return (sq * root_) >> 15;
	}

	bool Idle() const { return stage_ == kIdle; }
	bool Releasing() const { return stage_ == kRelease; }

private:
	static constexpr int32_t kIdle = 0, kAttack = 1, kDecay = 2, kSustain = 3, kRelease = 4;
	int32_t stage_ = kIdle;
	int32_t root_ = 0; // Q15
	int32_t step_ = 0;
	int32_t gateMs_ = 0;
	EnvelopeTimes times_;
};

// The cube root of a level (Q12 in, Q15 out), by halving the search 15
// times. Core 1, once per note.
inline int32_t CubeRootQ15(int32_t levelQ12)
{
	int32_t target = levelQ12 << 3; // Q15
	int32_t lo = 0, hi = 32768;
	while (hi - lo > 1)
	{
		int32_t mid = (lo + hi) >> 1;
		int32_t cube = (((mid * mid) >> 15) * mid) >> 15;
		if (cube < target)
			lo = mid;
		else
			hi = mid;
	}
	return hi;
}

// The envelope for a note at this tide (Q12).
inline EnvelopeTimes TideEnvelope(const EnvelopeRange &r, int32_t tideQ12)
{
	EnvelopeTimes t;
	t.attackMs = LogBlend(r.attackLow, r.attackHigh, tideQ12) + kAttackFloorMs;
	t.decayMs = LogBlend(r.decayLow, r.decayHigh, tideQ12);
	int32_t sustain = r.sustainLowQ12 + (((r.sustainHighQ12 - r.sustainLowQ12) * tideQ12) >> 12);
	t.sustainRootQ15 = CubeRootQ15(sustain);
	t.releaseMs = LogBlend(r.releaseLow, r.releaseHigh, tideQ12);
	return t;
}

} // namespace constancy
