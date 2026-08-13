#!/usr/bin/env python3
"""ghostsim.py - a model of NIBBLE's level detector and the GHOST rule.

There is no host C++ compiler on this machine, so this file is how the riskiest
logic on the card gets verified. It is a deliberate line-by-line port of
LevelTracker in ../levels.cpp: same integer arithmetic, same shifts, same
constants. If levels.cpp changes, CHANGE THIS TOO - or delete it rather than
let it drift into telling you a comfortable lie.

Two things are checked here:

  1. THE GHOST RULE. Four Voltages does not return to a rest voltage when you
     let go: the output sits at the last-pressed button's level. So releasing AB
     leaves the CV at A's voltage, indistinguishable from pressing A. The ghost
     rule suppresses exactly that one level, and only until the CV moves
     somewhere else. These tests assert the resulting note stream for real
     playing gestures.

  2. THE LEARN PASS END TO END. Not merely "did it capture ten numbers", but:
     drive a simulated learn, build the threshold table from it, then replay
     every combo through the finished detector and assert each one classifies
     back to itself. That is the property that actually matters, and it is the
     one that catches an off-by-one in the sort or the threshold midpoints.

Run:  python tools/ghostsim.py
"""

import sys

# ---------------------------------------------------------------------------
# Constants - must match levels.h exactly
# ---------------------------------------------------------------------------

CTRL_RATE      = 3000
SETTLE_TOL     = 24
SETTLE_TICKS   = CTRL_RATE // 80      # 37 ticks = 12.3ms
DEADBAND       = 16
MATCH_WINDOW   = 96
COLLISION_MIN  = 32
CV_SMOOTH_SHIFT = 3

NUM_SINGLES = 4
NUM_LEVELS  = 10

# AB AC AD BC BD CD
PAIR_MEMBERS = [(0, 1), (0, 2), (0, 3), (1, 2), (1, 3), (2, 3)]

NAMES = ["A", "B", "C", "D", "AB", "AC", "AD", "BC", "BD", "CD"]

# Ergonomic learn order: singles, then rows, columns, diagonals.
# Deliberately NOT index order - see levels.h.
LEARN_ORDER = [0, 1, 2, 3, 4, 9, 5, 8, 6, 7]

NONE, TRIGGER, GHOST_ARMED = "None", "Trigger", "GhostArmed"


def is_member_of(s, pair):
    """True if single `s` is one of pair `pair`'s two buttons."""
    if pair < NUM_SINGLES or pair >= NUM_LEVELS:
        return False
    if s < 0 or s >= NUM_SINGLES:
        return False
    return s in PAIR_MEMBERS[pair - NUM_SINGLES]


def asr(v, n):
    """Arithmetic shift right, matching C's >> on a signed int32.

    Python's >> already floors toward negative infinity, which is what an
    arithmetic shift does. Spelled out because the difference from C's
    division-truncation is a real source of port bugs.
    """
    return v >> n


def slew_noStall(v, target, shift):
    """One-pole slew that always reaches its target.

    The naive `v += (target - v) >> shift` STALLS: once the difference is
    smaller than 2^shift the shift rounds to zero and the filter stops moving,
    permanently short of the target. On a signed shift the stall is also
    ASYMMETRIC - it lands exactly when approaching from above, and ~2^shift
    short when approaching from below - so the same physical voltage reads
    differently depending on which combo you came from.

    That was measured here at 17 units of direction-dependent error, over a
    quarter of kCollisionMin, and it put two combos on the wrong side of a
    threshold in the learn round-trip test.

    The guard is the same one 18_chord_organ uses at main.cpp:136-138: if the
    step rounds to zero but a difference remains, move one LSB.
    """
    d = target - v
    if d == 0:
        return v
    step = asr(d, shift)
    if step == 0:
        step = 1 if d > 0 else -1
    return v + step


# ---------------------------------------------------------------------------
# The tracker
# ---------------------------------------------------------------------------

