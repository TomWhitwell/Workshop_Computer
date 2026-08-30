// shepard.h - the octave-stack oscillator bank and its spectral envelope.
//
// A Shepard tone is a stack of sine components spaced exactly one octave
// apart, all sliding together through a FIXED spectral envelope. Each
// component fades in at the bottom of the envelope, rises through it, and
// fades out at the top. Because the stack is octave-spaced, the configuration
// after sliding one whole octave is IDENTICAL to where it started - so the
// slide can be looped forever and the ear never catches the seam.
//
// Two quantities here are both called "phase" and must not be confused:
//
//   master_q32   a PITCH position. One octave = 2^32. Advances at the glide
//                rate, a few octaves per second at most. Its wrap IS the
//                illusion.
//   osc_q32[i]   an AUDIO phase. One cycle = 2^32. Advances at that layer's
//                frequency, up to ~28 kHz.
//
// Conflating them is the fastest way to break this card.
//
// Everything is fixed point. See fixed.h for why that is not negotiable.

#ifndef SHEPARD_SHEPARD_H_
#define SHEPARD_SHEPARD_H_

#include <stdint.h>

#include "fixed.h"

namespace shepard {

// Layer count range, from the X knob.
//
// 3 is the floor because two layers do not read as a continuous stack - you
// hear two oscillators, not an illusion.
//
// 11 is the ceiling because of NYQUIST, and the reason is worth recording
// since 12 looks like the natural number and was the original choice.
//
// Each layer added shifts the perceived pitch of the stack up by half a
// window slot, which at octave spacing is exactly +600 cents. That is even
// from N=3 to N=11 - measured 0.0 cents of deviation.
//
// At N=12 the top layer sits at 13.75 * 2^11 = 28160 Hz, above the 24 kHz
// Nyquist limit. It still carries window gain (0.067) and still counts in the
// constant-sum, but contributes NOTHING audible - so the audible centre lands
// 68 cents short of where the pattern leads the ear to expect. Heard on
// hardware as the pitch dropping about a semitone when the last layer is
// added, and very obvious when frozen.
//
// Capping at 11 keeps the top layer at 14080 Hz, comfortably audible, so
// every layer contributes and the pattern stays even all the way up.
static constexpr int kMinLayers = 3;
static constexpr int kMaxLayers = 11;

// Sine table. 1024 entries of Q15, linear interpolation between them.
//
// It doubles as the WINDOW table: the envelope is sin^2(pi*u), and sin(pi*u)
// is exactly the first HALF of a sin(2*pi*x) table. So there is no separate
// window LUT - see HannQ15 below.
static constexpr int kSinLutSize = 1024;
static constexpr int kSinLutMask = kSinLutSize - 1;

// pow2 table: 2^f for f in [0,1), output Q30 spanning [2^30, 2^31).
//
// 257 entries, not 256: the extra guard entry holds 2^1 so the linear
// interpolation can read lut[idx+1] at idx == 255 without a bounds check in
// the inner loop.
//
// Size chosen by measurement (tools/shepard_check.py reports the table):
//
//      65 entries -> 14.7 ppm -> 0.025 cents
//     129 entries ->  3.7 ppm -> 0.006 cents
//     257 entries ->  0.9 ppm -> 0.0016 cents   <- chosen
//
// 0.0016 cents is four orders of magnitude below audibility, and the whole
// table is 1 KB. There is no reason to go smaller.
static constexpr int kPow2LutSize = 256;

// TABLES ARE PLAIN GLOBALS, DEFINED IN shepard.cpp - NEVER header statics.
//
// This is WorkshopSpectral gotcha #1 and it fails SILENTLY: a `static` array
// declared inside a header can be placed in flash by the linker, where every
// write succeeds and is discarded, leaving the table at its init values. No
// error, no warning, just a card that sounds wrong. Verify with:
//
//     arm-none-eabi-nm build/shepard.elf | grep -E "sin_lut|pow2_lut"
//
// and look for 'B' or 'b' (BSS/RAM), never 't' or 'T' (flash).
extern int16_t sin_lut[];    // [kSinLutSize]      Q15 sin(2*pi*k/1024)
extern int32_t pow2_lut[];   // [kPow2LutSize + 1] Q30 2^(k/256)

// Fill the tables. Call once, before Run(). Uses floats - that is fine and
// expected HERE, at init. Never in the audio path.
void ShepardInit();

// Q15 sine of a Q32 audio phase, with linear interpolation between table
// entries. Interpolation matters: at 1024 points the raw table quantises to
// ~0.35 degrees of phase, which is audible as a buzz on low layers.
static inline int32_t __not_in_flash_func(SinQ15)(uint32_t phase_q32) {
  const uint32_t idx = phase_q32 >> 22;                  // 0..1023
  const int32_t frac = (int32_t)((phase_q32 >> 7) & 0x7FFF);   // Q15
  const int32_t s0 = sin_lut[idx];
  const int32_t s1 = sin_lut[(idx + 1) & kSinLutMask];
  return s0 + MulQ15(frac, s1 - s0);
}

// The spectral envelope: hann(u) = sin^2(pi*u), u in [0,1) as Q32.
//
// Reuses the FIRST HALF of sin_lut, because sin(pi*u) == sin(2*pi*(u/2)).
// No second table.
//
// Why this window and not another - the property that makes the card work:
//
//   sum over layers of hann((master_frac + i)/N)  ==  N/2   EXACTLY,
//
// for every layer count and every master phase, with ZERO ripple. That is the
// same constant-overlap-add property that makes a Hann window at 50% overlap
// work, and it is why the output level does not pulse as layers cycle through
// the envelope. It is a mathematical guarantee, not a tuning - do not
// "improve" this window. tools/shepard_check.py asserts it.
//
// It also handles both ends of the stack for free: hann(0) == hann(1) == 0
// exactly, so a layer entering at the bottom and one leaving at Nyquist are
// both silent. The octave wrap is a no-op in the audio - there is nothing to
// crossfade and nothing to click.
// THE LOOKUP MUST INTERPOLATE. This is not a refinement - a truncated read
// breaks the constant-sum property above, and it does so ASYMMETRICALLY:
//
//     truncated:     N even -> 0.006% ripple,  N odd -> up to 0.48%
//     interpolated:  every N -> 0.012%, at the integer rounding floor
//
// At even layer counts the layer positions land on exact table entries and the
// truncation error cancels between them; at odd counts they fall between
// entries and it does not. So a truncated version tests clean at N=4, 6, 8 and
// pulses audibly at N=3, 5, 7 - which reads as "the illusion only works at
// some densities" and is very hard to attribute to the window.
//
// Caught by tools/shepard_check.py::check_window_sum, which is exactly the
// kind of bug that test exists to find. Interpolating costs ~6 cycles, at
// control rate, once per layer.
static inline int32_t __not_in_flash_func(HannQ15)(uint32_t u_q32) {
  const uint32_t idx = (u_q32 >> 23) & 0x1FFu;   // 0..511, first half of table
  const int32_t frac = (int32_t)((u_q32 >> 8) & 0x7FFF);
  const int32_t s0 = sin_lut[idx];
  const int32_t s1 = sin_lut[(idx + 1) & kSinLutMask];
  const int32_t s = s0 + MulQ15(frac, s1 - s0);  // Q15 sin(pi*u)
  return MulQ15(s, s);                           // Q15 sin^2(pi*u)
}

// 2^f for f in [0,1) as Q32, returning Q30 in [2^30, 2^31).
//
// Control rate ONLY. This is the one genuinely expensive operation in the
// bank, and it is only ever driven by the master phase, which moves at a few
// octaves per second - so evaluating it at 1.5 kHz instead of 48 kHz costs
// nothing audible and saves 32x. See kControlMask in main.cpp.
static inline int32_t __not_in_flash_func(Pow2Q30)(uint32_t f_q32) {
  const uint32_t idx = f_q32 >> 24;                          // 0..255
  const int32_t frac = (int32_t)((f_q32 >> 9) & 0x7FFF);     // Q15
  const int32_t v0 = pow2_lut[idx];
  const int32_t v1 = pow2_lut[idx + 1];                      // guard entry
  return v0 + MulQ15(frac, v1 - v0);
}

// 1/sqrt(N) in Q15, indexed by layer count directly.
//
// A 13-entry table, not a reciprocal-sqrt approximation. N is an integer in
// 3..12 - there are exactly ten possible values, so a table is smaller,
// faster AND exact where a Newton-Raphson iteration would be none of those.
// Entries 0..2 exist only so the index needs no offset arithmetic.
//
// 1/sqrt(N) and not 1/N because the layers are mutually incoherent: different
// frequencies, unrelated phases, so their POWERS add rather than their
// amplitudes. Total amplitude grows as sqrt(N). Normalising by 1/N instead
// would make dense settings audibly quiet.
extern const int16_t kInvSqrtN[];   // [kMaxLayers + 1]

// The LIVE path's normaliser: 1/N^0.70, NOT 1/sqrt(N). The octave stack's
// layers are correlated copies of one source rather than independent
// oscillators, so they sum faster than sqrt(N). See the derivation and the
// measurements in shepard.cpp - using kInvSqrtN here leaves the audio path
// audibly quiet, and worst at low layer counts.
extern const int16_t kInvPowN[];    // [kMaxLayers + 1]

// EQUAL-POWER crossfade legs for the source mix, from sin_lut - no new table.
//
// A linear crossfade (`v = a*(1-m) + b*m`) is correct only when the two
// signals are the same, or correlated. The synth and the live stack are
// neither: they are unrelated, so their POWERS add and a linear fade dips
// 3 dB in the middle -
//
//     mix    linear   equal-power
//     0.00    1.000       1.000
//     0.25    0.791       1.000
//     0.50    0.707       1.000      <- the audible dip
//     0.75    0.791       1.000
//     1.00    1.000       1.000
//
// which is heard as the internal voice dominating everywhere except the
// extremes, because the fade robs the middle of the knob rather than the ends.
//
// cos/sin holds the power flat instead. Both legs come from the EXISTING sine
// table: mix spans a quarter turn, so `mix << 15` is the live leg's phase and
// `(32767 - mix) << 15` the synth's. Measured worst power deviation over a
// full sweep: 0.0076%, 0.0007 dB. Same trick as HannQ15 - the table already
// holds what is needed, so this costs two lookups and no flash.
static inline void __not_in_flash_func(XfadeQ15)(int32_t mix, int32_t* g_synth,
                                                 int32_t* g_live) {
  *g_synth = SinQ15((uint32_t)(32767 - mix) << 15);
  *g_live = SinQ15((uint32_t)mix << 15);
}

// Scale tables, as Q32 offsets within the octave. These are `const` and only
// ever read, so flash is CORRECT for them - the opposite of sin_lut/pow2_lut
// above. (WorkshopSpectral's kSmoothLut is the precedent.)
// PING decay coefficients, Q20. Declared here rather than in ping.h because
// the definition lives in shepard.cpp, which does not include ping.h - and a
// definition that does not match a visible declaration links to nothing.
extern const int32_t kPingDecay[];   // [17]

extern const uint32_t kScale12[];    // [12] chromatic, 12-ET
extern const uint32_t kScaleMaj[];   // [7]  major
extern const uint32_t kScaleMin[];   // [7]  natural minor
extern const uint32_t kScalePent[];  // [5]  major pentatonic
extern const uint32_t kScaleDim[];   // [8]  diminished / octatonic
extern const uint32_t kScaleWhole[]; // [6]  whole tone

enum Scale {
  kScaleSmooth = 0,   // no quantisation - continuous glissando
  kScaleChromatic = 1,
  kScaleMajor = 2,
  kScaleMinor = 3,
  kScalePentatonic = 4,
  kScaleDiminished = 5,
  kScaleWholeTone = 6,
  kScaleCount = 7
};

// Map a DEGREE-SPACE position to the pitch of that degree.
//
// pos_q32 spans the scale evenly: 0 is degree 0, 2^32/n is degree 1, and so
// on regardless of how far apart the degrees actually are in pitch. The
// return is the Q32 octave position of the degree the position falls in.
//
// This is what makes stepped modes even IN TIME. Advancing the free phase at
// a constant rate through degree space holds every note for the same
// duration; advancing it through PITCH space instead holds a whole tone twice
// as long as a semitone, so in major the notes either side of E-F and B-C
// pass twice as quickly as the rest.
//
// The window and the layer increments are untouched by this - they consume
// the resulting PITCH position and never see how it was arrived at, so the
// constant-sum invariant is exactly as before (verified: 0.0000% ripple
// either way).
uint32_t DegreeToPitch(uint32_t pos_q32, int scale);

// Snap a Q32 octave position to the nearest degree of a scale.
//
// Control rate only. The search is at most 12 compares, which is free at
// 1.5 kHz and not worth a cleverer structure.
uint32_t SnapToScale(uint32_t pos_q32, int scale);

// Advance a step within a scale, for the Pulse In 1 manual trigger.
// `dir` is +1 or -1. Returns the new Q32 position.
uint32_t StepScale(uint32_t pos_q32, int scale, int dir);

}  // namespace shepard

#endif  // SHEPARD_SHEPARD_H_
