// tape.h — the one audio buffer, shared by Mode 4 (scrub) and Mode 7 (jog).
//
// Recording is a gesture, not a running loop: hold Down, and the take is
// exactly as long as you held it, up to kMaxLen. Both modes record and play
// the same buffer, so a take grabbed in one is playable in the other.
//
// 1.5s at 48kHz is 144KB of the RP2040's 256KB. It was 1.75s until the five
// effect modes arrived wanting a 40KB buffer of their own (fx.h); a quarter of
// a second off the longest take bought all five, and the take is a gesture
// whose length you set by holding the switch, so the cap is a ceiling rather
// than the point. Raise it back if the effects are ever dropped.

#pragma once
#include <cstdint>
#include "fastmath.h"

namespace lp {

class Tape
{
public:
	static constexpr uint32_t kMaxLen = 72000;   // 1.5s
	// 5.3ms — just enough to keep PositionQ8's len-2 and Mode 7's position
	// ramp sane. It used to be 50ms, to throw away accidental taps; recording
	// now only STARTS once a press has outlived kTapTicks, so every take that
	// begins is deliberate and there is nothing left to guard against.
	static constexpr uint32_t kMinLen = 256;

	void StartRecord()
	{
		write_ = 0;
		recording_ = true;
	}

	/// A take shorter than kMinLen is discarded and the previous one kept —
	/// though its first few ms have already been overwritten.
	void StopRecord()
	{
		recording_ = false;
		if (write_ >= kMinLen) len_ = write_;
	}

	bool Recording() const { return recording_; }
	bool HasAudio() const { return len_ >= kMinLen; }
	uint32_t Length() const { return len_; }

	/// Every sample while recording. Runs out at the end of the buffer
	/// rather than wrapping: the take is whatever fitted.
	void Write(int32_t s)
	{
		if (write_ < kMaxLen) buf_[write_++] = static_cast<int16_t>(s);
	}

	/// A read head that can be moved without clicking. A jump starts a short
	/// crossfade from where it was, which is what makes beat-repeat slicing
	/// and a restarted platter usable rather than a string of pops.
	struct Head
	{
		static constexpr int kFade = 128;   // 2.7ms

		uint32_t idx = 0;
		int32_t  frac = 0;
		uint32_t oldIdx = 0;
		int32_t  oldFrac = 0;
		int      fade = 0;

		void JumpTo(uint32_t newIdx)
		{
			if (newIdx == idx) return;
			oldIdx = idx;
			oldFrac = frac;
			fade = kFade;
			idx = newIdx;
			frac = 0;
		}
	};

	/// Advance a head by a Q16 rate and read it, crossfaded if it just jumped.
	int32_t ReadHead(Head &h, int32_t rateQ16) const
	{
		int32_t sig = Read(h.idx, h.frac);
		if (h.fade > 0)
		{
			int32_t old = Read(h.oldIdx, h.oldFrac);
			sig = old + (((sig - old) * (Head::kFade - h.fade)) >> 7);
			Advance(h.oldIdx, h.oldFrac, rateQ16);
			h.fade--;
		}
		Advance(h.idx, h.frac, rateQ16);
		return sig;
	}

	void Advance(uint32_t &idx, int32_t &frac, int32_t rateQ16) const
	{
		frac += rateQ16;
		int32_t whole = frac >> 16;        // floors, so a negative rate steps back
		frac &= 0xFFFF;
		int32_t next = static_cast<int32_t>(idx) + whole;
		int32_t len = static_cast<int32_t>(len_);
		if (len < 1) len = 1;
		while (next >= len) next -= len;
		while (next < 0) next += len;
		idx = static_cast<uint32_t>(next);
	}

	/// idx must be < Length(). frac is Q16 within the sample.
	int32_t Read(uint32_t idx, int32_t fracQ16) const
	{
		uint32_t i1 = idx + 1;
		if (i1 >= len_) i1 = 0;
		int32_t s0 = buf_[idx];
		int32_t s1 = buf_[i1];
		return s0 + (((s1 - s0) * fracQ16) >> 16);
	}

	/// Unipolar Q16 -> a position in the take, in Q8 samples. Split in two so
	/// there is no 64-bit multiply in the audio path: len * 65535 overflows
	/// int32 on its own. The caller slews this, then splits it with
	/// idx = q8 >> 8 and fracQ16 = (q8 & 0xFF) << 8.
	int32_t PositionQ8(int32_t u) const
	{
		int32_t span = static_cast<int32_t>(len_) - 2;
		return ((u >> 8) * span) + (((u & 0xFF) * span) >> 8);
	}

private:
	int16_t  buf_[kMaxLen];
	uint32_t len_ = 0;
	uint32_t write_ = 0;
	bool     recording_ = false;
};

extern Tape gTape;

} // namespace lp
