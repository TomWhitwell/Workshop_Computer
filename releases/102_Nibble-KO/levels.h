// levels.h — turning one Four Voltages output into a stream of note events.
//
// This is the heart of the card, and the one piece of logic that cannot be
// re-derived by reading the rest of the code. It is modelled and tested in
// tools/ghostsim.py, which is a line-by-line port of this file. IF YOU CHANGE
// THIS, CHANGE THAT — or delete it rather than let it drift.
//
// ---------------------------------------------------------------------------
// THE PROBLEM
// ---------------------------------------------------------------------------
//
// Four Voltages has four non-latching buttons feeding a resistor network. Each
// combination produces a distinct voltage, so in principle a level detector
// gives you a 10-note keyboard for one patch cable.
//
// But the module does NOT return to a rest voltage when you let go: the output
// sits at the last-pressed button's level. Release AB and the CV falls to A's
// voltage — which is exactly what pressing A looks like. A naive detector fires
// a spurious note on every single release.
//
// ---------------------------------------------------------------------------
// THE GHOST RULE
// ---------------------------------------------------------------------------
//
// When a PAIR is active and the level then settles on one of that pair's two
// constituent singles, that specific level is a GHOST — the residue of a
// release, not a press. Suppress it: no retrigger, the pair stays sounding.
//
// ANY other recognised level is a genuine press and triggers, including a bare
// single that was part of the pair we just left. So:
//
//     AB -> (release) -> A     ghost, silent
//     AB -> (release) -> A -> B   B TRIGGERS: the ghost is A, not B
//     AB -> (release) -> A -> C -> A   the second A TRIGGERS: leaving cleared it
//
// Only ever ONE level is suppressed at a time, and it is cleared the instant
// the CV moves anywhere else. That is what keeps the rule comprehensible while
// you are playing rather than reading.
//
// ---------------------------------------------------------------------------
// WHY THIS IS GOOD RATHER THAN A WORKAROUND
// ---------------------------------------------------------------------------
//
// Hold C, tap A: you get AC, and the release back to C is silent. Tap B: you
// get BC, silent release. The held finger is a bank-select and the tapping
// finger plays, indefinitely, with no spurious hits. That falls out of the rule
// with no special-casing, and it is the intended way to play DRUMS.

#pragma once
#include <stdint.h>
#include "nibbleko.h"

namespace nko {

// ---------------------------------------------------------------------------
// Tolerances
// ---------------------------------------------------------------------------
//
// All in raw CV-in units: signed 12-bit, -2048..2047 over roughly +/-6V, so
// about 2.9mV per unit. These are ESTIMATES derived from the expected divider
// spacing, not measurements — they are grouped here because tuning them on
// hardware is expected. Sanity check: ten levels across even half the input
// range gives ~204 units of average gap, so kMatchWindow is under half a gap
// and kCollisionMin under a third.

/// A reading must stay within this of its own running mean to count as settled.
/// ~70mV: above the ADC noise floor, well below any usable gap.
constexpr int32_t kSettleTol = 24;

/// How long a plateau must hold before it counts. 37 ticks at 3kHz = 12.3ms:
/// long enough to reject a finger landing and switch bounce, short enough to be
/// imperceptible when playing.
constexpr int32_t kSettleTicks = kCtrlRate / 80;

/// Schmitt band around the level we are already on, so noise between two close
/// neighbours cannot flicker between them.
constexpr int32_t kDeadband = 16;

/// A settled value further than this from EVERY learned level is unrecognised.
/// This is what makes triples and the all-four combo safely ignorable: they
/// land in SOME threshold slot but far from its centre, so they match nothing
/// and the current note simply stays latched.
constexpr int32_t kMatchWindow = 96;

/// Two learned levels closer than this are reported as a collision.
///
/// This is a WARNING threshold, not the point where the detector stops working,
/// and the two are not the same number. The detector's real limit is set by
/// kSettleTol: a reading is accepted as settled within +/-24, so two levels are
/// genuinely ambiguous only once their gap drops below about 48, where a
/// reading could plausibly land on the wrong side of the midpoint between them.
///
/// This started at 64 — a guess made before any hardware existed, and 1.3x
/// stricter than even that theoretical limit. On the bench it cried wolf twice
/// over: combinations it flagged were being played reliably, first at 64 and
/// again at 40. A warning that fires on things that work teaches the player to
/// ignore the warning, which is worse than not warning at all.
///
/// 32 is deliberately well BELOW the theoretical limit, so it stays quiet
/// unless two levels are genuinely on top of each other. The model puts real
/// misclassification at gaps of 16 and under, so this still leaves a factor of
/// two — and everything between 32 and 48 is now trusted rather than flagged.
///
/// The trade is accepted knowingly: a pair in that band may occasionally
/// flicker under drift without having been flagged. Tuned against hardware
/// rather than theory, because the bench is the only thing that can say whether
/// a warning is useful or merely correct.
constexpr int32_t kCollisionMin = 32;

/// Input smoothing on the raw CV. Kills ADC dither without smearing an edge.
constexpr uint8_t kCvSmoothShift = 3;

/// Uncalibrated default spread, used before any learn has been done. The card
/// still plays; the LED indicator says it is guessing.
constexpr int32_t kDefaultLo = -1500;
constexpr int32_t kDefaultHi =  1500;

// ---------------------------------------------------------------------------

enum class LevelEvent : uint8_t {
	None,        ///< nothing to do, or unrecognised — keep the note latched
	Trigger,     ///< genuine new press on `idx`
	GhostArmed,  ///< fell back onto a released pair's member — SUPPRESSED
};

class LevelTracker
{
public:
	/// Install the even-spread default. Safe to call before any learn.
	void InitDefault();

