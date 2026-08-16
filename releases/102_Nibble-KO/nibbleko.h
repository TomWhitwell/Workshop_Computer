// nibbleko.h — the vocabulary shared by every other file.
//
// NIBBLE-KO turns the Workshop System's FOUR VOLTAGES module into a 12-voice
// percussion instrument. Four Voltages has four non-latching buttons (A B /
// C D), one knob, and four outputs driven by a resistor network. One of those
// outputs is patched to CV In 1; the card learns which voltage each button
// combination produces and maps combinations onto drum hits, using the SHIFT
// mechanic from NIBBLE's Percussion half: hold one button as a bank select,
// tap another to play — see levels.h for LevelTracker::Shift().
//
// Four buttons = four bits = one nibble. KO: this card is percussion only,
// the expanded, sample-capable descendant of NIBBLE's DRUMS half.
//
// THE CENTRAL DIFFICULTY, which shapes most of this card:
// Four Voltages does NOT return to a rest voltage when you let go. The output
// sits at the last-pressed button's level. So releasing AB leaves the output at
// A's voltage, which is indistinguishable from genuinely pressing A. A naive
// level detector fires a spurious note on every release. See levels.h for the
// ghost rule, which turns that flaw into the card's best playing technique.
//
// Ported from NIBBLE (../WoskshopButtons/nibble.h) — see docs/LESSONS.md there
// for why these constants are what they are. Trimmed to what a percussion-only
// card needs: no BootMode (this card only ever runs DRUMS), no scale/quantiser
// vocabulary (that lived in KEYS).

#pragma once
#include <stdint.h>

