#pragma once
#include <cstdint>
#include "Packed.h"
#include "Params.h"

namespace tem
{

// One micro looper, run by the sample clock: it records one sample per
// tick and plays one per tick, so a loop's length in samples is fixed but
// its length in seconds, and its pitch, follow the clock.
//
// Two taps make a loop. The first starts recording; the second stops it
// and the loop plays straight away, from where it started to where it
// stopped. A tap while playing starts a fresh recording over the old loop,
// which fades out (20ms). Two taps less than 300ms apart clear the loop
// instead.
//
//   empty  --tap-->  recording  --tap-->  playing  --tap-->  recording ...
//                        |                                   (old loop fades)
//                        +--tap within 300ms--> empty
//
// The read head is 16.16 fixed point in buffer samples (unsigned, so a loop
// can be longer than 32,768 samples), and moves by the speed zone's step
// every tick: +-1 is the speed it was recorded at, +-2 an
// octave up, +-1/2 an octave down (each sample is read twice, which suits
// the lo-fi character better than smoothing it over).
//
// Two places would click without a crossfade:
//
//  * The seam, where the loop's end runs back into its start. After the
//    stop tap the looper keeps recording for a few more milliseconds (the
//    "tail"), and the loop's first few milliseconds are blended from that
//    tail into the loop proper: the loop's first sample is the one that
//    followed its last, so the seam is continuous. It works the same in
//    reverse, at any speed, and the loop is exactly as long as it was
//    played. (Fading the loop's last 256 ticks into its first would shorten
//    every loop by up to 128ms at 2kHz.)
//
//    The tail is 5 to 11ms at every clock: 256 ticks at 48, 32 and 24kHz,
//    halving every two steps down to 16 ticks at 2kHz. A few milliseconds is
//    plenty to hide the join, and keeping it short matters: anything in the
//    tail is part of the seam, so a note cut off just after the stop tap
//    would click on every pass. (256 ticks at every clock would be 128ms at
//    2kHz.)
//
//  * A speed change. The old read head carries on at the old speed while
//    the new one starts from the same place, and the output fades between
//    them over 256 ticks, so a change of zone glides.
class Looper
{
public:
	static constexpr int32_t kEmpty = 0, kRecording = 1, kPlaying = 2;

	// 59KB, packed: 40,000 samples of loop (20 seconds at 2kHz) and the
	// seam's tail.
	static constexpr int32_t kSize = 40256;
	static constexpr int32_t kSeam = 256;	// the speed crossfade, and the longest seam, in ticks
	static constexpr int32_t kMaxLen = kSize - kSeam;
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
		wrapped_ = wrapPending_;
		wrapPending_ = false;
		if (wrapped_)
			passes_++;

		if (state_ == kRecording)
		{
			buf_.Write(w_, in);
			if (++w_ >= kMaxLen)
				Stop(clockStep); // the buffer is full: play what we have
		}
		else if (tail_ < seam_ && state_ == kPlaying)
		{
			buf_.Write(len_ + tail_, in);
			tail_++;
		}

		int32_t out = 0;
		if (state_ == kPlaying)
		{
			if (speed != head_.speed)
			{
				// A new speed zone: fade from the old head to a new one.
				ghost_ = head_;
				ghostLen_ = len_;
				ghostGain_ = kUnity;
				ghostStep_ = kUnity / kSeam;
				head_.speed = speed;
			}
			out = Sample((int32_t)(head_.pos >> 16));
			if (Advance(head_, len_))
				wrapPending_ = true;
			if (inGain_ < kUnity)
			{
				// Fading in the first time round, from silence.
				out = (out * inGain_) >> 16;
				inGain_ += kUnity / kSeam;
			}
		}
		if (ghostGain_ > 0)
		{
			int32_t i = (int32_t)(ghost_.pos >> 16);
			int32_t g = state_ == kPlaying ? Sample(i) : buf_.Read(i);
			Advance(ghost_, ghostLen_);
			if (state_ == kPlaying)
				out += ((g - out) * ghostGain_) >> 16; // crossfade
			else
				out = (g * ghostGain_) >> 16; // the old loop, fading out alone
			ghostGain_ -= ghostStep_;
		}
		return out;
	}

