// fx.h — the twelve performance effects of FX bank 1.
//
// Reached by holding any SINGLE and tapping another: four shifts times three
// taps is twelve ordered gestures, exactly as DRUMS gets its twelve voices
// from the same four buttons (see levels.h's Shift()). Effects are MOMENTARY:
// active while the tap is held, gone the instant it is released.
//
// Two families, and they hook into completely different places:
//
//   AUDIO effects process the drum bus, one sample at a time. They live in
//   FxRack::Process(), called from the audio tick.
//
//   TIMING effects change WHEN the loop's hits fire, or add extra ones. They
//   cannot be done in the audio path at all — they act on the looper's output
//   before a voice is even triggered. See FxRack::TimingMode().
//
// Nothing here touches the recorded pattern. An effect is a filter on the way
// out, so releasing the button leaves the loop exactly as it was — which is
// what makes it safe to lean on one mid-performance.

#pragma once
#include <stdint.h>
#include "nibbleko.h"
#include "fastmath.h"

namespace nko {

/// The twelve effects, in (shift, tap) order.
///
/// The layout is chosen so the four shifts are each a FAMILY, which is what
/// makes twelve gestures learnable — you remember "B is the destructive
/// ones", not twelve arbitrary pairings.
///
///   A = filters   B = destruction   C = rhythmic   D = transport
///
/// HALF-TIME AND DOUBLE-TIME USED TO LIVE IN C AND ARE GONE. They were the
/// only effects that moved the PLAYHEAD rather than filtering the output, and
/// that made them quietly destructive: engaging one off-beat displaced the
/// loop's phase permanently, because releasing simply carried on from
/// wherever the playhead had got to. A bar of half-time left the pattern half
/// a bar behind everything else in the rack with nothing to pull it back. A
/// phase-preserving version is possible — skip ticks, then snap to where the
/// pattern would have been — but it is real work for something that was not
/// much fun to play, so the idea went with them.
enum class Fx : uint8_t {
	None = 0,

	// --- A: filters, swept by the Main knob -----------------------------
	LowPass,      ///< A+B  low-pass, Main sets cutoff
	HighPass,     ///< A+C  high-pass, Main sets cutoff
	BandSweep,    ///< A+D  a resonant band, Main sweeps it

	// --- B: destruction --------------------------------------------------
	Crush,        ///< B+A  bit-crush, Main sets depth
	Decimate,     ///< B+C  sample-rate reduction
	Fold,         ///< B+D  wavefolder, Main sets drive

	// --- C: rhythmic ------------------------------------------------------
	Stutter,      ///< C+A  re-fire the last hit, Main sets the division
	Flam,         ///< C+B  every hit doubles, Main sets the gap
	Gate,         ///< C+D  chop the bus on and off, Main sets the rate

	// --- D: transport -----------------------------------------------------
	//
	// These mean DIFFERENT THINGS to a sampled voice and a synthesised one,
	// which is the point of grouping them: the gesture is "play it wrong on
	// purpose", and what that means depends on what the voice is made of.
	Reverse,      ///< D+A  samples play backwards; synth voices swell
	TapeStop,     ///< D+B  rate or pitch falls to zero. Main sets fall time
	Silence,      ///< D+C  hard mute, for drop-outs

