#pragma once
#include <cstdint>
#include "Config.h"
#include "Hook.h"
#include "Params.h"

namespace ab
{

// The knobs, the Z switch and Pulse In 1, once per block on core 1.
//
//   Z up        Main = master volume   X = drift        Y = tape
//   Z middle    Main = reverb mix      X = piano level  Y = choir level
//   Z down      (momentary) tap for a new piece; the knobs do nothing
//
// Six settings share three knobs, so each keeps its own stored value, with
// soft takeover (Hook.h): after a switch change a knob does nothing until it
// passes its stored value. They all start at the start-up values in
// Config.h -- not wherever the knobs happen to be at power-on -- so the
// card always starts the same way.
//
// CV In 1 adds to the reverb mix, CV In 2 to drift: the knob sets the
// centre, the CV moves around it.
class Panel
{
public:
	enum Setting
	{
		kVolume,
		kDrift,
		kTape,
		kReverb,
		kPiano,
		kChoir,
		kSettings
	};

	Panel()
	{
		const int32_t start[kSettings] = {kStartVolume, kStartDrift, kStartTape, kStartReverb, kStartPiano, kStartChoir};
		for (int i = 0; i < kSettings; i++)
		{
			hook_[i] = Hook(start[i]);
			smooth_[i] = start[i] << 8;
		}
	}

	// Once the knob readings have settled: every setting waits for its knob
	// to come round to the start-up value.
	void Start(const Status &s)
	{
		mode_ = s.sw == kSwitchUp ? kSwitchUp : kSwitchMiddle;
		for (int i = 0; i < kSettings; i++)
			hook_[i].Unhook(s.knob[i % 3]);
		pulseSeen_ = s.pulseEdges;
	}

	// One block. Returns true when a new piece has been asked for, by a
	// down tap or a confirmed Pulse In 1 edge.
	bool Tick(const Status &s)
	{
		bool newPiece = false;
		modeFlash_ = modeFlash_ > 0 ? modeFlash_ - 1 : 0;

		// The switch. Down is a spring-loaded tap: it fires the moment it's
		// pressed, then is locked out until the switch has been back out of
		// Down for 30ms, so bounce or a long hold can't fire twice.
		if (s.sw == kSwitchDown)
		{
			if (!tapLocked_)
			{
				newPiece = true;
				taps_++;
			}
			tapLocked_ = true;
			outOfDown_ = 0;
		}
		else
		{
			if (tapLocked_ && ++outOfDown_ >= kTapDebounceBlocks)
				tapLocked_ = false;
			if (s.sw != mode_)
			{
				// A new mode: its three settings wait for their knobs.
				mode_ = s.sw;
				int first = FirstOf(mode_);
				for (int k = 0; k < 3; k++)
					hook_[first + k].Unhook(s.knob[k]);
				modeFlash_ = kZFlashBlocks;
			}
			// The knobs belong to the current mode. (In Down they belong to
			// nothing, so a tap never nudges a setting.)
			int first = FirstOf(mode_);
			for (int k = 0; k < 3; k++)
				hook_[first + k].Update(s.knob[k]);
		}

		if (PulseConfirmed(s))
			newPiece = true;

		// What each setting is heading for: the knob's stored value plus
		// any CV, then a ~50ms glide to kill zipper noise.
		for (int i = 0; i < kSettings; i++)
		{
			int32_t target = hook_[i].Value();
			if (i == kReverb && s.cv[0] > 0)
				target += (s.cv[0] * kCvToSettingQ12) >> 12; // 0..5V: 0..100% added
			if (i == kDrift)
				target += (s.cv[1] * kCvToSettingQ12) >> 12; // +/-2.5V: +/-50%
			target = target < 0 ? 0 : (target > 4095 ? 4095 : target);
			// Q8 inside, so the glide can creep the last few steps.
			int32_t d = (target << 8) - smooth_[i];
			smooth_[i] += (int32_t)(((int64_t)d * kSettingGlideQ15) >> 15) + (d > 0 ? 1 : 0);
		}
		return newPiece;
	}

	// A setting, smoothed, 0..4095 (linear: the knob's own travel).
	int32_t Linear(Setting i) const { return smooth_[i] >> 8; }

	// The same with the squared curve, for finer control at low levels:
	// volume and the two layer levels.
	int32_t Squared(Setting i) const
	{
		int32_t v = Linear(i);
		return (v * v) >> 12;
	}

	// For the LEDs: the switch mode (kSwitchUp or kSwitchMiddle), how long
	// its row has left to light after a change, and whether any of its
	// knobs is still waiting to take over.
	int32_t Mode() const { return mode_; }
	int32_t ModeFlash() const { return modeFlash_; }
	bool Waiting() const
	{
		int first = FirstOf(mode_);
		return !hook_[first].Hooked() || !hook_[first + 1].Hooked() || !hook_[first + 2].Hooked();
	}

	uint32_t Taps() const { return taps_; } // for tests
	int32_t Stored(Setting i) const { return hook_[i].Value(); } // for tests

private:
	static int FirstOf(int32_t mode) { return mode == kSwitchUp ? kVolume : kReverb; }

	// Pulse In 1: an edge counts once it has waited 20ms with the jack still
	// patched (see kPulseConfirmBlocks). Equanimity's scheme, in blocks.
	bool PulseConfirmed(const Status &s)
	{
		uint32_t fresh = s.pulseEdges - pulseSeen_;
		pulseSeen_ = s.pulseEdges;
		if (!s.pulseConnected)
		{
			pulseWaiting_ = 0;
			return false;
		}
		if (fresh > 0 && pulseWaiting_ == 0)
			pulseWaiting_ = kPulseConfirmBlocks;
		if (pulseWaiting_ > 0 && --pulseWaiting_ == 0)
			return true;
		return false;
	}

	Hook hook_[kSettings];
	int32_t smooth_[kSettings]; // Q8
	int32_t mode_ = kSwitchMiddle;
	int32_t modeFlash_ = 0;
	bool tapLocked_ = false;
	int32_t outOfDown_ = 0;
	uint32_t taps_ = 0;
	uint32_t pulseSeen_ = 0;
	int32_t pulseWaiting_ = 0; // blocks until the pending edge counts (0: none)
};

} // namespace ab
