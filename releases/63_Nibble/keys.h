// keys.h — the default boot mode: a ten-note keyboard.
//
// A combo becomes a scale degree becomes a 1V/oct pitch, plus a harmony voice
// and two envelopes shaped by one macro knob.
//
//   CV Out 1     1V/oct pitch, calibrated. Stepped, or glided when Switch UP.
//   CV Out 2     1V/oct HARMONY, a third above in the current scale
//   Audio Out 1  FILTER envelope   -- snappier, for a filter cutoff
//   Audio Out 2  LOUDNESS envelope -- amplitude / how long the note lasts
//   Pulse Out 1  gate on every note event
//
// The audio outs are DC-coupled so they work as CV, but they are NOT calibrated
// the way the CV outs are. They are fine driving envelope and filter inputs;
// they are not accurate enough for pitch, and the README says so.

#pragma once
#include <stdint.h>
#include "nibble.h"

namespace nib {

/// The envelope destinations.
///
/// There were three. LENGTH is gone: on the panel it turned out to be
/// indistinguishable from LOUDNESS — both are "how long does this note last" —
/// so it was a control that cost an output and told you nothing new. CV Out 2
/// now carries a HARMONY pitch instead, which is something the card could not
/// do at all before.
enum EnvDest : uint8_t { kEnvFilter = 0, kEnvLoudness = 1 };
constexpr int kNumEnvs = 2;

// How far above the played note the harmony sits is per-scale, and lives with
// the scale tables: see HarmonyDegreesFor() in scales.h.

/// The root of the scale sits at 0V.
///
/// It used to be MIDI note 36, which put the root at THREE VOLTS — so tuning an
/// oscillator to match meant winding it down an octave and a half, and it read
/// as an octave jump against anything else in the rack. Worse, it wasted 3V of
/// a ~6V output: the top of a wide scale with full transpose asked for 7.3V and
/// silently clipped.
///
/// At 0V the whole useful range is available above it, an oscillator at its own
/// zero is already in tune, and CV In 2 can transpose in either direction from
/// something musically neutral.
constexpr int kBaseNote = 0;

/// Transpose range from CV In 2, in semitones either way.
///
/// Downward transposition is CLAMPED at the root rather than going negative:
/// the CV outputs can swing below zero, but an oscillator's 1V/oct input
/// generally cannot use it, so the notes would simply pile up at the bottom.
constexpr int kTransposeSemis = 24;

/// Highest the transposed root may go, in semitones above 0V.
///
/// Sized so the highest note the card can produce still fits the ~6V output.
/// The widest case is the Maj7 arpeggio's HARMONY voice: the harmony sits two
/// degrees above the tenth combo, i.e. degree 11, which on a four-note scale is
/// two octaves plus a major seventh — 35 semitones above the root. 36 + 35 = 71
/// semitones = 5.92V, just inside.
///
/// Beyond this the top of the scale clips silently, which is exactly how the
/// old 3V root managed to ask for 7.3V without ever complaining. Verified for
/// all twelve scales rather than reasoned about — the arpeggios are the ones
/// that bite, because few notes per octave means degrees climb fast.
constexpr int kMaxRoot = 36;

/// Extra fractional bits the envelope accumulator carries.
///
/// This is not a refinement — without it the envelope CANNOT be made long.
/// `e -= (e >> shift) + 1` reaches zero because of the `+1`, but that same `+1`
/// dominates as soon as `e >> shift` rounds to zero, which for a peak of 2047
/// happens around shift 11. Every shift above that decayed in the SAME 43ms,
/// so the whole top half of the macro knob did nothing at all and the longest
/// available envelope was a blip.
///
/// Running the accumulator 8 bits higher keeps the shift meaningful for far
/// longer: the range becomes ~13ms to ~11s, and the knob works across its
/// whole travel.
constexpr int kEnvFrac = 8;

/// A one-shot decay envelope. Exponential-shaped via the WildPebble idiom,
/// `e -= (e >> shift) + 1`, which is guaranteed to actually reach zero rather
/// than leaving a DC tail.
struct Env
{
	int32_t value = 0;      ///< scaled up by kEnvFrac
	int32_t peak  = 0;      ///< likewise
	uint8_t shift = 12;

	void Trigger() { value = peak; }

	/// One audio-rate step. Envelopes run at 48kHz so they are smooth and
	/// clickless with no separate slew stage. Returns the OUTPUT-scale value.
	inline int32_t Step()
	{
		if (value > 0)
		{
			value -= (value >> shift) + 1;
			if (value < 0) value = 0;
		}
		return value >> kEnvFrac;
	}
};

class Keys
{
public:
	/// Recompute the macro distribution. Control rate; `main` and `x` are raw
	/// knob readings, 0..4095.
	void SetMacro(int32_t mainKnob, int32_t xKnob);

	/// Fire the envelopes and latch a new pitch pair.
	/// `harmonyNote` is the in-scale third computed by the caller, which owns
	/// the root and scale.
	void NoteOn(uint8_t midiNote, uint8_t harmonyNote);

	/// Re-fire the envelopes without changing pitch (momentary tap, Pulse In).
	void Retrigger();

	/// One audio-rate step. Returns the envelope levels, 0..2047.
	void StepEnvelopes(int32_t out[kNumEnvs]);

	/// Target pitch in millivolts, for CV Out 1.
	/// `glide` slews toward it; otherwise it steps.
	int32_t PitchMillivolts(bool glide);

	/// Harmony pitch in millivolts, for CV Out 2. Follows the same glide
	/// setting, so the two voices move together rather than one sliding under
	/// the other.
	int32_t HarmonyMillivolts(bool glide);

	uint8_t Note() const { return note_; }

private:
	Env      env_[kNumEnvs];
	uint8_t  note_       = kBaseNote;
	int32_t  targetMv_   = 0;
	int32_t  glideMv_    = 0;
	int32_t  harmTarget_ = 0;
	int32_t  harmGlide_  = 0;
	bool     glideInit_  = false;
};

} // namespace nib
