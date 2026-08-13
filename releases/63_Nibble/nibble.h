// nibble.h — the vocabulary shared by every other file.
//
// NIBBLE turns the Workshop System's FOUR VOLTAGES module into an instrument.
// Four Voltages has four non-latching buttons (A B / C D), one knob, and four
// outputs driven by a resistor network. One of those outputs is patched to
// CV In 1; the card learns which voltage each button combination produces and
// maps the combinations onto notes (KEYS) or drum sounds (DRUMS).
//
// Four buttons = four bits = one nibble. Hence the name.
//
// THE CENTRAL DIFFICULTY, which shapes most of this card:
// Four Voltages does NOT return to a rest voltage when you let go. The output
// sits at the last-pressed button's level. So releasing AB leaves the output at
// A's voltage, which is indistinguishable from genuinely pressing A. A naive
// level detector fires a spurious note on every release. See levels.h for the
// ghost rule, which turns that flaw into the card's best playing technique.

#pragma once
#include <stdint.h>

namespace nib {

// ---------------------------------------------------------------------------
// Rates
// ---------------------------------------------------------------------------

/// ProcessSample() is called at this rate by ComputerCard's DMA interrupt.
constexpr int32_t kSampleRate = 48000;

/// Control-rate divider. 48000/16 = 3000Hz.
///
/// Higher than WorkshopBio's 1500Hz, deliberately: the settle detector's timing
/// granularity is one control tick, and for a keyboard, note-timing JITTER is
/// more audible than absolute latency. 3kHz halves the jitter for load that is
/// trivial either way.
constexpr int32_t kCtrlDiv  = 16;
constexpr int32_t kCtrlRate = kSampleRate / kCtrlDiv;   // 3000Hz

// WARNING, and this is the single easiest thing to get wrong on this platform:
// the control tick runs INLINE inside ProcessSample(), which is a DMA interrupt
// handler. The divider lowers the AVERAGE load but does NOT relax the deadline.
// On the sample where the control tick fires, everything must still complete
// within that one 20.83us slot. Never quote an amortised cycles/sample figure
// as headroom -- the worst single sample is what decides whether audio glitches.

// ---------------------------------------------------------------------------
// Combos
// ---------------------------------------------------------------------------

constexpr int kNumButtons = 4;
constexpr int kNumSingles = 4;                   // A B C D
constexpr int kNumPairs   = 6;                   // AB AC AD BC BD CD
constexpr int kNumLevels  = kNumSingles + kNumPairs;   // 10

/// Combo indices, fixed for the whole card. Singles first so that
/// `idx < kNumSingles` is the "is this a single?" test, and pairs in
/// lexicographic order so kPairMembers below is trivially checkable.
///
/// Triples and the all-four combo are deliberately NOT represented: they are
/// not learned and are ignored at runtime (see levels.h, kMatchWindow).
enum Combo : int8_t {
	kA = 0, kB = 1, kC = 2, kD = 3,
	kAB = 4, kAC = 5, kAD = 6, kBC = 7, kBD = 8, kCD = 9,
	kComboNone = -1
};

/// The two buttons making up each pair, indexed by (combo - kNumSingles).
constexpr uint8_t kPairMembers[kNumPairs][2] = {
	{0, 1},   // AB
	{0, 2},   // AC
	{0, 3},   // AD
	{1, 2},   // BC
	{1, 3},   // BD
	{2, 3},   // CD
};

/// True if single `s` is one of pair `pair`'s two buttons.
/// Returns false for a non-pair `pair`, so callers do not need to pre-check.
static inline bool IsMemberOf(int8_t s, int8_t pair)
{
	if (pair < kNumSingles || pair >= kNumLevels) return false;
	if (s < 0 || s >= kNumSingles) return false;
	const uint8_t *m = kPairMembers[pair - kNumSingles];
	return (m[0] == s) || (m[1] == s);
}

/// Given a pair and one of its two buttons, return the OTHER one.
///
/// Used to turn (pair, shift) into (shift, tap): if you were holding C and the
/// CV moved to the AC level, the button you just struck is A. Returns
/// kComboNone if `pair` is not a pair or `one` is not part of it.
static inline int8_t OtherMember(int8_t pair, int8_t one)
{
	if (pair < kNumSingles || pair >= kNumLevels) return kComboNone;
	if (one < 0 || one >= kNumSingles) return kComboNone;
	const uint8_t *m = kPairMembers[pair - kNumSingles];
	if (m[0] == one) return static_cast<int8_t>(m[1]);
	if (m[1] == one) return static_cast<int8_t>(m[0]);
	return kComboNone;
}

/// Which LEDs to light for a combo. Singles light one; pairs light both of
/// their buttons. LEDs 0..3 map directly onto the A B / C D button layout --
/// the top 2x2 of the Computer's LEDs mirrors the Four Voltages panel, which is
/// the best affordance available and is used as the organising principle for
/// every display on this card.
static inline uint8_t ComboLedMask(int8_t combo)
{
	if (combo < 0 || combo >= kNumLevels) return 0;
	if (combo < kNumSingles) return static_cast<uint8_t>(1u << combo);
	const uint8_t *m = kPairMembers[combo - kNumSingles];
	return static_cast<uint8_t>((1u << m[0]) | (1u << m[1]));
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

enum class BootMode : uint8_t {
	Keys  = 0,   // default: a 10-note keyboard
	Drums = 1,   // switch held at power-on: lo-fi percussion looper
};

/// The switch is NOT readable straight away, and reads Down until it settles:
/// ComputerCard derives it from knobs[3], off a ~60Hz smoothing filter starting
/// at zero, and zero decodes as Down. So for the first few milliseconds of
/// EVERY boot the card reports Down wherever the switch actually is.
///
/// Latching on "Down seen at any point in the window" therefore latches on
/// every boot -- both WorkshopZX and WorkshopBio shipped that bug. Take ONE
/// reading once settled instead.
constexpr int32_t kBootWindowSamples = kSampleRate / 2;    // 0.5s

/// How long the boot splash announces which mode came up.
constexpr int32_t kSplashSamples = kSampleRate;            // 1.0s

// ---------------------------------------------------------------------------
// Switch gestures
// ---------------------------------------------------------------------------

/// Down for this long is a HOLD; released before it is a TAP.
///
/// The momentary switch carries four actions across two contexts:
///   Play  + tap  -> retrigger the current note/sound
///   Play  + hold -> enter learn
///   Learn + tap  -> capture this step
///   Learn + hold -> abort learn
/// One detector, four dispatches. See main.cpp.
constexpr int32_t kHoldTicks = 2 * kCtrlRate;              // 2s

/// A knob must move at least this much (of 4095) to count as "being moved".
/// Wide enough to ignore ADC dither on a still knob -- which would otherwise
/// pin a "knob moving" display on permanently -- and narrow enough that a
/// deliberate nudge registers.
constexpr int32_t kKnobMoveThresh = 64;

// ---------------------------------------------------------------------------
// Pitch
// ---------------------------------------------------------------------------

/// Millivolts per semitone, x256. 1V/oct is 1000mV per 12 semitones, so one
/// semitone is 83.333mV, kept in Q8.
///
/// Convert with `(note * kMvPerSemiQ8 + 128) >> 8` — the +128 ROUNDS instead of
/// truncating, and it matters. Truncating is biased the same way at every
/// octave (every C landed a full millivolt flat, 1.2 cents, audible against a
/// tuned oscillator); rounding brings the worst case over 0..127 down to
/// 0.33mV, or 0.4 cents, which is not.
constexpr int32_t kMvPerSemiQ8 = 21333;   // round(1000/12 * 256)

/// Semitone number -> millivolts, rounded.
static inline int32_t SemisToMillivolts(int32_t semis)
{
	return (semis * kMvPerSemiQ8 + 128) >> 8;
}

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------

// Physical layout:   0 1
//                    2 3
//                    4 5
constexpr int kNumLeds = 6;

constexpr uint16_t kLedFull = 4095;
constexpr uint16_t kLedDim  = 600;
constexpr uint16_t kLedGlow = 200;

} // namespace nib
