// fx.h — the one buffer the five effect modes share, and the primitives they
// build out of it.
//
// Modes 10-14 are mutually exclusive: only the current mode's ticks run, so
// they can all carve the same 40KB rather than each owning a line. That is the
// only reason a reverb fits on this card at all next to Mode 4's 168KB take.
// Each mode lays out its own regions from offset 0 and must not exceed kFxLen;
// the layouts are asserted at compile time where they are declared.
//
// NOTHING HERE CLEARS THE BUFFER IN ONE GO. 40KB of stores is thousands of
// times the 20.8us sample budget, so a mode entering wipes the buffer over
// control ticks (FxClearChunk) and keeps its wet output muted until that
// finishes — about 13ms, which no one hears. Writing a fresh mode without that
// step gives you the previous mode's buffer as a burst of noise.

#pragma once
#include <cstdint>
#include "fastmath.h"

namespace lp {

/// 426ms at 48kHz. This is the card's RAM ceiling — see CLAUDE.md's budget
/// before growing it.
constexpr uint32_t kFxLen = 20480;
extern int16_t gFx[kFxLen];

/// How many samples a mode wipes per control tick while clearing. 1024 stores
/// is nothing against a control tick's ~128k cycles, and the whole buffer goes
/// in 20 ticks (13ms).
constexpr uint32_t kFxClearChunk = 1024;

/// Incremental wipe of the WHOLE shared buffer, whatever a mode has carved out
/// of it. Call from the control tick with the previous return until it reaches
/// kFxLen, and keep the wet path muted until then. Wiping everything is both
/// simpler and safer than each region clearing itself: a mode that grows a
/// region cannot forget to wipe the new part.
static inline uint32_t FxClearChunk(uint32_t from)
{
	uint32_t end = from + kFxClearChunk;
	if (end > kFxLen) end = kFxLen;
	for (uint32_t i = from; i < end; i++) gFx[i] = 0;
	return end;
}

/// A circular delay line over [offset, offset+len) of gFx.
///
/// Reads are taps into the PAST: Tap(0) is the newest sample written. The
/// fractional read is what lets a delay time be swept by hand without
/// stepping — and what makes the pitch bend as it moves, which is the whole
/// point of a hand-swept delay.
class Line
{
public:
	void Init(uint32_t offset, uint32_t len)
	{
		buf_ = gFx + offset;
		len_ = len;
		write_ = 0;
	}

	uint32_t Length() const { return len_; }

	void __attribute__((always_inline)) Write(int32_t s)
	{
		buf_[write_] = static_cast<int16_t>(clamp_i32(s, -32767, 32767));
		if (++write_ >= len_) write_ = 0;
	}

	/// `back` samples ago. 0 is the newest. Wraps, so any value is safe.
	int32_t __attribute__((always_inline)) Tap(uint32_t back) const
	{
		uint32_t i = write_ + len_ - 1 - (back % len_);
		if (i >= len_) i -= len_;
		return buf_[i];
	}

	/// Q16 samples ago, interpolated between the two neighbouring taps.
	int32_t __attribute__((always_inline)) TapQ16(int32_t backQ16) const
	{
		uint32_t whole = static_cast<uint32_t>(backQ16 >> 16);
		int32_t  frac  = backQ16 & 0xFFFF;
		int32_t s0 = Tap(whole);
		int32_t s1 = Tap(whole + 1);            // one sample further back
		return s0 + (((s1 - s0) * frac) >> 16);
	}

private:
	int16_t *buf_ = gFx;
	uint32_t len_ = 1;
	uint32_t write_ = 0;
};

/// One-pole low-pass, Q15 coefficient. No buffer. Used for the damping in a
/// reverb tank and the tone in a delay's feedback path.
class OnePole
{
public:
	void Reset() { z_ = 0; }
	/// dampQ15: 0 passes everything, near 32767 is almost all low-pass.
	void SetDamp(int32_t dampQ15) { d_ = dampQ15; }

	int32_t __attribute__((always_inline)) Process(int32_t x)
	{
		// z += (1-d)*(x - z), all Q15. |x| <= 32767 keeps the product in int32.
		z_ += ((x - z_) * (32768 - d_)) >> 15;
		return z_;
	}

private:
	int32_t z_ = 0;
	int32_t d_ = 0;
};

/// Freeverb's damped comb: the delayed output is low-passed before it goes
/// back in, so each pass through the tank loses its top end. That is what
/// makes a tail sound like a room rather than a ringing pipe.
class Comb
{
public:
	void Init(uint32_t offset, uint32_t len) { line_.Init(offset, len); n_ = len; }
	uint32_t Length() const { return n_; }
	void Reset() { lp_.Reset(); }

	void Set(int32_t fbQ15, int32_t dampQ15) { fb_ = fbQ15; lp_.SetDamp(dampQ15); }

	int32_t __attribute__((always_inline)) Process(int32_t x)
	{
		int32_t out = line_.Tap(n_ - 1);
		line_.Write(x + ((lp_.Process(out) * fb_) >> 15));
		return out;
	}

private:
	Line     line_;
	OnePole  lp_;
	uint32_t n_ = 1;
	int32_t  fb_ = 0;
};

/// Schroeder allpass, fixed coefficient. Diffuses without colouring, which is
/// what turns four combs' worth of discrete echoes into a wash.
class Allpass
{
public:
	void Init(uint32_t offset, uint32_t len) { line_.Init(offset, len); n_ = len; }
	uint32_t Length() const { return n_; }

	int32_t __attribute__((always_inline)) Process(int32_t x)
	{
		int32_t buf = line_.Tap(n_ - 1);
		line_.Write(x + ((buf * kG) >> 15));
		return buf - x;
	}

private:
	static constexpr int32_t kG = 16384;   // 0.5
	Line     line_;
	uint32_t n_ = 1;
};

/// Dry/wet in Q15, equal-gain (not equal-power): a delay or reverb at full wet
/// should be exactly the effect and nothing else, which a -3dB centre breaks.
static inline int32_t __attribute__((always_inline)) mix_q15(int32_t dry, int32_t wet, int32_t mixQ15)
{
	return dry + (((wet - dry) * mixQ15) >> 15);
}

} // namespace lp
