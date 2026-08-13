// keys.cpp — the keyboard mode's macro, envelopes and pitch output.

#include "keys.h"
#include "fastmath.h"
#include "pico.h"

namespace nib {

namespace {

// Envelope decay shifts, with kEnvFrac bits of headroom in the accumulator.
//
//   FILTER    shift  7..16  ->   24ms .. 3.7s
//   LOUDNESS  shift  8..17  ->   44ms .. 5.7s
//
// These are MUCH longer than the first version, which topped out at 43ms for
// every setting above shift 11 — see kEnvFrac in keys.h for why that happened.
// On the panel it meant the macro knob's whole upper half did nothing and every
// note was a blip.
//
// Loudness is given the longer floor and ceiling of the two: it is the one
// standing in for "how long is this note", so it needs to reach genuine pads,
// while the filter wants to be able to go a little snappier for pluck sounds.
constexpr uint8_t kMinShift[kNumEnvs]   = { 7, 8 };   // filter, loudness
constexpr uint8_t kShiftRange[kNumEnvs] = { 9, 9 };

/// Full-scale envelope output. The audio outs run -2048..2047 and the envelopes
/// are unipolar, so this is the positive ceiling.
constexpr int32_t kEnvFullScale = 2047;

} // namespace

// ---------------------------------------------------------------------------
// The macro
// ---------------------------------------------------------------------------

void Keys::SetMacro(int32_t mainKnob, int32_t xKnob)
{
	const int32_t m = knob_to_q16(mainKnob);    // intensity
	const int32_t x = knob_to_q16(xKnob);       // distribution

	// Main sets how long/big the note is; X decides how that is SPLIT between
	// the filter and the loudness. Three anchors, crossfaded so X is continuous
	// and there is no audible step where they meet:
	//
	//   DARK      filter well short of the amplitude — the note opens briefly
	//             and then sustains dull. Plucks, muted things.
	//   BALANCED  both follow the macro together.
	//   BRIGHT    filter outlasts the amplitude — the note stays open all the
	//             way through its tail. Pads, swells, anything that should
	//             bloom rather than close down.
	int32_t aF, aA, bF, bA, t;

	if (x < kQ16One / 2)
	{
		t = x * 2;
		// DARK
		aF = mul_q16(m, 26214);                     // 40% of macro
		aA = m;
		// BALANCED
		bF = m; bA = m;
	}
	else
	{
		t = (x - kQ16One / 2) * 2;
		// BALANCED
		aF = m; aA = m;
		// BRIGHT
		bF = m;
		bA = mul_q16(m, 45875);                     // 70% of macro
	}

	const int32_t amount[kNumEnvs] = {
		aF + mul_q16(bF - aF, t),
		aA + mul_q16(bA - aA, t),
	};

	for (int i = 0; i < kNumEnvs; i++)
	{
		int32_t a = amount[i];
		if (a < 0)        a = 0;
		if (a > kQ16One)  a = kQ16One;

		// The peak is FULL SCALE regardless of the macro; only the decay length
		// follows it.
		//
		// Scaling the peak too (as the first version did) meant a short setting
		// was also a quiet one, so the envelopes barely registered at the bottom
		// of the knob and there was no way to get a short, LOUD note — which is
		// most percussive playing. Length is the interesting axis here; anything
		// downstream has its own depth control for the other one.
		env_[i].peak = kEnvFullScale << kEnvFrac;

		// sqrt bends the law so the bottom of the travel moves fastest, which
		// is where the audible difference between "click" and "short" lives.
		int32_t curve = fast_sqrt_q16(a);
		env_[i].shift = static_cast<uint8_t>(
			kMinShift[i] + ((curve * kShiftRange[i]) >> 16));
	}
}

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------

void Keys::NoteOn(uint8_t midiNote, uint8_t harmonyNote)
{
	note_ = midiNote;

	// Pitch is carried in millivolts rather than as a MIDI note so that glide
	// has something continuous to move through. CVOutMillivolts() applies the
	// same EEPROM calibration as CVOutMIDINote(), so tuning is unaffected.
	targetMv_   = SemisToMillivolts(midiNote);
	harmTarget_ = SemisToMillivolts(harmonyNote);

	// First note after boot should not glide up from zero — that is a swoop
	// nobody asked for.
	if (!glideInit_)
	{
		glideMv_   = targetMv_;
		harmGlide_ = harmTarget_;
		glideInit_ = true;
	}

	Retrigger();
}

void Keys::Retrigger()
{
	for (int i = 0; i < kNumEnvs; i++) env_[i].Trigger();
}

int32_t __not_in_flash_func(Keys::PitchMillivolts)(bool glide)
{
	if (!glide)
	{
		glideMv_ = targetMv_;
		return targetMv_;
	}

	// "Fast glide": a short portamento, not a lazy slide. Shift 9 at 48kHz
	// covers a semitone in a few milliseconds and an octave in around 40 —
	// audible as a bend rather than as a wait.
	//
	// slew_exact, not slew: a stalling slew would leave the pitch permanently
	// a few millivolts flat of the note it was heading for, which on a 1V/oct
	// output is an audible detune rather than a rounding error.
	glideMv_ = slew_exact(glideMv_, targetMv_, 9);
	return glideMv_;
}

int32_t __not_in_flash_func(Keys::HarmonyMillivolts)(bool glide)
{
	if (!glide)
	{
		harmGlide_ = harmTarget_;
		return harmTarget_;
	}
	// Same slew rate as the melody, so the two voices arrive together. A
	// different rate would have the harmony sliding into place under a note
	// that had already settled, which reads as a tuning fault rather than an
	// effect.
	harmGlide_ = slew_exact(harmGlide_, harmTarget_, 9);
	return harmGlide_;
}

void __not_in_flash_func(Keys::StepEnvelopes)(int32_t out[kNumEnvs])
{
	for (int i = 0; i < kNumEnvs; i++) out[i] = env_[i].Step();
}

} // namespace nib
