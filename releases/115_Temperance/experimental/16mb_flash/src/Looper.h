/*
 * Adapted from Temperance 0.1.0 by Matt Allison.
 * Copyright (c) 2026 Matt Allison. Original code: MIT license.
 * Source: releases/115_Temperance/src/Looper.h
 * Experimental derivative: 0.1.0-flash-exp.1, 2026-11-04.
 * See ../ATTRIBUTION.md and ../LICENSES/Temperance-MIT.txt.
 */
#pragma once
#include <cstdint>
#include "FlashStore.h"
#include "Params.h"

namespace tem
{

// Flash-backed experimental looper. Audio core only reads decoded RAM caches.
// Positions use half-sample units (the six speeds are exact multiples of 1/2),
// avoiding both the original 65536-sample limit and per-tick 64-bit arithmetic.
class Looper
{
public:
	static constexpr int32_t kEmpty = 0, kRecording = 1, kPlaying = 2;

    void Channel(unsigned channel) { channel_ = channel; }
    static constexpr int32_t kSeam = 256;
    static constexpr int32_t kMaxLen = temflash::kCapacity;
    static constexpr int32_t kMinLen = 2 * kSeam;

	// A record tap (the switch or this looper's Pulse in). `now` counts
	// samples, for the double tap; `clockStep` sets how many ticks the old
	// loop's 20ms fade lasts.
	void Tap(uint32_t now, int32_t clockStep)
	{
		if (state_ == kRecording)
		{
			if (now - startedAt_ < kDoubleTap)
				Clear(clockStep);
			else
				Stop(clockStep);
		}
		else
			Start(now, clockStep);
	}

	// One clock tick: record `in` (if recording), move the read heads at
	// `speed` (16.16 samples per tick) and return the loop's output.
	// `clockStep` sets the seam's length, if the buffer fills this tick.
	int32_t Tick(int32_t in, int32_t speed, int32_t clockStep)
	{
		speed_ = speed;
		// A wrap flagged on the last tick is reported now, as this tick
		// plays the loop's first sample.
        wrapped_ = false;

		if (state_ == kRecording)
		{
			if (!temflash::Record(channel_, in)) {
                Stop(clockStep);
                return 0;
            }
			if (++w_ >= kMaxLen)
				Stop(clockStep); // the buffer is full: play what we have
		}
		else if (tail_ < seam_ && state_ == kPlaying)
		{
			tailPcm_[tail_] = (int16_t)in;
			tail_++;
		}

        int32_t out = 0;
        if (state_ == kPlaying && temflash::Ready(channel_))
        {
            temflash::Request(channel_, 0, head_.pos / 2, speed, len_, true);
            // Pause until the first block arrives; don't skip the attack.
            if (!temflash::Available(channel_, 0, head_.pos / 2)) {
                (void)temflash::Read(channel_, 0, head_.pos / 2);
                return lastOut_;
            }
            wrapped_ = wrapPending_;
            wrapPending_ = false;
            if (wrapped_) ++passes_;
			if (speed != head_.speed)
			{
				// A new speed zone: fade from the old head to a new one.
				ghost_ = head_;
				ghostLen_ = len_;
				ghostGain_ = kUnity;
				ghostStep_ = kUnity / kSeam;
				head_.speed = speed;
			}
			out = Sample(head_.pos / 2, 0);
			if (Advance(head_, len_))
				wrapPending_ = true;
			if (inGain_ < kUnity)
			{
				// Fading in the first time round, from silence.
				out = (out * inGain_) >> 16;
				inGain_ += kUnity / kSeam;
			}
		}
        // Keep the second cache warm before a direction/speed crossfade.
        if (state_ == kPlaying && temflash::Ready(channel_)) {
            const Head &g = ghostGain_ > 0 ? ghost_ : head_;
            temflash::Request(channel_, 1, g.pos / 2, g.speed, len_, true);
        }
		if (ghostGain_ > 0)
		{
			int32_t i = ghost_.pos / 2;
			int32_t g = Sample(i, 1);
			Advance(ghost_, ghostLen_);
			if (state_ == kPlaying)
				out += ((g - out) * ghostGain_) >> 16; // crossfade
			else
				out = (g * ghostGain_) >> 16; // the old loop, fading out alone
			ghostGain_ -= ghostStep_;
		}
        if (restartGain_ > 0) {
            out += (restartValue_ * restartGain_) >> 16;
            restartGain_ -= restartStep_;
        }
        lastOut_ = out;
		return out;
	}