	int32_t State() const { return state_; }
	int32_t Pos() const { return state_ == kPlaying ? (int32_t)(head_.pos >> 16) : w_; }
	int32_t Len() const { return state_ == kPlaying ? len_ : w_; }
	uint32_t Starts() const { return starts_; }
	uint32_t Stops() const { return stops_; }
	// The last tick played the loop's first sample: playback began, or the
	// read head ran off one end and came back at the other.
	bool Wrapped() const { return wrapped_; }
	uint32_t Passes() const { return passes_; } // counts the loop's starts
	int32_t Peek(int32_t i) const { return buf_.Read(i); } // for tests

private:
	static constexpr int32_t kUnity = 65536;

	struct Head
	{
		uint32_t pos = 0;  // 16.16 buffer samples
		int32_t speed = 0; // 16.16 per tick, negative for reverse
	};

	void Start(uint32_t now, int32_t clockStep)
	{
		if (state_ == kPlaying)
			FadeOut(clockStep);
		state_ = kRecording;
		w_ = 0;
		tail_ = kSeam;
		startedAt_ = now;
		starts_++;
	}

	void Stop(int32_t clockStep)
	{
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
		head_.pos = speed_ >= 0 ? 0 : ((uint32_t)len_ << 16) - 1; // reverse starts at the end
		inGain_ = 0;
		wrapPending_ = true;
	}

	void Clear(int32_t clockStep)
	{
		if (state_ == kPlaying)
			FadeOut(clockStep);
		state_ = kEmpty;
		len_ = 0;
		tail_ = kSeam;
	}

	// Hand the playing loop to the ghost head, to fade out over 20ms.
	void FadeOut(int32_t clockStep)
	{
		// 65536 / (20ms of ticks) for each clock step.
		static constexpr int32_t kFadeStep[kClockSteps] = {68, 102, 137, 205, 273, 410, 546, 819, 1092, 1638};
		ghost_ = head_;
		ghostLen_ = len_;
		ghostGain_ = kUnity;
		ghostStep_ = kFadeStep[clockStep];
	}

	// The seam's length is 256 >> kSeamShift[clockStep] ticks: 5 to 11ms.
	static constexpr int32_t kSeamShift[kClockSteps] = {0, 0, 0, 1, 1, 2, 2, 3, 3, 4};

	// The loop's sample at `i`, with the seam blended in: over the seam's
	// first samples, from the tail (what followed the loop's end) into the
	// loop's own start. Only as far as the tail has been written: on the
	// first pass it's still being recorded.
	int32_t Sample(int32_t i) const
	{
		int32_t v = buf_.Read(i);
		if (i < tail_)
		{
			int32_t t = buf_.Read(len_ + i);
			v = t + (((v - t) * (i << seamShift_)) >> 8); // i / seam_ of the way into the loop
		}
		return v;
	}

	// Move a head one tick and wrap it at the loop's ends. True when it wrapped.
	static bool Advance(Head &h, int32_t len)
	{
		// At most 40000 x 65536: needs all 32 bits, so unsigned.
		uint32_t end = (uint32_t)len << 16;
		h.pos += (uint32_t)h.speed;
		if (h.pos < end)
			return false;
		// Off one end. Forward, it has run past the end. In reverse it has
		// gone below zero, which in unsigned arithmetic wraps round to a
		// value far above any loop's end (2^32 less a step), and adding the
		// length brings it back to the right place near the end.
		if (h.speed >= 0)
			h.pos -= end;
		else
			h.pos += end;
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
	Packed12<kSize> buf_;
};

} // namespace tem