	/// Install a captured set and rebuild the decision table.
	/// Counts collisions as a side effect; see CollisionCount().
	void LearnFrom(const int32_t *levels);

	/// Forget which combo is held, without touching the learned table.
	///
	/// Needed on the way out of learn mode. The detector keeps running during a
	/// learn (its settle state is what validates each capture tap), so by the
	/// end current_ names whichever combo was being calibrated last and ghost_
	/// may be armed. Carrying that into play means the first real press is
	/// compared against stale state — and if it happens to match, it is
	/// silently swallowed as "no change".
	///
	/// Deliberately leaves primed_ alone: the boot blip has long since been
	/// swallowed, and re-arming it would eat the player's first note after a
	/// calibration, which is exactly when they are trying to hear whether it
	/// worked.
	void ResetHeld();

	/// One control tick. `idx` receives the combo index on a non-None result.
	LevelEvent Step(int32_t cvIn, int8_t &idx);

	/// The level the CV is currently sitting on. This is the tracker's own
	/// state, and while a ghost is armed it is the RELEASED-ONTO single, not
	/// the pair you can still hear. For display, use Sounding().
	int8_t Current() const { return current_; }

	/// Which single was held when the current pair arrived, or kComboNone.
	///
	/// THE VOLTAGE CANNOT TELL YOU THIS. A+B and B+A close the same two
	/// switches, so the resistor network produces one identical level for both —
	/// press order is simply not in the signal.
	///
	/// What IS available is where the CV came FROM. Holding A and tapping B
	/// arrives at the AB level from A; holding B and tapping A arrives at the
	/// same level from B. Latching the previous single at the moment the pair
	/// triggers therefore recovers the ordering the voltage threw away, and
	/// turns six combinations into twelve distinguishable gestures.
	///
	/// Valid only while a pair is current. Reset by ResetHeld().
	int8_t Shift() const { return shift_; }

	/// The combo that is actually sounding.
	///
	/// Differs from Current() exactly while a ghost is armed: the CV has fallen
	/// back onto one of a released pair's members, but the pair is what last
	/// triggered and what is still audible. Showing Current() on the LEDs made
	/// the panel contradict the sound every time a finger came off a pair.
	int8_t Sounding() const
	{
		return (ghost_ != kComboNone && ghostFrom_ != kComboNone)
		     ? ghostFrom_ : current_;
	}

	/// True once the CV has held still long enough to be trusted. The learn
	/// machine uses this to reject a capture tap that arrived mid-transition.
	bool Settled() const { return candTicks_ >= kSettleTicks; }

	/// The settled value, valid when Settled(). This is what a capture takes.
	int32_t SettledValue() const { return candMean_; }

	bool    Learned() const        { return learned_; }
	uint8_t CollisionCount() const { return collisions_; }

private:
	int8_t Match(int32_t v, int8_t cur) const;
	void   Rebuild();

	// --- learned data ------------------------------------------------------
	// Saved to flash by calibstore.h once a learn succeeds, and restored at
	// boot instead of re-learning — see main.cpp's LearnTick()/EnterLearn().
	// The Four Voltages knob moving would invalidate a saved calibration
	// silently, but the knob is not expected to move in normal use, and
	// recalibrating on every power-up made the card unplayable within
	// seconds of switch-on. The escape hatch is the alt-boot learn, still one
	// gesture away, for when the knob genuinely has moved.
	int32_t level_[kNumLevels]     = {};
	uint8_t sorted_[kNumLevels]    = {};   // combo indices, ascending by level_
	uint8_t slotOf_[kNumLevels]    = {};   // inverse of sorted_, for hysteresis
	int32_t thresh_[kNumLevels - 1] = {};  // midpoints between adjacent levels
	bool    learned_    = false;
	uint8_t collisions_ = 0;

	// --- running detection -----------------------------------------------
	int32_t smooth_    = 0;
	int32_t candMean_  = 0;
	int32_t candTicks_ = 0;
	int8_t  current_   = kComboNone;
	int8_t  ghost_     = kComboNone;
	/// The pair the ghost was released FROM — what is still sounding. Kept only
	/// so the display can show it; the detection logic never reads it.
	int8_t  ghostFrom_ = kComboNone;
	/// The single the CV was sitting on when the current pair arrived.
	int8_t  shift_     = kComboNone;

	/// At power-on the Four Voltages output already sits at whatever was last
	/// pressed before power-off. Swallow exactly one settle so the card does
	/// not fire a note nobody asked for.
	bool primed_ = false;
};

} // namespace nko
