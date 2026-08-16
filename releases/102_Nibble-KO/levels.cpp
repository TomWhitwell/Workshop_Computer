// levels.cpp — implementation of the level detector and the ghost rule.
//
// A line-by-line counterpart of tools/ghostsim.py. Keep them in step.
//
// Everything here runs at the 3kHz control rate, inline inside ProcessSample()
// — which is a DMA interrupt handler. Integer only, no division in Step(), no
// allocation, no blocking.

#include "levels.h"
#include "fastmath.h"

// For __not_in_flash_func. Included explicitly rather than relying on
// ComputerCard.h pulling it in transitively — this file does not need the rest
// of that header, and an implicit dependency on a macro is how a build breaks
// mysteriously later. (pico/platform.h refuses to be included directly.)
#include "pico.h"

namespace nko {

namespace {

/// abs() for int32_t without dragging in <cstdlib>.
inline int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

} // namespace

// ---------------------------------------------------------------------------
// Table construction — runs on learn completion and at boot, never in the hot
// path, so clarity beats cleverness here.
// ---------------------------------------------------------------------------

void LevelTracker::Rebuild()
{
	// Insertion sort of ten elements by learned level. Not qsort: it is
	// flash-resident on this platform and absurd for this size.
	for (int i = 0; i < kNumLevels; i++) sorted_[i] = static_cast<uint8_t>(i);

	for (int i = 1; i < kNumLevels; i++)
	{
		uint8_t key = sorted_[i];
		int j = i - 1;
		while (j >= 0 && level_[sorted_[j]] > level_[key])
		{
			sorted_[j + 1] = sorted_[j];
			j--;
		}
		sorted_[j + 1] = key;
	}

	// A decision threshold sits midway between each adjacent pair of levels.
	for (int k = 0; k < kNumLevels - 1; k++)
		thresh_[k] = (level_[sorted_[k]] + level_[sorted_[k + 1]]) >> 1;

	// Match() needs to ask "which slot is the combo I am currently on?", which
	// is the inverse permutation.
	for (int k = 0; k < kNumLevels; k++)
		slotOf_[sorted_[k]] = static_cast<uint8_t>(k);
}

void LevelTracker::InitDefault()
{
	// An even spread, so an uncalibrated card still does something musical
	// rather than nothing at all.
	for (int i = 0; i < kNumLevels; i++)
		level_[i] = kDefaultLo + (kDefaultHi - kDefaultLo) * i / (kNumLevels - 1);

	learned_    = false;
	collisions_ = 0;
	Rebuild();
}

void LevelTracker::LearnFrom(const int32_t *levels)
{
	collisions_ = 0;
	for (int i = 0; i < kNumLevels; i++) level_[i] = levels[i];

	// Two levels closer than kCollisionMin overlap once kMatchWindow and the
	// Schmitt band are taken into account. The capture is still kept — a learn
	// that completes with a warning is far more useful than one that refuses —
	// but the player is told, and the Play-mode indicator keeps saying so.
	for (int i = 0; i < kNumLevels; i++)
		for (int j = 0; j < i; j++)
			if (iabs(level_[i] - level_[j]) < kCollisionMin && collisions_ < 255)
				collisions_++;

	learned_ = true;
	Rebuild();
}

void LevelTracker::ResetHeld()
{
	current_   = kComboNone;
	ghost_     = kComboNone;
	ghostFrom_ = kComboNone;
	shift_     = kComboNone;
	// The settle plateau is left running: the CV has not moved, so re-deriving
	// it from scratch would only add a spurious 12ms of latency to the first
	// press after a learn.
}

// ---------------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------------

int8_t LevelTracker::Match(int32_t v, int8_t cur) const
{
	// Walk the sorted thresholds. Ten entries, so a linear walk beats a binary
	// search on an M0+ once branch cost is counted.
	int k = 0;
	while (k < kNumLevels - 1 && v > thresh_[k]) k++;
	int8_t cand = static_cast<int8_t>(sorted_[k]);

	// Schmitt hysteresis, biased toward the level we are already on, in the
	// spirit of 11_goldfish/quantiser.h's note_in_up / note_in_down idiom.
	//
	// Applied to ADJACENT slots only: a jump of two or more slots is
	// unambiguous, and making it wait would add latency for nothing.
	// The `k >= 1` on the second branch is defensive only, and deliberately so:
	// slotOf_ always holds 0..kNumLevels-1, so reaching it with k == 0 would
	// require curSlot == -1, which cannot happen. It costs nothing and it stops
	// a future edit to the sort from turning a logic slip into an out-of-bounds
	// read of thresh_[-1]. Verified unreachable in tools/ghostsim.py.
	if (cur >= 0 && cand != cur)
	{
		int curSlot = slotOf_[cur];
		if (curSlot == k + 1 && v > thresh_[k] - kDeadband)
			cand = cur;
		else if (curSlot == k - 1 && k >= 1 && v < thresh_[k - 1] + kDeadband)
			cand = cur;
	}

	// The value fell in this slot, but is it actually NEAR the level that owns
	// the slot? A triple or the all-four combo lands somewhere in the range but
	// far from any learned centre. Rejecting on distance is what makes them
	// safely ignorable rather than silently snapping to a neighbour.
	if (iabs(v - level_[cand]) > kMatchWindow) return kComboNone;
	return cand;
}

// ---------------------------------------------------------------------------
// The detect step
// ---------------------------------------------------------------------------

LevelEvent __not_in_flash_func(LevelTracker::Step)(int32_t cvIn, int8_t &idx)
{
	// 1. Smooth the raw input.
	//
	// slew_exact, NOT slew: the plain shift stalls short of its target and does
	// so asymmetrically, which makes a settled reading depend on the direction
	// it was approached from. That cost 17 units of error here and broke the
	// learn round-trip. See fastmath.h.
	smooth_ = slew_exact(smooth_, cvIn, kCvSmoothShift);

	// 2. Settle detector.
	//
	// The mean is RESTARTED on every excursion rather than a counter merely
	// being reset, so a slow drift can never accumulate into a false settle:
	// each tick outside tolerance begins a fresh plateau.
	if (candTicks_ > 0 && iabs(smooth_ - candMean_) <= kSettleTol)
	{
		candMean_ = slew_exact(candMean_, smooth_, 4);
		if (candTicks_ < kSettleTicks) candTicks_++;
	}
	else
	{
		candMean_  = smooth_;
		candTicks_ = 1;
		return LevelEvent::None;          // still moving
	}

	if (candTicks_ < kSettleTicks) return LevelEvent::None;

	// 3. Match the settled plateau to a learned level.
	int8_t m = Match(candMean_, current_);
	if (m == kComboNone)  return LevelEvent::None;   // unrecognised: stay latched
	if (m == current_)    return LevelEvent::None;   // nothing changed

	// 4. THE GHOST RULE.
	//
	// Clearing the ghost happens FIRST and unconditionally. That single
	// ordering decision IS the rule: it is what makes "leaving a ghost level
	// re-arms it as a genuine note" true without any extra bookkeeping.
	ghost_     = kComboNone;
	ghostFrom_ = kComboNone;

	// Are we leaving a PAIR and arriving at one of ITS OWN two members? That is
	// a release falling back to residue, not a press.
	if (current_ >= kNumSingles && IsMemberOf(m, current_))
	{
		ghost_     = m;
		ghostFrom_ = current_;   // remembered for the display only
		current_   = m;
		idx        = m;
		return LevelEvent::GhostArmed;
	}

	// Latch WHICH single we came from, if we are arriving at a pair from one.
	//
	// This is the only place the press order survives: the voltage for A+B is
	// the same whichever went down first, but the level we are LEAVING is the
	// button that was already held. See Shift() in the header.
	if (m >= kNumSingles && current_ >= 0 && current_ < kNumSingles)
		shift_ = current_;
	else if (m < kNumSingles)
		shift_ = kComboNone;      // back on a bare single: no shift in play

	// Everything else is a genuine press: another pair, a different pair's
	// member, a single that was not part of the pair we left, or any move at
	// all starting from a single.
	current_ = m;
	idx      = m;

	// Swallow the very first settle after power-on — the Four Voltages output
	// is already sitting at whatever was last pressed before the card was
	// switched off, and firing a note for that is startling.
	if (!primed_)
	{
		primed_ = true;
		return LevelEvent::None;
	}

	return LevelEvent::Trigger;
}

} // namespace nko