namespace nko {

// ---------------------------------------------------------------------------
// Rates
// ---------------------------------------------------------------------------

/// ProcessSample() is called at this rate by ComputerCard's DMA interrupt.
constexpr int32_t kSampleRate = 48000;

/// Control-rate divider. 48000/16 = 3000Hz.
///
/// Inherited from NIBBLE: the settle detector's timing granularity is one
/// control tick, and for percussion, hit-timing JITTER is more audible than
/// absolute latency.
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
// Modes
// ---------------------------------------------------------------------------

/// What the four buttons currently do.
///
/// The mode is LATCHED: it is chosen with the switch held Down (see main.cpp's
/// gesture reader) and then persists while the switch sits at Middle (play) or
/// Up (play and record). That latching is what lets every mode be recorded
/// through one mechanism -- Down and Up never need to be held at once.
enum class Mode : uint8_t {
	Drums   = 0,   ///< the kit, played as NIBBLE's DRUMS. Power-on default.
	Mute    = 1,   ///< three mute-group toggles
	Fx      = 2,   ///< twelve performance effects, four banks of three
	Pattern = 3,   ///< four stored patterns: tap recalls, hold stores
	WebUi   = 4,   ///< USB/browser setup. A doorway, not a performance mode.
	kNumModes = 5
};

/// Which mode each SINGLE selects while the switch is held Down. Indexed by
/// combo (kA..kD), so the table reads in panel order.
constexpr Mode kModeForSingle[kNumSingles] = {
	Mode::Drums,   // A
	Mode::Mute,    // B
	Mode::Fx,      // C
	Mode::Pattern, // D
};

/// The one-shot actions reached by switch+PAIR. Not modes: they fire and
/// leave the latched mode alone.
enum class Action : uint8_t {
	None = 0,
	EnterWebUi,   ///< AB
	Undo,         ///< AC
	Quantise,     ///< AD
	PlayStop,     ///< CD
};

/// Which action each PAIR fires, indexed by (combo - kNumSingles) so it lines
/// up with kPairMembers.
///
/// The layout follows how often you reach for each thing under pressure. The
/// ADJACENT pairs -- AB, CD, and AC across the top row -- are the ones a hand
/// already on the buttons can hit without looking, so they carry the actions
/// you want mid-performance:
///
///   AB  UNDO       you have just recorded something wrong and want it gone
///                  before the loop comes round again
///   AC  QUANTISE   changing the grid between overdub passes is a normal part
///                  of building a pattern, not a setup step
///   CD  PLAY/STOP
///
/// Entering the WebUI is a setup activity done once with both hands free, so
/// it takes the awkward diagonal (BD) rather than a comfortable pair.
constexpr Action kActionForPair[kNumPairs] = {
	Action::Undo,         // AB
	Action::Quantise,     // AC
	Action::None,         // AD  (reserved)
	Action::None,         // BC  (reserved)
	Action::EnterWebUi,   // BD
	Action::PlayStop,     // CD
};

/// How many mute groups there are. Three, not four -- the fourth button is
/// reserved in Mute mode.
constexpr int kNumMuteGroups = 3;

/// How many effects live in each bank. Three, same reasoning.
constexpr int kNumFxPerBank = 3;

// ---------------------------------------------------------------------------
// Switch gestures
// ---------------------------------------------------------------------------

/// The switch is NOT readable straight away, and reads Down until it settles:
/// ComputerCard derives it from knobs[3], off a ~60Hz smoothing filter starting
/// at zero, and zero decodes as Down. So for the first few milliseconds of
/// EVERY boot the card reports Down wherever the switch actually is.
///
/// Latching on "Down seen at any point in the window" therefore latches on
/// every boot -- three sibling cards have shipped that bug between them. Take
/// ONE reading once settled instead.
constexpr int32_t kBootWindowSamples = kSampleRate / 2;    // 0.5s

/// How long the boot splash announces the card is ready.
constexpr int32_t kSplashSamples = kSampleRate;            // 1.0s

/// Down for this long is a HOLD rather than a press.
///
/// Used for the one destructive gesture on the card: throwing away a
/// calibration in progress and starting it again. It has to be FAR longer
/// than the mode-select debounce (kSelectMinTicks, 50ms), because every
/// ordinary mode-select is a brief press of the same switch -- at 50ms a tap
/// aborted the calibration, which is exactly what it felt like on the bench:
/// "as soon as I tap the switch I'm back at the start".
///
/// Two seconds, as NIBBLE used. Long enough that it cannot be reached by
/// accident, short enough not to feel like the card has hung.
constexpr int32_t kHoldTicks = 2 * kCtrlRate;              // 2s

/// A knob must move at least this much (of 4095) to count as "being moved".
/// Wide enough to ignore ADC dither on a still knob -- which would otherwise
/// pin a "knob moving" display on permanently -- and narrow enough that a
/// deliberate nudge registers.
constexpr int32_t kKnobMoveThresh = 64;

// There is no mode-change animation and no constant for one. An earlier
// version flashed a per-mode pattern across LEDs 0-3 on every latch; it read
// as noise rather than information. The current mode's SHIFT button pulses
// instead (kLedQuarter, see main.cpp's ModeReminder), which says the same
// thing continuously rather than only in the moment after you change it.

/// Blink shifts, as `(timer >> shift) & 1`, against the 3kHz control rate.
///
/// These MUST be checked against the rate the timer actually ticks at. A shift
/// of 5 gives 47Hz, well above flicker fusion, so it reads as a dim steady glow
/// rather than a blink -- every calibration alert in NIBBLE was written at
/// shift 4, 5 or 6 and was therefore INVISIBLE, reported from the bench as
/// "the alerts aren't happening".
///
///   >>7 = 11.7Hz   urgent but countable
///   >>8 =  5.9Hz   a clear warning blink
///   >>9 =  2.9Hz   slow and deliberate
///
/// Anything below >>7 at this tick rate is not a blink.
constexpr int kBlinkFast = 7;    ///< ~12Hz, for "something is wrong"
constexpr int kBlinkSlow = 8;    ///< ~6Hz, for a countable warning

// ---------------------------------------------------------------------------
// Pitch (DRUMS bassline CV out — see drums.h kBassSemis)
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
// CV expansion
// ---------------------------------------------------------------------------
//
// The glitch generators on CV Out 1 and 2, and the chaos level that drives
// them. See main.cpp's GlitchTick().

/// Chaos with nothing patched into Audio In 2: about 5%, one division in
/// twenty. Not zero, so the glitch outputs always do something — an output
/// that is silent until you patch a control into it reads as broken.
constexpr int32_t kChaosDefault = 205;          // 5% of 4095

/// Above this chaos, CV Out 1 stops being beats-only and starts considering
/// every grid division. Below it the sparse stream is strictly on the beat.
constexpr int32_t kChaosDivisionOpens = 1200;

/// Above this, CV Out 2's hits can become ratchets — a short burst instead of
/// one gate. This is what makes high chaos sound like a machine failing
/// rather than merely a busier pattern.
constexpr int32_t kChaosRatchetOpens = 2800;

/// Ceiling on firing probability, out of 4096.
///
/// Never 4096. A stream that fires on EVERY candidate is a pulse train, not
/// a random one — and the downbeat, which carries extra weighting, would hit
/// every bar at full chaos and turn the least predictable setting into a
/// metronome. ~75% still reads as "constantly glitching" while keeping gaps.
constexpr int32_t kGlitchMaxOdds = 3072;        // 75%

/// Gate LENGTHS, as a fraction of the DIVISION the gate fired on.
///
/// These were fixed millisecond figures (20ms/5ms/2ms) and that was wrong. A
/// trigger wants to be tempo-independent; a GATE whose width IS the effect's
/// duration has to be musical, or patching CV Out into Pulse In 2 applies
/// each random effect for a click and nothing is audible. Reported from the
/// bench as "they need to last meaningful musical divisions".
///
/// A fraction of the DIVISION, not of the beat, and that distinction is
/// load-bearing: candidates arrive every division once chaos opens them up,
/// so a beat-long gate would still be high when the next one fired. The line
/// would sit permanently high and stop being a gate at all — checked
/// arithmetically across 40-240bpm and every grid, where a 7/8-beat gate
/// overran the next candidate in all nine combinations.
///
/// Under 1 in each case, so consecutive gates always have a gap rather than
/// merging into one continuous high.
///
///     samples = (samplesPerDivision * numerator) / denominator
constexpr int32_t kGateLongNum    = 3, kGateLongDen    = 4;  // most of the division
constexpr int32_t kGateShortNum   = 1, kGateShortDen   = 2;  // half
constexpr int32_t kGateRatchetNum = 1, kGateRatchetDen = 4;  // a stab

/// Gate high level for CV Out. About 5V on this hardware, which is
/// comfortably above Pulse In's threshold when self-patched.
constexpr int16_t kCvGateHigh = 1700;
constexpr int16_t kCvGateLow  = -2048;

// ---------------------------------------------------------------------------
// Flash
// ---------------------------------------------------------------------------
//
// The physical part, described once. calibstore.h and samplestore.h each
// carve a region out of it and both used to declare these themselves, which
// only compiled while no translation unit included both — adding USB put them
// together and the duplicate definitions broke the build.
//
// The map, low to high:
//
//   0x000000  firmware (code + baked samples)
//   0x080000  saved calibration, one 4KB sector   (calibstore.h)
//   0x100000  user samples, 1MB                   (samplestore.h)
//
// Both stored regions sit at FIXED offsets so reflashing firmware never moves
// or wipes them. CMakeLists.txt's checksize.cmake step fails the build if the
// image grows into the first of them.
constexpr uint32_t kFlashBase   = 0x10000000u;
constexpr uint32_t kFlashSize   = 2u * 1024 * 1024;

/// RP2040 flash erases a sector at a time; every erase must be sector-aligned.
constexpr uint32_t kFlashSector = 4096;

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

/// Half brightness. Used where two states must be told apart at a glance
/// rather than merely be visible: a muted group's hits against an audible
/// one's, and the click on LED 5.
///
/// Not kLedFull/2 — the LEDs are driven by PWM against an eye whose response
/// is closer to logarithmic, so half the duty cycle reads a good deal more
/// than half as bright. 1200 of 4095 is about where "clearly dimmer, still
/// obviously lit" sits; trim it on the bench if it reads wrong.
constexpr uint16_t kLedHalf = 1200;

/// The mode reminder: the shift button of the current mode pulses at this
/// brightness, so the panel always says which mode you are in without
/// spending an LED on it.
constexpr uint16_t kLedQuarter = 500;

} // namespace nko