class LevelTracker:
    def __init__(self):
        self.level   = [0] * NUM_LEVELS      # learned raw CV per combo
        self.sorted_ = list(range(NUM_LEVELS))
        self.slot_of = list(range(NUM_LEVELS))
        self.thresh  = [0] * (NUM_LEVELS - 1)
        self.learned = False
        self.collisions = 0

        self.smooth     = 0
        self.cand_mean  = 0
        self.cand_ticks = 0
        self.current    = -1
        self.ghost      = -1
        self.shift      = -1
        self.primed     = False

    # -- table construction -------------------------------------------------

    def rebuild(self):
        """Sort learned levels and derive the decision thresholds.

        Insertion sort of ten elements - no qsort, which is flash-resident on
        the card and absurd for this size.
        """
        self.sorted_ = list(range(NUM_LEVELS))
        for i in range(1, NUM_LEVELS):
            key = self.sorted_[i]
            j = i - 1
            while j >= 0 and self.level[self.sorted_[j]] > self.level[key]:
                self.sorted_[j + 1] = self.sorted_[j]
                j -= 1
            self.sorted_[j + 1] = key

        for k in range(NUM_LEVELS - 1):
            self.thresh[k] = (self.level[self.sorted_[k]]
                              + self.level[self.sorted_[k + 1]]) >> 1

        for k in range(NUM_LEVELS):
            self.slot_of[self.sorted_[k]] = k

    def init_default(self):
        """Even spread across -1500..+1500, used before any learn has happened.

        The card still plays; the LED indicator shows it is uncalibrated.
        """
        for i in range(NUM_LEVELS):
            self.level[i] = -1500 + (3000 * i) // (NUM_LEVELS - 1)
        self.learned = False
        self.collisions = 0
        self.rebuild()

    def learn_from(self, levels):
        """Install a full set of learned levels and count collisions."""
        self.collisions = 0
        for i in range(NUM_LEVELS):
            self.level[i] = levels[i]
        for i in range(NUM_LEVELS):
            for j in range(i):
                if abs(self.level[i] - self.level[j]) < COLLISION_MIN:
                    self.collisions += 1
        self.learned = True
        self.rebuild()

    # -- matching -----------------------------------------------------------

    def match(self, v, cur):
        """Settled value -> combo index, or -1 if unrecognised.

        Schmitt deadband biased toward the level we are already on, lifted in
        spirit from 11_goldfish/quantiser.h's note_in_up / note_in_down idiom.
        """
        k = 0
        while k < NUM_LEVELS - 1 and v > self.thresh[k]:
            k += 1
        cand = self.sorted_[k]

        # Hysteresis on ADJACENT slots only. Non-adjacent jumps are unambiguous
        # and delaying them would add latency for nothing.
        if cur >= 0 and cand != cur:
            cur_slot = self.slot_of[cur]
            if cur_slot == k + 1 and v > self.thresh[k] - DEADBAND:
                cand = cur
            elif cur_slot == k - 1 and v < self.thresh[k - 1] + DEADBAND:
                cand = cur

        # A triple or the all-four combo lands in SOME slot but far from its
        # centre. Rejecting on distance is what makes them safely ignored.
        if abs(v - self.level[cand]) > MATCH_WINDOW:
            return -1
        return cand

    # -- the detect step ----------------------------------------------------

    def step(self, cv_in):
        """One control tick. Returns (event, combo_index)."""
        # 1. Smooth. Must not stall - see slew_noStall.
        self.smooth = slew_noStall(self.smooth, cv_in, CV_SMOOTH_SHIFT)

        # 2. Settle detector. Restarting the MEAN (not just a counter) on every
        #    excursion means a slow drift never accumulates into a false settle.
        if self.cand_ticks > 0 and abs(self.smooth - self.cand_mean) <= SETTLE_TOL:
            self.cand_mean = slew_noStall(self.cand_mean, self.smooth, 4)
            if self.cand_ticks < SETTLE_TICKS:
                self.cand_ticks += 1
        else:
            self.cand_mean = self.smooth
            self.cand_ticks = 1
            return (NONE, -1)

        if self.cand_ticks < SETTLE_TICKS:
            return (NONE, -1)

        # 3. Match.
        m = self.match(self.cand_mean, self.current)
        if m < 0:
            return (NONE, -1)          # unrecognised: stay latched
        if m == self.current:
            return (NONE, -1)          # no change

        # 4. THE GHOST RULE.
        #    Leaving the ghost clears it, unconditionally and FIRST - so
        #    returning to that level later from elsewhere is a genuine press.
        #    This ordering is the whole rule.
        self.ghost = -1

        # Was the level we are LEAVING a pair, and the level we ARRIVE at one
        # of its two members? That is a release falling back to residue.
        if self.current >= NUM_SINGLES and is_member_of(m, self.current):
            self.ghost = m
            self.current = m
            return (GHOST_ARMED, m)

        # Latch which single we came FROM. The voltage for A+B is identical
        # whichever button went down first; the level being LEFT is the one
        # that was already held, and that is the only place the order survives.
        if m >= NUM_SINGLES and 0 <= self.current < NUM_SINGLES:
            self.shift = self.current
        elif m < NUM_SINGLES:
            self.shift = -1

        self.current = m

        # At power-on the Four Voltages output is already sitting at whatever
        # was last pressed before power-off. Swallow exactly one settle so the
        # card does not fire a note nobody asked for.
        if not self.primed:
            self.primed = True
            return (NONE, -1)

        return (TRIGGER, m)


