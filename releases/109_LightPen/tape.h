// tape.h — the one audio buffer, shared by Mode 4 (scrub) and Mode 7 (jog).
//
// Recording is a gesture, not a running loop: hold Down, and the take is
// exactly as long as you held it, up to kMaxLen. Both modes record and play the
// same buffer, so a take grabbed in one is playable in the other.
//
// SIX SECONDS in 144KB — the same 144KB that one and a half seconds of 16-bit
// 48kHz used to occupy. Two things buy the 4x, and both are deliberately
// lo-fi: this is a tape scrubber and a jog wheel, so cassette-grade is in
// character rather than a compromise.
//
//   * 24kHz. Pairs of incoming samples are averaged and stored as one. A
//     2-point average is not a brick wall — its response is |cos(pi f / 48k)|,
//     which nulls exactly at the new 12kHz Nyquist and is about -8dB at 18kHz —
//     so content up there folds down quietly rather than being removed. It
//     costs one add per sample, and the treble loss reads as tape.
//
//   * 8 bits, COMPANDED, not linear. Sign, 3-bit exponent, 4-bit mantissa, so
//     the step size tracks the signal: roughly 32dB of signal-to-noise at EVERY
//     level, rather than 53dB at full scale and 23dB thirty down. Linear 8-bit
//     sounds grainy on exactly the quiet tails a scrubber lingers over; this
//     does not. Encode and decode are both a handful of integer ops, no tables.
//
// Why not ADPCM, which would be another 2x: it is sequential. Mode 4 scrubs to
// arbitrary positions and Mode 7 plays backwards, and a differential codec
// cannot be entered at an arbitrary sample or run in reverse at all.
//
// RATES. Every mode still talks in "Q16 where 65536 is normal speed". The
// conversion to stored samples happens in here — Advance() and ReadHead() do it
// themselves, and StoreRate() is exposed for Mode 7, which runs its own
// accumulator so that it can wrap a signed position.

#pragma once
#include <cstdint>
#include "fastmath.h"

namespace lp {

class Tape
{
public:
	/// Input samples per stored sample.
	static constexpr int      kDecimShift = 1;
	static constexpr int32_t  kStoreRate  = kSampleRate >> kDecimShift;   // 24kHz
	static constexpr uint32_t kMaxLen     = 144000;                       // 6.0s
	// 10.7ms at the store rate. The comment here used to say this was only
	// about keeping PositionQ8's len-2 sane; it is not. Mode 4's SWEEP clamps
	// its loop end to [256, len], and with len below 256 that clamp returns a
	// loop end PAST the end of the take, reading stale bytes. The clamp there is
	// now written to cope on its own, so this value is a free choice again — but
	// do not lower it without re-reading that line.
	//
	// A press that outlives kTapTicks starts recording, which zeroes the write
	// head; if it then releases before kMinLen samples are down, the take is
	// discarded and the PREVIOUS one kept, with its first few ms already
	// overwritten. That window is 16 control ticks wide (about 11ms of press
	// duration, 171-182ms) and costs at most 10.7ms of the old take. Decimation
	// doubled it in time, since a tick now contributes 16 stored samples
	// rather than 32.
	static constexpr uint32_t kMinLen = 256;

	/// A rate relative to normal speed (Q16, 65536 = 1x) as an advance in
	/// STORED samples per output sample.
	static constexpr int32_t StoreRate(int32_t rateQ16) { return rateQ16 >> kDecimShift; }

	void StartRecord()
	{
		write_ = 0;
		acc_ = 0;
		accN_ = 0;
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

	/// Every sample while recording. Averages each pair down to one stored
	/// sample, then compands. Runs out at the end of the buffer rather than
	/// wrapping: the take is whatever fitted.
	void __attribute__((always_inline)) Write(int32_t s)
	{
		acc_ += s;
		if (++accN_ < (1 << kDecimShift)) return;
		int32_t avg = acc_ >> kDecimShift;
		acc_ = 0;
		accN_ = 0;
		if (write_ < kMaxLen) buf_[write_++] = Encode(avg);
	}

	/// Sign, 3-bit exponent, 4-bit mantissa. The bias of 16 is what gives the
	/// smallest segment somewhere to put zero.
	static uint8_t __attribute__((always_inline)) Encode(int32_t x)
	{
		uint32_t sign = 0;
		if (x < 0) { sign = 0x80; x = -x; }
		if (x > 2047) x = 2047;
		uint32_t v = static_cast<uint32_t>(x) + 16;   // 16..2063
		uint32_t e = 0;
		// At most 7 iterations, and only once per two samples while recording.
		while (v >= 32 && e < 7) { v >>= 1; e++; }
		return static_cast<uint8_t>(sign | (e << 4) | (v & 0x0F));
	}

	/// The exact inverse of Encode for every code it can emit.
	static int32_t __attribute__((always_inline)) Decode(uint8_t c)
	{
		int32_t m = static_cast<int32_t>(
			(static_cast<uint32_t>(c & 0x0F) | 0x10) << ((c >> 4) & 0x07)) - 16;
		return (c & 0x80) ? -m : m;
	}

	/// A read head that can be moved without clicking. A jump starts a short
	/// crossfade from where it was, which is what makes beat-repeat slicing and
	/// a restarted platter usable rather than a string of pops.
	struct Head
	{
		static constexpr int kFade = 128;   // 5.3ms at the store rate

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

	/// Advance a head at `rateQ16` (65536 = normal speed) and read it,
	/// crossfaded if it just jumped.
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

	/// `rateQ16` is relative to normal speed; the store rate is applied here.
	void Advance(uint32_t &idx, int32_t &frac, int32_t rateQ16) const
	{
		frac += StoreRate(rateQ16);
		int32_t whole = frac >> 16;        // floors, so a negative rate steps back
		frac &= 0xFFFF;
		int32_t next = static_cast<int32_t>(idx) + whole;
		int32_t len = static_cast<int32_t>(len_);
		if (len < 1) len = 1;
		while (next >= len) next -= len;
		while (next < 0) next += len;
		idx = static_cast<uint32_t>(next);
	}

	/// idx must be < Length(). frac is Q16 within the stored sample.
	int32_t Read(uint32_t idx, int32_t fracQ16) const
	{
		uint32_t i1 = idx + 1;
		if (i1 >= len_) i1 = 0;
		int32_t s0 = Decode(buf_[idx]);
		int32_t s1 = Decode(buf_[i1]);
		return s0 + (((s1 - s0) * fracQ16) >> 16);
	}

	/// Unipolar Q16 -> a position in the take, in Q8 STORED samples. Split in
	/// two so there is no 64-bit multiply in the audio path: len * 65535
	/// overflows int32 on its own. The caller slews this, then splits it with
	/// idx = q8 >> 8 and fracQ16 = (q8 & 0xFF) << 8.
	int32_t PositionQ8(int32_t u) const
	{
		int32_t span = static_cast<int32_t>(len_) - 2;
		return ((u >> 8) * span) + (((u & 0xFF) * span) >> 8);
	}

private:
	uint8_t  buf_[kMaxLen];
	uint32_t len_ = 0;
	uint32_t write_ = 0;
	int32_t  acc_ = 0;
	int      accN_ = 0;
	bool     recording_ = false;
};

extern Tape gTape;

} // namespace lp