	kNumFx
};

/// (shift, tap) -> effect. -1 on the diagonal: a button cannot shift itself.
///
/// Read this as four rows of three. Same shape as drums.cpp's kGestureVoice
/// and for the same reason: it is the ONLY place the mapping lives, so it can
/// be rearranged without touching either the gesture reader or the DSP.
extern const Fx kFxForGesture[kNumSingles][kNumSingles];

/// What an effect does to TRIGGERING, if anything. Nothing here moves the
/// playhead — see the note on the removed half/double-time above.
enum class FxTiming : uint8_t {
	Normal = 0,
	Stutter,     ///< repeat the last hit at a division
	Flam,        ///< double every hit, closely spaced
};

/// Does this effect act at TRIGGER time rather than on the audio?
///
/// Reverse and TapeStop are both: a voice has to be STARTED backwards or
/// started with a falling rate, which cannot be done to a signal that has
/// already been mixed. They are applied in DrumKit::TriggerVoice.
static inline bool IsTriggerFx(Fx f)
{
	return f == Fx::Reverse || f == Fx::TapeStop;
}

/// How many effects can be layered at once. One per SHIFT button — each shift
/// owns an automation lane, so up to four recorded effects can overlap.
constexpr int kNumFxSlots = kNumSingles;

/// Roll a random effect, and say which SLOT it belongs in.
///
/// Rolls a GESTURE rather than an effect index, which is what makes the slot
/// fall out for free: the table's row IS the shift, and the shift IS the
/// slot. Picking an effect and then choosing a slot for it separately would
/// break two things quietly — FxRack::TriggerFx() only looks in slot D for
/// Reverse/TapeStop, and FxRack::Timing() only looks in slot C for
/// Stutter/Flam, so either landing anywhere else would simply not fire.
///
/// The diagonal is None (a button cannot shift itself), so re-roll the tap
/// rather than returning an effect that does nothing. Three of sixteen
/// gestures are on the diagonal, so this almost never loops more than once.
///
/// `rng` is the caller's PRNG state — see fastmath.h, no global generator.
static inline Fx RandomFx(uint32_t &rng, int8_t &slotOut)
{
	const int8_t shift = static_cast<int8_t>(xorshift32(rng) & 3u);
	int8_t tap;
	do { tap = static_cast<int8_t>(xorshift32(rng) & 3u); } while (tap == shift);
	slotOut = shift;
	return kFxForGesture[shift][tap];
}

/// Shift C's slot is where the rhythmic effects live. It is no longer
/// exclusive: half-time and double-time were the only pair that genuinely
/// contradicted each other, and they are gone. Stutter and flam act on
/// triggering, gate acts on the signal, and none of the three conflicts with
/// anything in another bank — so C chains like every other slot.
constexpr int kFxRhythmSlot = kC;

/// One effect and the state it needs, independent of the others.
///
/// Each slot carries its OWN filter/decimator/gate state. Sharing one set
/// across the chain would make two layered filters interfere — the second
/// would read the first's charge as its own — so the cost of four copies
/// buys correctness, not just convenience. It is about 30 bytes each.
struct FxSlot
{
	Fx      fx    = Fx::None;
	int32_t depth = 2048;

	// Filter state. The three A-row effects are the same SVF read at
	// different outputs, so one set covers all three.
	int32_t v1 = 0, v2 = 0, g = 8000;

	// Decimate: the held sample and its countdown.
	int32_t holdVal = 0;
	int32_t holdCnt = 0;

	// Gate: free-running, so the chop stays rhythmic however late it is
	// grabbed rather than restarting on the press.
	uint32_t gatePhase = 0;

	/// One sample through this slot alone.
	int32_t Step(int32_t in);

	/// Reset the carried state. Called when the slot changes effect, so the
	/// new one does not open with a click of the old one's residue.
	void Clear() { v1 = v2 = 0; holdVal = holdCnt = 0; }
};

/// The effect chain: up to four slots processed in SERIES.
///
/// Series, not parallel, and not "one wins". Crush on one layer and gate on
/// another gives a crushed AND gated bus, which is the reason for layering at
/// all. Slots are walked in shift order, so the chain is deterministic —
/// A then B then D — rather than depending on the order they were recorded.
///
/// Shift C's slot is the TIMING one and never appears in the audio chain: its
/// effects act on the looper instead. See kLaneTiming in looper.h.
class FxRack
{
public:
	/// Set one slot's effect and depth. Control rate.
	/// `slot` is the shift index, 0..3. `depth` is 0..4095.
	void SetSlot(int slot, Fx f, int32_t depth);

	/// Clear every slot. Cheaper than four calls and says what it means.
	void Clear();

	/// What the rhythmic slot is asking for at trigger time, if anything.
	FxTiming Timing() const;

	/// Which TRIGGER-time effect is armed (reverse, tape-stop), or None.
	///
	/// Separate from Timing() because these are answered by a different slot
	/// and consumed in a different place: DrumKit::TriggerVoice needs them
	/// when a voice STARTS, where Timing() is about whether extra hits get
	/// scheduled at all.
	Fx TriggerFx() const;

	/// True if any slot is doing anything, for the audio path to skip the
	/// whole chain when nothing is active.
	bool Any() const { return active_ != 0; }

	/// One audio sample through the whole chain.
	int32_t Step(int32_t in);

private:
	FxSlot   slot_[kNumFxSlots];
	uint8_t  active_ = 0;      ///< bit per slot, for a quick "anything on?"
};

} // namespace nko