# ---------------------------------------------------------------------------
# Simulated hardware
# ---------------------------------------------------------------------------

class FourVoltages:
    """A model of the Four Voltages output as NIBBLE sees it.

    Two behaviours matter and both are modelled:
      - finite slew: the voltage ramps between levels rather than stepping, so
        the settle detector has something real to reject.
      - no rest state: releasing everything leaves the output at the last
        SINGLE that was held, which is the entire reason the ghost rule exists.
    """

    def __init__(self, levels, slew_ticks=6, noise=0, seed=12345):
        self.levels = levels
        self.slew_ticks = slew_ticks
        self.noise = noise
        self.value = float(levels[0])
        self.target = float(levels[0])
        self._rng = seed

    def _rand(self):
        # xorshift32, same generator family as the card's fastmath.h
        s = self._rng
        s ^= (s << 13) & 0xFFFFFFFF
        s ^= s >> 17
        s ^= (s << 5) & 0xFFFFFFFF
        self._rng = s & 0xFFFFFFFF
        return self._rng

    def set_combo(self, combo):
        self.target = float(self.levels[combo])

    def tick(self):
        d = self.target - self.value
        if abs(d) < 0.5:
            self.value = self.target
        else:
            self.value += d / self.slew_ticks
        v = self.value
        if self.noise:
            v += (self._rand() % (2 * self.noise + 1)) - self.noise
        return int(round(v))


