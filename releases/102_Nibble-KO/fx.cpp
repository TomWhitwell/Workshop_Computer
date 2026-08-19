// fx.cpp — the twelve performance effects.

#include "fx.h"
#include "pico.h"

namespace nko {

// Four rows of three, one family per shift. The diagonal is None because a
// button cannot shift itself.
const Fx kFxForGesture[kNumSingles][kNumSingles] = {
	//          tap A            tap B            tap C            tap D
	/* A */ { Fx::None,       Fx::LowPass,     Fx::HighPass,    Fx::BandSweep },
	/* B */ { Fx::Crush,      Fx::None,        Fx::Decimate,    Fx::Fold      },
	/* C */ { Fx::Stutter,    Fx::Flam,        Fx::None,        Fx::Gate      },
	/* D */ { Fx::Reverse,    Fx::TapeStop,    Fx::Silence,     Fx::None      },
};

void __not_in_flash_func(FxRack::SetSlot)(int slot, Fx f, int32_t depth)
{
	if (slot < 0 || slot >= kNumFxSlots) return;
	FxSlot &s = slot_[slot];

	// Clearing the state on a CHANGE, not every call. Filters carry charge and
	// the decimator holds a sample; leaving those alive across a switch makes
	// the new effect open with a click of the old one's residue.
	if (f != s.fx) { s.Clear(); s.fx = f; }
	s.depth = depth;

	if (f == Fx::None) active_ &= static_cast<uint8_t>(~(1u << slot));
	else               active_ |= static_cast<uint8_t>(1u << slot);

	// The filter coefficient, for the three effects that use one. A perceptual
	// taper — a linear sweep spends most of its travel where the ear hears
	// almost no change. Cubic-weighted, same shape as the DJ filter's.
	int32_t r = (depth << 15) >> 12;               // 0..32767
	int32_t q = (r * r) >> 15;
	int32_t c = (q * r) >> 15;
	s.g = 400 + ((r * 3000 + q * 8000 + c * 15000) >> 15);
	if (s.g < 200)   s.g = 200;
	if (s.g > 20000) s.g = 20000;
}

void FxRack::Clear()
{
	for (int i = 0; i < kNumFxSlots; i++)
	{
		slot_[i].Clear();
		slot_[i].fx = Fx::None;
	}
	active_ = 0;
}

FxTiming __not_in_flash_func(FxRack::Timing)() const
{
	switch (slot_[kFxRhythmSlot].fx)
	{
	case Fx::Stutter: return FxTiming::Stutter;
	case Fx::Flam:    return FxTiming::Flam;
	default:          return FxTiming::Normal;
	}
}

Fx __not_in_flash_func(FxRack::TriggerFx)() const
{
	const Fx f = slot_[kD].fx;
	return IsTriggerFx(f) ? f : Fx::None;
}

int32_t __not_in_flash_func(FxRack::Step)(int32_t in)
{
	if (active_ == 0) return in;

	// Series, in shift order, so the chain is deterministic rather than
	// depending on which effect was recorded first.
	//
	// Every slot is walked now, including C: it used to be skipped wholesale
	// because it held the transport effects, but its rhythmic set contains
	// GATE, which is an ordinary audio effect and has to be in the chain to
	// do anything. Effects that act elsewhere — stutter and flam at trigger
	// time, reverse and tape-stop when a voice starts — fall through
	// FxSlot::Step as pass-throughs, so no slot needs special-casing here.
	int32_t v = in;
	for (int i = 0; i < kNumFxSlots; i++)
		if (active_ & (1u << i)) v = slot_[i].Step(v);
	return v;
}

int32_t __not_in_flash_func(FxSlot::Step)(int32_t in)
{
	const int32_t depth_ = depth;
	switch (fx)
	{
	case Fx::None:
	default:
		return in;

	// --- A: filters ------------------------------------------------------
	//
	// One Chamberlin SVF read at three different outputs. The update order is
	// the corrected one from the DJ filter: high-pass from the PREVIOUS
	// states, integrate, then low-pass from the freshly updated band-pass.
	case Fx::LowPass:
	case Fx::HighPass:
	case Fx::BandSweep:
	{
		constexpr int32_t kRes = 14000;            // Q15, a mild emphasis
		int32_t hp = in - ((kRes * v1) >> 15) - v2;
		v1 += (g * hp) >> 15;
		int32_t lp = v2 + ((g * v1) >> 15);
		v2 = lp;

		// Clamp the states. An integer SVF at high g with resonance wraps
		// rather than merely getting loud, and a wrap is a full-scale square
		// wave, not a filter sound.
		constexpr int32_t kMax = 1 << 20;
		if (v1 >  kMax) v1 =  kMax;
		if (v1 < -kMax) v1 = -kMax;
		if (v2 >  kMax) v2 =  kMax;
		if (v2 < -kMax) v2 = -kMax;

		if (fx == Fx::LowPass)  return lp;
		if (fx == Fx::HighPass) return hp;
		return v1 >> 2;                           // band-pass, tamed
	}

	// --- B: destruction --------------------------------------------------
	case Fx::Crush:
	{
		// Quantise to fewer bits. The knob picks how many are thrown away:
		// 1 bit at the bottom (barely audible) to 9 at the top (brutal).
		int32_t bits = 1 + ((depth_ * 8) >> 12);
		int32_t mask = ~((1 << bits) - 1);
		return in & mask;
	}

	case Fx::Decimate:
	{
		// Sample-and-hold at a reduced rate. Holding for N samples divides the
		// effective rate by N, which aliases everything above the new Nyquist
		// back down — the characteristic grainy pitch-shimmer.
		int32_t hold = 1 + ((depth_ * 32) >> 12);
		if (--holdCnt <= 0) { holdCnt = hold; holdVal = in; }
		return holdVal;
	}

	case Fx::Fold:
	{
		// A wavefolder. Drive the signal up, then REFLECT anything past the
		// rails back down rather than clipping it — clipping removes harmonics
		// and folding adds them, which is why this sounds like an effect and a
		// clipper sounds like a fault.
		int32_t drive = 256 + ((depth_ * 1792) >> 12);     // Q8, 1x..8x
		int32_t v = (in * drive) >> 8;
		constexpr int32_t kLim = 2047;
		// Two folds is enough for anything reachable at 8x; a while loop here
		// would be unbounded work inside an interrupt.
		for (int i = 0; i < 3; i++)
		{
			if (v >  kLim) v =  2 * kLim - v;
			else if (v < -kLim) v = -2 * kLim - v;
			else break;
		}
		return v;
	}

	// --- D: dynamics -----------------------------------------------------
	case Fx::Gate:
	{
		// A rhythmic chop. Free-running rather than started on the press, so
		// the gate stays in phase with the music however late you grab it.
		//
		// The rate is a division of the sample clock rather than of the loop,
		// which keeps it independent of tempo — a gate that slowed with the
		// pattern would just sound like tremolo.
		uint32_t rate = 64 + static_cast<uint32_t>((4095 - depth_) * 12);
		gatePhase += 1;
		if (gatePhase >= rate) gatePhase = 0;
		// A short ramp at each edge, or the chop clicks on every transition.
		constexpr uint32_t kEdge = 32;
		uint32_t half = rate >> 1;
		if (gatePhase >= half) return 0;
		int32_t gain = 256;
		if (gatePhase < kEdge)          gain = static_cast<int32_t>(gatePhase * 8);
		else if (half - gatePhase < kEdge) gain = static_cast<int32_t>((half - gatePhase) * 8);
		if (gain > 256) gain = 256;
		return (in * gain) >> 8;
	}

	case Fx::Silence:
		// A hard mute for drop-outs. Deliberately abrupt — the point is the
		// hole, and a fade would blunt it.
		return 0;

	// These act somewhere other than the audio path: stutter and flam
	// schedule extra hits, reverse and tape-stop change how a voice STARTS.
	// Pass through untouched.
	case Fx::Stutter:
	case Fx::Flam:
	case Fx::Reverse:
	case Fx::TapeStop:
		return in;
	}
}

} // namespace nko