	int32_t State() const { return state_; }
	int32_t Pos() const { return state_ == kPlaying ? head_.pos / 2 : w_; }
	int32_t Len() const { return state_ == kPlaying ? len_ : w_; }
	uint32_t Starts() const { return starts_; }
	uint32_t Stops() const { return stops_; }
	// The last tick played the loop's first sample: playback began, or the
	// read head ran off one end and came back at the other.
	bool Wrapped() const { return wrapped_; }
	uint32_t Passes() const { return passes_; } // counts the loop's starts


private:
	static constexpr int32_t kUnity = 65536;

	struct Head
	{
		int32_t pos = 0;  // half-sample units
		int32_t speed = 0; // 16.16 per tick, negative for reverse
	};

	void Start(uint32_t now, int32_t clockStep)
	{
		if (state_ == kPlaying)
			FadeOut(clockStep);
        ghostGain_ = 0;
        temflash::Start(channel_);
		state_ = kRecording;
		w_ = 0;
		tail_ = kSeam;
		startedAt_ = now;
		starts_++;
	}

	void Stop(int32_t clockStep)
	{
        temflash::Stop(channel_);
		stops_++;
		if (w_ < kMinLen)
		{
			// Too short to loop (only possible if the clock moved a long way
			// mid-recording; a double tap clears before it gets here).
			state_ = kEmpty;
			return;
		}
		len_ = w_;
		seamShift_ = kSeamShift[clockStep];
		seam_ = kSeam >> seamShift_;
		tail_ = 0;
		state_ = kPlaying;
		head_.speed = speed_;
		head_.pos = speed_ >= 0 ? 0 : len_ * 2 - 1; // reverse starts at the end
		inGain_ = 0;
		wrapPending_ = true;
	}

	void Clear(int32_t clockStep)
	{
		if (state_ == kPlaying)
			FadeOut(clockStep);
        temflash::Stop(channel_);
        for (unsigned h = 0; h < 2; ++h)
            temflash::Request(channel_, h, 0, 1, 0, false);
        ghostGain_ = 0;
        wrapPending_ = false;
		state_ = kEmpty;
		len_ = 0;
		tail_ = kSeam;
	}

	// Re-recording reuses the same flash region. Fade its last output value
    // over 20ms instead of reading an old loop while its sectors are erased.
	void FadeOut(int32_t clockStep)
	{
		// 65536 / (20ms of ticks) for each clock step.
		static constexpr int32_t kFadeStep[kClockSteps] = {68, 102, 137, 205, 273, 410, 546, 819, 1092, 1638};
        restartValue_ = lastOut_;
        restartGain_ = kUnity;
        restartStep_ = kFadeStep[clockStep];
	}

	// The seam's length is 256 >> kSeamShift[clockStep] ticks: 5 to 11ms.
	static constexpr int32_t kSeamShift[kClockSteps] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4};

	// The loop's sample at `i`, with the seam blended in: over the seam's
	// first samples, from the tail (what followed the loop's end) into the
	// loop's own start. Only as far as the tail has been written: on the
	// first pass it's still being recorded.
	int32_t Sample(int32_t i, unsigned head) const
	{
		int32_t v = temflash::Read(channel_, head, i);
		if (i < tail_)
		{
			int32_t t = tailPcm_[i];
			v = t + (((v - t) * (i << seamShift_)) >> 8); // i / seam_ of the way into the loop
		}
		return v;
	}

	// Move a head one tick and wrap it at the loop's ends. True when it wrapped.
	static bool Advance(Head &h, int32_t len)
	{
        int32_t end = len * 2;
        h.pos += h.speed / 32768;
        if (h.pos >= 0 && h.pos < end) return false;
        if (h.pos >= end) h.pos -= end;
        else h.pos += end;
		return true;
	}

	// Small, hot state first (short offsets from `this`), the buffer last.
	int32_t state_ = kEmpty;
	Head head_;
	int32_t len_ = 0;
	int32_t w_ = 0;			// recording: the next sample to write
	int32_t tail_ = kSeam;	// tail samples written after the stop tap
	int32_t seam_ = kSeam;	// the tail's length, in ticks
	int32_t seamShift_ = 0;
	int32_t inGain_ = kUnity; // fade-in on the first pass, x 65536
	int32_t speed_ = 0;		// the speed zone, as of the last tick
	Head ghost_;			// the old read head, fading out
	int32_t ghostLen_ = 0;
	int32_t ghostGain_ = 0, ghostStep_ = 0;
	bool wrapped_ = false, wrapPending_ = false;
	uint32_t startedAt_ = 0;
	uint32_t starts_ = 0, stops_ = 0, passes_ = 0;
	unsigned channel_ = 0;
    int16_t tailPcm_[kSeam] = {};
    int32_t lastOut_ = 0, restartValue_ = 0, restartGain_ = 0, restartStep_ = 0;
};

} // namespace tem