def spaced_levels(lo=-1400, hi=1400):
    """Ten evenly spaced levels - the well-behaved case."""
    return [lo + (hi - lo) * i // (NUM_LEVELS - 1) for i in range(NUM_LEVELS)]


def squashed_levels():
    """A deliberately bad knob position: two pairs land almost on top of each
    other, so the collision warning must fire and those two must be the only
    ones that fail to round-trip."""
    lv = spaced_levels()
    lv[5] = lv[4] + 20        # AC sits 20 units from AB - well under COLLISION_MIN
    return lv


# ---------------------------------------------------------------------------
# Gesture driver
# ---------------------------------------------------------------------------

def play(tracker, hw, gesture, hold_ticks=80):
    """Run a gesture and return the list of (event, name) that fired.

    `gesture` is a list of combo indices to move through. GhostArmed events are
    reported too, because "it was suppressed" is exactly what we assert on.
    """
    out = []
    for combo in gesture:
        hw.set_combo(combo)
        for _ in range(hold_ticks):
            ev, idx = tracker.step(hw.tick())
            if ev != NONE:
                out.append((ev, NAMES[idx]))
    return out


def prime(tracker, hw, combo, ticks=80):
    """Settle onto a starting combo and swallow the power-on blip."""
    hw.set_combo(combo)
    for _ in range(ticks):
        tracker.step(hw.tick())


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

FAILURES = []


def check(name, got, want):
    if got == want:
        print("  PASS  %s" % name)
    else:
        print("  FAIL  %s" % name)
        print("          got:  %r" % (got,))
        print("          want: %r" % (want,))
        FAILURES.append(name)


def test_drum_gesture():
    """Hold C, tap A, tap B, tap A - the DRUMS playing technique.

    The held finger is bank-select, the tapping finger plays. Each release
    falls back to C and must be SUPPRESSED, or every tap would sound twice.
    """
    lv = spaced_levels()
    t = LevelTracker(); t.learn_from(lv)
    hw = FourVoltages(lv)

    prime(t, hw, 2)                       # settle on C, swallow the boot blip
    seq = play(t, hw, [5, 2, 7, 2, 5, 2])  # AC C BC C AC C

    check("drums: hold C, tap A/B/A",
          seq,
          [(TRIGGER, "AC"), (GHOST_ARMED, "C"),
           (TRIGGER, "BC"), (GHOST_ARMED, "C"),
           (TRIGGER, "AC"), (GHOST_ARMED, "C")])


def test_ab_release_then_b():
    """AB -> release to A (ghost, silent) -> press B alone.

    The case specified by hand: B was part of AB, but the ghost is A, so B is a
    genuine new press and must trigger.
    """
    lv = spaced_levels()
    t = LevelTracker(); t.learn_from(lv)
    hw = FourVoltages(lv)

    prime(t, hw, 0)                       # start on A
    seq = play(t, hw, [4, 0, 1])          # AB -> A (ghost) -> B

    check("keys: AB -> A(ghost) -> B triggers",
          seq,
          [(TRIGGER, "AB"), (GHOST_ARMED, "A"), (TRIGGER, "B")])


def test_ghost_cleared_on_leaving():
    """AB -> A (ghost) -> C -> A. The final A must trigger: the ghost was
    cleared the moment we left it."""
    lv = spaced_levels()
    t = LevelTracker(); t.learn_from(lv)
    hw = FourVoltages(lv)

    prime(t, hw, 0)
    seq = play(t, hw, [4, 0, 2, 0])       # AB -> A(ghost) -> C -> A

    check("keys: ghost cleared on leaving, A retriggers",
          seq,
          [(TRIGGER, "AB"), (GHOST_ARMED, "A"),
           (TRIGGER, "C"), (TRIGGER, "A")])


def test_pair_to_pair():
    """Pair straight to another pair with no release: both trigger, no ghost."""
    lv = spaced_levels()
    t = LevelTracker(); t.learn_from(lv)
    hw = FourVoltages(lv)

    prime(t, hw, 0)
    seq = play(t, hw, [4, 9])             # AB -> CD

    check("keys: pair -> pair, no ghost",
          seq,
          [(TRIGGER, "AB"), (TRIGGER, "CD")])


def test_pair_falls_to_non_member():
    """AB -> C. C is not a member of AB, so this is a genuine press and must
    trigger. The card must never silently swallow a press."""
    lv = spaced_levels()
    t = LevelTracker(); t.learn_from(lv)
    hw = FourVoltages(lv)

    prime(t, hw, 0)
    seq = play(t, hw, [4, 2])             # AB -> C

    check("keys: pair -> non-member single triggers",
          seq,
          [(TRIGGER, "AB"), (TRIGGER, "C")])


def test_triples_ignored():
    """A triple produces a voltage no learned level claims. It must be ignored
    entirely, leaving the current note latched - not snapped to a neighbour."""
    lv = spaced_levels()
    t = LevelTracker(); t.learn_from(lv)
    hw = FourVoltages(lv)

    prime(t, hw, 0)
    # A triple lands between two learned levels, far from either centre.
    triple_v = (lv[4] + lv[5]) // 2
    hw.levels = lv + [triple_v]
    seq = play(t, hw, [4, 10, 4])         # AB -> triple -> AB

    check("keys: triple ignored, note stays latched",
          seq,
          [(TRIGGER, "AB")])


def test_boot_blip_suppressed():
    """At power-on the output already sits at the last thing pressed before
    power-off. That first settle must NOT fire a note."""
    lv = spaced_levels()
    t = LevelTracker(); t.learn_from(lv)
    hw = FourVoltages(lv)

    hw.set_combo(3)                       # card powers up with D already held
    seq = []
    for _ in range(120):
        ev, idx = t.step(hw.tick())
        if ev != NONE:
            seq.append((ev, NAMES[idx]))

    check("boot: first settle is swallowed", seq, [])


def test_noise_immunity():
    """With ADC noise at the tolerance edge, a held combo must not flicker."""
    lv = spaced_levels()
    t = LevelTracker(); t.learn_from(lv)
    hw = FourVoltages(lv, noise=SETTLE_TOL // 2)

    prime(t, hw, 0)
    seq = play(t, hw, [4], hold_ticks=1200)   # hold AB for 0.4s

    check("noise: held combo fires exactly once",
          seq, [(TRIGGER, "AB")])


def test_learn_roundtrip():
    """THE ONE THAT MATTERS.

    Drive a full learn in the ergonomic order, build the table, then replay
    every combo and assert each classifies back to itself. This proves the
    learn produces a table that WORKS - not merely that it captured ten
    numbers. An off-by-one in the sort or the midpoints dies here.
    """
    lv = spaced_levels()
    hw = FourVoltages(lv)

    # Simulate the learn: for each step in the ergonomic order, hold the combo,
    # let it settle, then "tap" - capturing the smoothed value.
    learner = LevelTracker()
    captured = [0] * NUM_LEVELS
    for step in LEARN_ORDER:
        hw.set_combo(step)
        for _ in range(120):
            learner.step(hw.tick())
        # The tap captures the settled reading.
        captured[step] = learner.cand_mean

    t = LevelTracker()
    t.learn_from(captured)

    check("learn: no false collisions on a clean spread", t.collisions, 0)

    # Replay every combo through the finished detector.
    bad = []
    for combo in range(NUM_LEVELS):
        r = LevelTracker()
        r.learn_from(captured)
        r.primed = True                   # we are testing classification, not boot
        h2 = FourVoltages(lv)
        prime(r, h2, combo)
        if r.current != combo:
            bad.append((NAMES[combo],
                        NAMES[r.current] if r.current >= 0 else "unmatched"))

    check("learn: every combo round-trips to itself", bad, [])


def test_no_directional_bias():
    """REGRESSION. A settled reading must not depend on which combo it was
    approached from.

    The naive one-pole `v += (target-v) >> shift` stalls once the difference
    drops below 2^shift, and the stall is asymmetric on a signed shift: exact
    from above, ~2^shift short from below. That produced 17 units of
    direction-dependent error - over a quarter of COLLISION_MIN - and put two
    combos on the wrong side of a threshold. See slew_noStall.

    Asserting zero (not "small") because the guard makes it exactly zero, and a
    non-zero result means the guard has been dropped.
    """
    lv = spaced_levels()
    worst = 0
    for combo in range(NUM_LEVELS):
        for start in range(NUM_LEVELS):
            if start == combo:
                continue
            hw = FourVoltages(lv)
            t = LevelTracker()
            hw.set_combo(start)
            for _ in range(200):
                t.step(hw.tick())
            hw.set_combo(combo)
            for _ in range(300):
                t.step(hw.tick())
            worst = max(worst, abs(t.cand_mean - lv[combo]))

    check("learn: no directional capture bias (90 approaches)", worst, 0)


def other_member(pair, one):
    if pair < NUM_SINGLES or pair >= NUM_LEVELS:
        return -1
    a, b = PAIR_MEMBERS[pair - NUM_SINGLES]
    if a == one:
        return b
    if b == one:
        return a
    return -1


def test_ordered_pairs_distinguished():
    """THE ORDERING. hold-A-tap-B must be told apart from hold-B-tap-A.

    The two close the same switches, so the resistor network gives ONE voltage
    for both - press order is not in the signal at all. What is available is
    the level the CV came FROM, which is the button that was already held.

    This asserts all twelve ordered gestures resolve to the right (shift, tap),
    which is what lets DRUMS carry twelve voices instead of six.
    """
    lv = spaced_levels()
    got = {}
    for shift in range(NUM_SINGLES):
        for tap in range(NUM_SINGLES):
            if shift == tap:
                continue
            pair = None
            for p in range(NUM_SINGLES, NUM_LEVELS):
                if is_member_of(shift, p) and is_member_of(tap, p):
                    pair = p
            t = LevelTracker()
            t.learn_from(lv)
            hw = FourVoltages(lv)
            prime(t, hw, shift)          # hold the shift
            play(t, hw, [pair])          # tap the other one
            got[(shift, tap)] = (t.shift, other_member(t.current, t.shift))

    bad = [(NAMES[s] + "+" + NAMES[tp], v)
           for (s, tp), v in got.items() if v != (s, tp)]
    check("drums: all 12 ordered gestures resolve correctly", bad, [])

    # And the specific case that motivated it.
    check("drums: A-held+B is distinct from B-held+A",
          got[(0, 1)] != got[(1, 0)], True)


def test_shift_cleared_on_bare_single():
    """Returning to a bare single must drop the shift, or a later pair reached
    some other way would inherit a stale one."""
    lv = spaced_levels()
    t = LevelTracker(); t.learn_from(lv)
    hw = FourVoltages(lv)
    prime(t, hw, 2)              # hold C
    play(t, hw, [5])             # tap A -> AC, shift should be C
    check("drums: shift latched while pair held", t.shift, 2)
    play(t, hw, [1])             # go to a bare B
    check("drums: shift cleared on a bare single", t.shift, -1)


def test_learn_collision_detected():
    """A squashed knob position must be REPORTED, and the learn must still
    complete. Degrading to a warning beats stalling on the combo it cannot
    separate."""
    lv = squashed_levels()
    t = LevelTracker()
    t.learn_from(lv)

    check("learn: collision detected on squashed spread",
          t.collisions > 0, True)
    check("learn: still completes despite collision", t.learned, True)


def test_learn_order_is_a_permutation():
    """The ergonomic order must hit all ten combos exactly once - an easy thing
    to typo and a silent disaster if wrong."""
    check("learn: order is a permutation of all 10",
          sorted(LEARN_ORDER), list(range(NUM_LEVELS)))


def main():
    print("NIBBLE ghost/level model")
    print()
    print("Ghost rule:")
    test_drum_gesture()
    test_ab_release_then_b()
    test_ghost_cleared_on_leaving()
    test_pair_to_pair()
    test_pair_falls_to_non_member()
    test_triples_ignored()
    test_boot_blip_suppressed()
    test_noise_immunity()
    print()
    print("Learn:")
    test_learn_order_is_a_permutation()
    test_no_directional_bias()
    test_ordered_pairs_distinguished()
    test_shift_cleared_on_bare_single()
    test_learn_roundtrip()
    test_learn_collision_detected()
    print()

    if FAILURES:
        print("%d FAILED: %s" % (len(FAILURES), ", ".join(FAILURES)))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
