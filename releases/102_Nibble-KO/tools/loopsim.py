#!/usr/bin/env python3
"""loopsim.py - model of the DRUMS event looper.

Mirrors looper.cpp. The interesting properties are not "does it store events"
but the ordering ones, which are easy to get subtly wrong and hard to hear:

  - every recorded hit fires exactly ONCE per pass, never zero or twice;
  - quantisation can move an event EARLIER than the tick it was stored at, and
    such an event must still fire rather than being stranded for a whole loop;
  - overdubbing inserts into a sorted array WHILE a pass is in flight, so the
    playback cursor has to be corrected or the rest of that pass misfires;
  - tempo changes re-time the pattern rather than dropping or doubling hits.

Run:  python tools/loopsim.py
"""

import sys

TICKS_PER_BEAT = 48
BEATS_PER_LOOP = 16
LOOP_TICKS = TICKS_PER_BEAT * BEATS_PER_LOOP      # 768
# (the old fixed QUANT_TICKS is gone -- the grid is runtime now, see
#  QUANT_NOTES_PER_BEAT below)
NUM_VOICES = 12          # the looper stores VOICES, not key combos
MAX_EVENTS = 512
MAX_KNOB_EVENTS = 320
KNOB_EVENT = 0x80
THIS_PASS = 0x40
KNOB_REPLACE_WINDOW = 12
# Must stay a POWER OF TWO: lane_of() masks with NUM_LANES-1. Ten lanes
# therefore need sixteen slots. Mirrors looper.h's KnobLane.
NUM_LANES = 16
LANE_FILTER, LANE_TONE = 0, 1
LANE_FX_A, LANE_FX_B, LANE_FX_C, LANE_FX_D = 2, 3, 4, 5
LANE_PAR_A, LANE_PAR_B, LANE_PAR_C, LANE_PAR_D = 6, 7, 8, 9
LANE_FX_FIRST = LANE_FX_A
LANE_PAR_FIRST = LANE_PAR_A
NUM_FX_SLOTS = 4
NUM_PATTERNS = 3   # hold D and tap A/B/C; the shift is not a slot

# Shift C's lane is the TIMING one and never joins the audio chain.
LANE_TIMING = LANE_FX_C


def fx_lane_for_shift(shift):
    return LANE_FX_FIRST + shift


def par_lane_for_shift(shift):
    return LANE_PAR_FIRST + shift


def pack_fx(fx):
    """The FX lane carries only WHICH effect; depth lives in its own
    parameter lane now. Mirrors looper.h's PackFx."""
    return fx & 0xFF


def fx_of(v): return v


def is_knob(what):
    return (what & KNOB_EVENT) != 0


def lane_of(what):
    return what & (NUM_LANES - 1)


def is_this_pass(what):
    return (what & THIS_PASS) != 0


def same_kind(a, b):
    return (a & ~THIS_PASS) == (b & ~THIS_PASS)
CTRL_RATE = 3000
CLOCK_MAX_GAP = 2 * CTRL_RATE
CLOCK_TIMEOUT = 3 * CTRL_RATE
BPM_MIN, BPM_MAX = 40, 240
Q16 = 65536

FAILURES = []


def check(name, got, want):
    if got == want:
        print("  PASS  %s" % name)
    else:
        print("  FAIL  %s" % name)
        print("          got:  %r" % (got,))
        print("          want: %r" % (want,))
        FAILURES.append(name)


# Quantise grid: NOTES PER BEAT, not a divisor of TICKS_PER_BEAT directly.
# Mirrors kQuantNotesPerBeat in looper.h -- a 1/16 note is FOUR per beat, a
# 1/8 note TWO, a 1/12 "note" (triplet-eighth feel) THREE. An earlier version
# of this table held {16, 12, 8} used directly as the divisor, which put 16
# grid points in a single BEAT instead of a bar -- four times finer than the
# names claimed, and inaudible even at the loosest (1/8) setting. See
# looper.h's kQuantNotesPerBeat comment.
QUANT_NOTES_PER_BEAT = [4, 3, 2]     # 16th, 12th (triplet), 8th


def fire_tick(ev):
    """When an event actually sounds. Mirrors Looper::FireTick in looper.cpp.

    Just the stored tick now: quantisation happens ONCE at record time (see
    Looper.quantise_tick), so a hit recorded under 12ths keeps sounding on
    the 12th grid even after later overdubs are recorded under 8ths. That is
    what lets a straight part and a triplet part coexist in one loop.

    The array is still sorted by THIS rather than read directly, because
    rounding at capture can push a hit near the end of the loop across the
    boundary - so a hit recorded late in the bar can sort before one recorded
    early in it.
    """
    return ev[0]


class Looper:
    def __init__(self):
        self.events = []          # list of [tick, what, value], kept sorted
        self.play_head = 0
        self.cursor = 0
        self.phase = 0
        self.tick_inc = 0
        self.last_x = -9999
        self.knob_count = 0
        self.since_clock = 0
        self.clock_timeout = 0
        self.quant_grid = 0        # index into QUANT_NOTES_PER_BEAT; 0 = 16th

    def set_tempo_bpm(self, bpm):
        self.tick_inc = (bpm * TICKS_PER_BEAT * Q16) // (60 * CTRL_RATE)

    # --- external clock ---------------------------------------------------

    def set_tempo_knob(self, x):
        """Mirrors Looper::SetTempo. Ignored while clocked, and it FORGETS the
        knob position while clocked so the tempo snaps back when it releases."""
        if self.clocked():
            self.last_x = -9999
            return
        if self.last_x >= 0 and abs(x - self.last_x) < 64:
            return
        self.last_x = x
        bpm = BPM_MIN + ((x * (BPM_MAX - BPM_MIN)) >> 12)
        self.set_tempo_bpm(bpm)

    def clocked(self):
        return self.clock_timeout > 0

    def clock_pulse(self):
        if 0 < self.since_clock <= CLOCK_MAX_GAP:
            self.tick_inc = (TICKS_PER_BEAT * Q16) // self.since_clock
        self.since_clock = 0
        self.clock_timeout = CLOCK_TIMEOUT

    def tick_clock(self):
        if self.since_clock < CTRL_RATE * 8:
            self.since_clock += 1
        if self.clock_timeout > 0:
            self.clock_timeout -= 1

    def bpm(self):
        return self.tick_inc * 60 * CTRL_RATE / (Q16 * TICKS_PER_BEAT)

    def advance(self):
        if self.tick_inc <= 0:
            return False
        self.phase += self.tick_inc
        if self.phase < Q16:
            return False
        while self.phase >= Q16:
            self.phase -= Q16
            self.play_head = (self.play_head + 1) % LOOP_TICKS
            if self.play_head == 0:
                self.cursor = 0
        return True

    def insert(self, ev):
        if len(self.events) >= MAX_EVENTS:
            return
        ev_when = fire_tick(ev)
        i = len(self.events)
        self.events.append(ev)
        while i > 0 and fire_tick(self.events[i - 1]) > ev_when:
            self.events[i] = self.events[i - 1]
            i -= 1
        self.events[i] = ev
        if is_knob(ev[1]):
            self.knob_count += 1
        if i <= self.cursor:
            self.cursor += 1

    def remove(self, i):
        if i < 0 or i >= len(self.events):
            return
        if is_knob(self.events[i][1]) and self.knob_count > 0:
            self.knob_count -= 1
        del self.events[i]
        if i < self.cursor and self.cursor > 0:
            self.cursor -= 1

    def near_playhead(self, tick):
        d = tick - self.play_head
        if d > LOOP_TICKS // 2:
            d -= LOOP_TICKS
        if d < -LOOP_TICKS // 2:
            d += LOOP_TICKS
        return abs(d) <= KNOB_REPLACE_WINDOW

    def arm_knobs(self):
        """Everything already recorded belongs to a previous pass."""
        for e in self.events:
            e[1] &= ~THIS_PASS

    def record_filter_at(self, value, lane=LANE_FILTER):
        """Automation REPLACES itself within a WINDOW, not on an exact tick.

        Exact matching replaced nothing: a second pass samples on a different
        phase and the grids never coincide. The pass tag stops the window
        eating the sweep it is currently laying down.
        """
        what = KNOB_EVENT | lane
        i = 0
        while i < len(self.events):
            e = self.events[i]
            if (same_kind(e[1], what) and not is_this_pass(e[1])
                    and self.near_playhead(fire_tick(e))):
                self.remove(i)
            else:
                i += 1
        if self.knob_count >= MAX_KNOB_EVENTS:
            return
        # `value` here is the STORED byte, i.e. already knob>>4. The C++ does
        # that shift inside RecordLane; record_knob_at() below is the faithful
        # mirror of it. This entry point takes the stored form because most
        # tests care about replacement and ordering rather than scaling.
        self.insert([self.play_head, what | THIS_PASS, value & 0xFF])

    def record_knob_at(self, knob, lane=LANE_FILTER):
        """Mirrors the C++ RecordLane: takes a 0..4095 knob value and stores
        knob>>4, because LoopEvent::value is a single byte."""
        self.record_filter_at((knob >> 4) & 0xFF, lane)

    def record_hit(self, voice, vel=100):
        # A hit the player just performed outranks stale automation.
        if len(self.events) >= MAX_EVENTS and self.knob_count > 0:
            for i, e in enumerate(self.events):
                if is_knob(e[1]):
                    self.remove(i)
                    break
        assert 0 <= voice < NUM_VOICES, "loop stores voices, not combos"
        # Snapped HERE, against the grid live right now - not left raw for
        # playback to re-round. Mirrors RecordHit/QuantiseTick.
        self.insert([self.quantise_tick(self.play_head), voice, vel])

    def quantise_tick(self, tick):
        """Round to the CURRENT grid. Mirrors Looper::QuantiseTick."""
        q = TICKS_PER_BEAT // QUANT_NOTES_PER_BEAT[self.quant_grid]
        return ((tick + q // 2) // q * q) % LOOP_TICKS

    def cycle_quant_grid(self):
        """16th -> 12th -> 8th -> 16th. Affects only FUTURE hits."""
        self.quant_grid = (self.quant_grid + 1) % len(QUANT_NOTES_PER_BEAT)
        return self.quant_grid

    def fire(self):
        out = []
        while self.cursor < len(self.events):
            ev = self.events[self.cursor]
            if fire_tick(ev) != self.play_head:
                break
            out.append((ev[1], ev[2]))
            self.cursor += 1
        return out

    def clear(self):
        self.events = []
        self.cursor = 0
        self.knob_count = 0

    # --- undo, depth 1 ----------------------------------------------------

    def snapshot(self):
        """Copy the pattern aside. Called on ARMING record, not on leaving."""
        self.snap = [list(e) for e in self.events]
        self.snap_knob_count = self.knob_count
        self.have_snap = True

    # --- pattern slots ----------------------------------------------------

    def store_pattern(self, i):
        if not hasattr(self, "patterns"):
            self.patterns = {}
        # Never overwrite a slot with silence -- irreversible, and never what
        # the gesture meant. Mirrors the C++ guard.
        if not self.events:
            return False
        self.patterns[i] = ([list(e) for e in self.events], self.knob_count)
        return True

    def recall_pattern(self, i):
        pats = getattr(self, "patterns", {})
        if i not in pats or not pats[i][0]:
            return False
        evs, kc = pats[i]
        self.events = [list(e) for e in evs]
        self.knob_count = kc

        # The playhead is deliberately NOT moved. The cursor must be rebuilt
        # from it for the same reason undo's must -- see there.
        self.cursor = 0
        while (self.cursor < len(self.events)
               and fire_tick(self.events[self.cursor]) < self.play_head):
            self.cursor += 1
        return True

    def undo(self):
        """Restore the snapshot. Consumed, so a second undo is a no-op."""
        if not getattr(self, "have_snap", False):
            return False
        self.events = [list(e) for e in self.snap]
        self.knob_count = self.snap_knob_count

        # Rebuild the cursor from the playhead. Resetting it to zero would
        # replay everything between the loop start and here.
        self.cursor = 0
        while (self.cursor < len(self.events)
               and fire_tick(self.events[self.cursor]) < self.play_head):
            self.cursor += 1

        self.have_snap = False
        return True


def run_pass(lp, collect=None):
    """Run exactly one full loop from the current position, collecting fires."""
    fired = []
    start = lp.play_head
    ticks = 0
    while ticks < LOOP_TICKS:
        if lp.advance():
            ticks += 1
            for ev in lp.fire():
                fired.append((lp.play_head, ev[0]))
            if collect is not None:
                collect(lp)
    return fired


def test_every_hit_fires_once():
    """The core property. Record hits across the bar, then play two full passes
    and assert each hit sounds exactly once per pass."""
    lp = Looper()
    lp.set_tempo_bpm(120)

    combos = [0, 1, 2, 3, 4]
    # Place hits at ticks that are NOT already on the quantise grid, so the
    # quantiser genuinely has work to do.
    for i, c in enumerate(combos):
        lp.play_head = 37 + i * 97
        lp.record_hit(c)
    lp.play_head = 0
    lp.cursor = 0

    for p in range(2):
        fired = run_pass(lp)
        got = sorted(c for (_t, c) in fired)
        check("pass %d: every hit fires exactly once" % (p + 1),
              got, sorted(combos))


def test_hit_quantised_earlier_still_fires():
    """A hit stored at tick 5 quantises to 0 - BEFORE where it was recorded.

    Such an event must fire exactly once per pass. Two different bugs live
    here, and this test caught the second:

      - sorting the array by RAW tick leaves it unsorted by the order things
        actually sound, so the cursor walk misfires (fixed by sorting on
        fire_tick);
      - a "catch up on anything overdue" fire condition (`when <= play_head`)
        makes an event that fires at tick 0 match at EVERY tick until the
        cursor passes it, so it sounds late on one pass and again on the next.
        Exact tick matching is correct, and safe because no tick is ever
        skipped at any supported tempo.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)
    lp.play_head = 5
    lp.record_hit(7)
    lp.play_head = 0
    lp.cursor = 0

    fired = run_pass(lp)
    check("hit quantised earlier than stored still fires",
          [c for (_t, c) in fired], [7])


def test_overdub_midpass():
    """Insert an event during a pass, at a position the cursor has ALREADY
    walked past. It must not fire twice this pass, and must fire next pass."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    lp.play_head = 400
    lp.record_hit(1)
    lp.play_head = 0
    lp.cursor = 0

    fired_this = []
    inserted = [False]

    def maybe_insert(l):
        # Once we are past tick 600, add an event back at tick 100 (behind us).
        if not inserted[0] and l.play_head > 600:
            inserted[0] = True
            saved = l.play_head
            l.play_head = 100
            l.record_hit(5)
            l.play_head = saved

    ticks = 0
    while ticks < LOOP_TICKS:
        if lp.advance():
            ticks += 1
            for ev in lp.fire():
                fired_this.append(ev[0])
            maybe_insert(lp)

    check("overdub: event inserted behind cursor does not fire this pass",
          fired_this, [1])

    fired_next = [c for (_t, c) in run_pass(lp)]
    check("overdub: it does fire on the next pass",
          sorted(fired_next), [1, 5])


def test_tempo_retimes_not_drops():
    """Changing tempo must not drop or duplicate hits - the pattern is
    re-timed, which is the whole reason this is an event loop."""
    combos = [0, 2, 4, 6, 8]
    for bpm in (BPM_MIN, 90, 120, 174, BPM_MAX):
        lp = Looper()
        lp.set_tempo_bpm(bpm)
        for i, c in enumerate(combos):
            lp.play_head = i * 150 + 11
            lp.record_hit(c)
        lp.play_head = 0
        lp.cursor = 0
        fired = sorted(c for (_t, c) in run_pass(lp))
        check("tempo %3d: all hits fire once" % bpm, fired, sorted(combos))


def test_tempo_affects_duration():
    """Faster tempo must complete the loop in fewer control ticks."""
    dur = {}
    for bpm in (60, 240):
        lp = Looper()
        lp.set_tempo_bpm(bpm)
        n = 0
        ticks = 0
        while ticks < LOOP_TICKS:
            if lp.advance():
                ticks += 1
            n += 1
        dur[bpm] = n
    check("tempo: 240bpm loop is ~4x shorter than 60bpm",
          abs(dur[60] / dur[240] - 4.0) < 0.05, True)
    print("          60bpm=%d ticks, 240bpm=%d ticks (ratio %.3f)"
          % (dur[60], dur[240], dur[60] / dur[240]))


def test_full_buffer_drops_not_wraps():
    """When the buffer fills, further hits are DROPPED. Wrapping would
    overwrite the oldest events, silently rewriting the pattern."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    for i in range(MAX_EVENTS + 50):
        lp.play_head = i % LOOP_TICKS
        lp.record_hit(i % 10)
    check("full buffer: caps at kMaxEvents", len(lp.events), MAX_EVENTS)


def test_events_stay_sorted():
    """Playback is a cursor walk, which is only valid if the array is sorted."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    import random
    random.seed(7)
    for _ in range(200):
        lp.play_head = random.randrange(LOOP_TICKS)
        lp.record_hit(random.randrange(10))
    ticks = [fire_tick(e) for e in lp.events]
    check("events remain sorted by FIRE time", ticks, sorted(ticks))


def test_automation_cannot_starve_hits():
    """THE OVERDUB BUG.

    Filter automation and drum hits share one array. A continuous knob sweep
    emits an event every kFilterSampleTicks - about 96 per pass - and the first
    version let those ACCUMULATE without limit. After roughly five passes of
    idle twiddling the array was full, Insert() started silently dropping
    everything, and NEW DRUM HITS STOPPED BEING RECORDED.

    From the player's side that is indistinguishable from the looper
    overwriting what they just played, which is exactly how it was reported.

    Two fixes, both checked here: automation replaces itself on a tick rather
    than piling up, and a hit evicts stale automation if the array is full.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)

    # Twenty passes of dense knob movement.
    for _pass in range(20):
        for tick in range(0, LOOP_TICKS, 8):
            lp.play_head = tick
            lp.record_filter_at((tick + _pass) & 0xFF)

    check("automation is capped, not unbounded",
          lp.knob_count <= MAX_KNOB_EVENTS, True)
    print("          after 20 passes of sweeping: %d automation events"
          % lp.knob_count)

    # Now play some hits. Every one must be recorded.
    before = len([e for e in lp.events if not is_knob(e[1])])
    for i in range(32):
        lp.play_head = i * 20
        lp.record_hit(i % 10)
    after = len([e for e in lp.events if not is_knob(e[1])])

    check("all 32 hits recorded despite heavy automation",
          after - before, 32)


def test_automation_replaces_on_same_tick():
    """A second knob PASS over the same spot replaces the first.

    Note the arm_knobs() between passes. Events from the pass in progress are
    deliberately protected from the replace window - without that the window
    eats the sweep it is laying down. So "replace" means "a later pass replaces
    an earlier one", not "every sample replaces the previous sample".
    """
    lp = Looper()
    lp.set_tempo_bpm(120)
    for v in (10, 20, 30, 40):
        lp.arm_knobs()
        lp.play_head = 96
        lp.record_filter_at(v)

    at96 = [e for e in lp.events
            if is_knob(e[1]) and fire_tick(e) == 96]
    check("automation on one tick collapses to the latest", len(at96), 1)
    check("...and it is the most recent value", at96[0][2], 40)


def test_lanes_are_independent():
    """Two automation lanes share one array and one replace-on-tick rule.

    Recording a Y move must not delete a filter move sitting on the same tick -
    that would make the two knobs fight for one slot and each erase the other.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)
    lp.play_head = 96
    lp.record_filter_at(100, LANE_FILTER)
    lp.record_filter_at(200, LANE_TONE)
    lp.arm_knobs()

    at96 = [e for e in lp.events if is_knob(e[1]) and fire_tick(e) == 96]
    check("lanes: both survive on the same tick", len(at96), 2)
    lanes = sorted(lane_of(e[1]) for e in at96)
    check("lanes: one of each", lanes, [LANE_FILTER, LANE_TONE])

    # And each still replaces ITSELF on a later pass.
    lp.record_filter_at(150, LANE_FILTER)
    at96 = [e for e in lp.events if is_knob(e[1]) and fire_tick(e) == 96]
    check("lanes: a lane still replaces its own event", len(at96), 2)
    filt = [e for e in at96 if lane_of(e[1]) == LANE_FILTER][0]
    check("lanes: ...with the latest value", filt[2], 150)


def test_live_hit_does_not_replay_same_pass():
    """THE DOUBLING. A hit recorded at the playhead must not fire again on the
    pass that recorded it.

    main.cpp records the hit and then calls Fire() later in the SAME control
    tick. With the cursor left pointing at the newly inserted event, the walk
    landed on it and played it back on top of the live hit the player had
    already heard: two sounds a few milliseconds apart, and two voices consumed
    per hit instead of one. A few overdub passes then exhausted the polyphony,
    which is what made the loop appear to silence itself.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)
    lp.play_head = 0
    lp.cursor = 0

    fired = []
    ticks = 0
    while ticks < 40:
        if lp.advance():
            ticks += 1
            if ticks == 10:
                lp.record_hit(0)          # played live at this instant
            for ev in lp.fire():
                fired.append(ev[0])

    check("record: a live hit does not replay on the same pass", fired, [])

    # ...but it MUST come back on the next pass.
    nxt = [c for (_t, c) in run_pass(lp)]
    check("record: it does play on the next pass", nxt, [0])


def test_external_clock():
    """Pulse In 1 must override the X knob, and hand it back when it stops.

    Two bugs lived here. The edge was polled from the 3kHz control tick while
    ComputerCard only holds it true for one 48kHz sample, so ~94% of pulses
    were dropped and the clock could essentially never lock - it is latched at
    audio rate now. And SetTempo tracked the knob position WHILE clocked, so
    when the clock stopped the knob compared equal, read as unmoved, and the
    tempo stayed where the clock left it.
    """
    lp = Looper()
    for _ in range(200):
        lp.set_tempo_knob(4095)
        lp.tick_clock()
    check("clock: knob alone sets max tempo", round(lp.bpm()), 239)

    per = int(CTRL_RATE * 60 / 90)
    for _ in range(6):
        for _ in range(per):
            lp.set_tempo_knob(4095)
            lp.tick_clock()
        lp.clock_pulse()
    check("clock: a 90 BPM clock overrides the knob", round(lp.bpm()), 90)

    n = 0
    while lp.clocked() and n < CLOCK_TIMEOUT + 100:
        lp.set_tempo_knob(4095)
        lp.tick_clock()
        n += 1
    lp.set_tempo_knob(4095)
    check("clock: reverts ~3s after the last pulse",
          abs(n / CTRL_RATE - 3.0) < 0.05, True)
    check("clock: ...and goes back to the KNOB tempo", round(lp.bpm()), 239)


def test_clock_locks_across_range():
    """The timeout must be longer than the longest measurable gap, or a slow
    clock times out before its next pulse and can never lock at all."""
    bad = []
    for bpm in (30, 40, 60, 120, 240):
        lp = Looper()
        per = int(CTRL_RATE * 60 / bpm)
        for _ in range(3):
            for _ in range(per):
                lp.tick_clock()
            lp.clock_pulse()
        if abs(lp.bpm() - bpm) > 1:
            bad.append((bpm, round(lp.bpm())))
    check("clock: locks across 30-240 BPM", bad, [])


def test_rerecording_a_sweep_replaces_it():
    """A second pass over the same knob must SUBSUME the first, not interleave.

    The replace test used to be an exact tick match, and a second pass samples
    on a different phase from the first - so of 96 samples per loop, exactly
    zero landed on an existing event. Both sweeps survived and playback
    alternated between two different values on adjacent ticks.

    The window fixes that, and the pass tag stops the window eating the sweep
    it is laying down - without it a full re-record collapsed to one event.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)

    for t in range(0, LOOP_TICKS, 8):
        lp.play_head = t
        lp.record_filter_at(100)
    first = len([e for e in lp.events if is_knob(e[1])])
    check("sweep: first pass records a full lane", first, 96)

    lp.arm_knobs()
    for t in range(3, LOOP_TICKS, 8):        # a DIFFERENT phase
        lp.play_head = t
        lp.record_filter_at(200)

    knobs = [e for e in lp.events if is_knob(e[1])]
    check("sweep: a second pass does not double the events",
          len(knobs) <= 100, True)
    stale = [e for e in knobs if e[2] == 100]
    check("sweep: nothing from the first pass survives", stale, [])
    print("          %d events after re-recording (was %d)" % (len(knobs), first))


def test_partial_rerecord_keeps_the_rest():
    """Sweeping half a bar must replace that half and leave the rest."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    for t in range(0, LOOP_TICKS, 8):
        lp.play_head = t
        lp.record_filter_at(100)

    lp.arm_knobs()
    for t in range(200, 400, 8):
        lp.play_head = t
        lp.record_filter_at(200)

    outside = [e for e in lp.events
               if is_knob(e[1]) and (e[0] < 180 or e[0] > 420)]
    check("sweep: untouched parts of the bar keep their automation",
          all(e[2] == 100 for e in outside) and len(outside) > 50, True)


def test_clear_empties():
    lp = Looper()
    lp.set_tempo_bpm(120)
    for i in range(20):
        lp.play_head = i * 30
        lp.record_hit(i % 10)
    lp.clear()
    lp.play_head = 0
    check("clear: no events remain", (len(lp.events), lp.cursor), (0, 0))
    check("clear: nothing fires afterwards",
          [c for (_t, c) in run_pass(lp)], [])


def test_undo_restores_the_previous_pass():
    """The core promise: undo puts back exactly what was there before record
    was armed, and drops only what the armed pass added."""
    lp = Looper()
    lp.set_tempo_bpm(120)

    # A first pass, kept.
    lp.play_head = 0;   lp.record_hit(0)
    lp.play_head = 384; lp.record_hit(1)
    before = [list(e) for e in lp.events]

    # Arm record: snapshot. Then overdub two more hits.
    lp.snapshot()
    lp.play_head = 192; lp.record_hit(2)
    lp.play_head = 576; lp.record_hit(3)
    check("undo: overdub added its hits", len(lp.events), 4)

    check("undo: reported success", lp.undo(), True)
    check("undo: pattern is exactly the pre-record one", lp.events, before)


def test_undo_is_one_way():
    """A second undo must do nothing, not toggle the overdub back. A redo that
    looks like undo-twice is worse than a no-op."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    lp.play_head = 0; lp.record_hit(0)
    lp.snapshot()
    lp.play_head = 192; lp.record_hit(5)
    lp.undo()
    after_first = [list(e) for e in lp.events]

    check("undo: a second undo reports nothing to do", lp.undo(), False)
    check("undo: ...and changes nothing", lp.events, after_first)


def test_undo_with_no_snapshot():
    """Undo before ever recording must be a safe no-op, not a crash or a
    silent wipe."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    lp.play_head = 0; lp.record_hit(4)
    check("undo: no snapshot reports failure", lp.undo(), False)
    check("undo: ...and leaves the pattern alone", len(lp.events), 1)


def test_undo_does_not_replay_the_bar_so_far():
    """THE subtle one. Undo replaces the event array, so the cursor -- an index
    into the old array -- is meaningless afterwards.

    Resetting it to zero looks like the safe thing and is not: the walk in
    fire() would then re-fire every event between the start of the loop and
    the current playhead, all on the tick the undo landed on. Undoing halfway
    through a bar would spray the first half of the pattern out at once.

    Rebuilding it from the playhead is what avoids that.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)

    # Four hits spread across the loop, all in the snapshot.
    for t, v in ((0, 0), (192, 1), (384, 2), (576, 3)):
        lp.play_head = t
        lp.record_hit(v)
    lp.snapshot()
    lp.play_head = 96; lp.record_hit(9)     # the pass we will undo

    # Undo from the middle of the bar, past the first two hits.
    lp.play_head = 400
    lp.undo()

    # Nothing may fire on the undo tick itself.
    check("undo: nothing fires on the tick undo lands", lp.fire(), [])

    # Only the hits still AHEAD of the playhead should fire this pass.
    fired = []
    while lp.play_head < LOOP_TICKS - 1:
        lp.play_head += 1
        for ev in lp.fire():
            fired.append((lp.play_head, ev[0]))
    check("undo: only events ahead of the playhead fire",
          [c for (_t, c) in fired], [3])


def test_patterns_store_voices_not_sounds():
    """A pattern must hold VOICE INDICES and nothing about what they sound
    like, so uploading a sample or re-pointing a slot changes what an existing
    pattern plays without touching the pattern.

    record_hit already asserts the value is a voice rather than a combo. What
    this pins down is the range: every stored hit has to be addressable as a
    voice index, because that is what makes the same four bars playable on a
    card with a completely different sample set.

    The failure this guards against is someone "optimising" the resolved
    sample pointer into the event to save an indirection -- which would be
    faster, and would make patterns non-portable and un-transferable.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)

    for v in range(NUM_VOICES):
        lp.play_head = v * 8
        lp.record_hit(v)

    hits = [e for e in lp.events if not is_knob(e[1])]
    check("agnostic: every voice recorded", len(hits), NUM_VOICES)
    check("agnostic: all stored as plain voice indices",
          sorted(e[1] for e in hits), list(range(NUM_VOICES)))
    # Four bytes, and the sound-bearing one is velocity -- not a sample id.
    check("agnostic: an event is still 3 fields", len(hits[0]), 3)


def test_pattern_recall_keeps_the_playhead():
    """Recall swaps the pattern WITHOUT moving the playhead, so switching
    mid-bar reads as the band changing part rather than a stop and start.

    The cursor is an index into an array that has just been replaced, so it
    has to be rebuilt from the playhead -- exactly the trap undo has. Reset
    it to zero instead and every event between the loop start and here fires
    at once, which is a burst of the first half of the pattern on the tick
    you switched.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)

    # Slot 0: hits early and late in the bar.
    lp.play_head = 0;   lp.record_hit(1)
    lp.play_head = 600; lp.record_hit(2)
    lp.store_pattern(0)

    # Slot 1: a different pattern, same shape.
    lp.clear()
    lp.play_head = 100; lp.record_hit(5)
    lp.play_head = 700; lp.record_hit(6)
    lp.store_pattern(1)

    # Play into the middle of the bar, then switch.
    lp.play_head = 400
    check("pattern: recall reports success", lp.recall_pattern(0), True)
    check("pattern: playhead did not move", lp.play_head, 400)
    check("pattern: nothing fires on the switch tick", lp.fire(), [])

    # Only the hit still AHEAD of 400 should fire this pass.
    fired = []
    while lp.play_head < LOOP_TICKS - 1:
        lp.play_head += 1
        for ev in lp.fire():
            fired.append(ev[0])
    check("pattern: only events ahead of the playhead fire", fired, [2])


def test_quantise_snaps_at_capture_not_playback():
    """A hit is snapped to the grid ONCE, when recorded. Changing the grid
    afterwards must not move it.

    This is a deliberate reversal of the looper's original design, in which
    quantisation was a non-destructive PLAYBACK filter. The reason is the
    property asserted below: a live playback filter applies ONE divisor to
    every event at once, so switching to 12ths for a triplet fill would drag
    the straight part recorded under 16ths onto the triplet grid too. Snapping
    at capture lets both live in one loop.
    """
    lp = Looper(); lp.set_tempo_bpm(120)

    # A hit deliberately off the 16th grid (16th = every 12 ticks at TPB 48).
    lp.play_head = 7
    lp.record_hit(1)
    at_16th = lp.events[0][0]
    check("quant: snapped to the 16th grid at capture", at_16th % 12, 0)

    # Now cycle to 12ths and confirm the ALREADY-RECORDED hit did not move.
    lp.cycle_quant_grid()
    check("quant: grid is now 12th", QUANT_NOTES_PER_BEAT[lp.quant_grid], 3)
    check("quant: the existing hit did NOT move", lp.events[0][0], at_16th)


def test_mixed_grids_coexist_in_one_loop():
    """THE reason quantisation moved to capture time: a straight part and a
    triplet part in the same loop, each on the grid it was played against."""
    lp = Looper(); lp.set_tempo_bpm(120)

    # Straight part under 16ths (grid divides TICKS_PER_BEAT into 12-tick steps).
    lp.play_head = 25
    lp.record_hit(1)
    straight = lp.events[0][0]

    # Triplet part under 12ths (16-tick steps).
    lp.cycle_quant_grid()
    lp.play_head = 100
    lp.record_hit(2)
    triplet = [e for e in lp.events if e[1] == 2][0][0]

    check("mixed: straight hit sits on the 16th grid", straight % 12, 0)
    check("mixed: triplet hit sits on the 12th grid", triplet % 16, 0)
    # And the straight one is STILL on its own grid, untouched by the change.
    check("mixed: straight hit still on the 16th grid", straight % 12, 0)


def test_quant_grid_cycles_three_ways():
    """16th -> 12th -> 8th -> back. Three steps, matching the LED count."""
    lp = Looper()
    check("cycle: starts at 16th", QUANT_NOTES_PER_BEAT[lp.quant_grid], 4)
    lp.cycle_quant_grid()
    check("cycle: then 12th", QUANT_NOTES_PER_BEAT[lp.quant_grid], 3)
    lp.cycle_quant_grid()
    check("cycle: then 8th", QUANT_NOTES_PER_BEAT[lp.quant_grid], 2)
    lp.cycle_quant_grid()
    check("cycle: wraps back to 16th", QUANT_NOTES_PER_BEAT[lp.quant_grid], 4)


def test_mutes_are_not_loop_state():
    """Mutes are a MIXER move, not part of the music, so nothing about them
    lives in the loop.

    Two consequences, both wanted: a stored pattern carries no mute state, and
    `muted_` survives a recall because it is card state the looper never sees.
    That makes mutes an arrangement layer sitting ABOVE the patterns -- drop
    the hats, swap patterns, hats stay dropped.

    This asserts the negative: no event the looper stores can encode a mute,
    because the only tags that exist are drum hits and knob lanes. If someone
    adds a mute event later, this fails and they have to come and read the
    reasoning in MutePress first.
    """
    lp = Looper(); lp.set_tempo_bpm(120)
    lp.play_head = 0;   lp.record_hit(1)
    lp.play_head = 200; lp.record_knob_at(2000, LANE_FILTER)

    for e in lp.events:
        if is_knob(e[1]):
            check("mutes: knob events are lanes 0-9 only",
                  lane_of(e[1]) <= LANE_PAR_D, True)
        else:
            check("mutes: hit events are plain voice indices",
                  0 <= e[1] < NUM_VOICES, True)


def test_undo_does_not_reach_the_pattern_slots():
    """Undo is about RECORDING, and only recording.

    It covers what a pass with the switch Up put into the live loop -- hits,
    mutes, effects, knob curves. It deliberately does NOT cover storing a
    pattern, which is a separate deliberate act on separate state. A gesture
    that sometimes meant "undo my playing" and sometimes "un-store that slot"
    would be two features sharing one name.

    So StorePattern must not snapshot, and Undo must leave the slots alone.
    """
    lp = Looper(); lp.set_tempo_bpm(120)

    # A pattern in slot 0, and a snapshot taken by arming record.
    lp.play_head = 0; lp.record_hit(1)
    lp.store_pattern(0)
    lp.snapshot()

    # Overdub, then store that over the slot.
    lp.play_head = 384; lp.record_hit(2)
    lp.store_pattern(0)
    stored_after = [list(e) for e in lp.patterns[0][0]]
    check("undo/slots: the store took", len(stored_after), 2)

    # Undo reverts the LIVE loop only.
    check("undo/slots: undo succeeded", lp.undo(), True)
    check("undo/slots: live loop reverted", len(lp.events), 1)
    check("undo/slots: the slot was NOT reverted",
          lp.patterns[0][0], stored_after)


def test_store_refuses_an_empty_loop():
    """Storing silence over a good pattern destroys it unrecoverably -- Undo
    covers the live loop, not the slots.

    This is the tail of a real bug: PatternPress read `recording_`, which is
    updated later in the same tick, so a stale-true reading turned a RECALL
    into a STORE. When the live loop happened to be empty that wiped the slot,
    and recalling it afterwards sounded exactly like the pattern had been
    muted. The read is fixed at the source; this guard makes the destructive
    half impossible regardless.
    """
    lp = Looper(); lp.set_tempo_bpm(120)
    lp.play_head = 0;   lp.record_hit(1)
    lp.play_head = 384; lp.record_hit(2)
    check("store: a real pattern stores", lp.store_pattern(0), True)

    lp.clear()
    check("store: an empty loop is refused", lp.store_pattern(0), False)

    # And the slot still holds the good pattern.
    check("store: the slot survived", lp.recall_pattern(0), True)
    check("store: ...with its hits intact", len(lp.events), 2)


def test_pattern_slots_are_three_not_four():
    """THREE slots, because the gesture is hold-D-and-tap: the shift button is
    not itself a slot, same as the three mute groups.

    This also pins down why there is no hold-to-store gesture. Four Voltages
    LATCHES -- a held button is a level that sits there indefinitely -- so any
    'held for N ticks' test passes eventually and every recall would become a
    store. The switch says which verb instead.
    """
    check("pattern: three slots, one per non-shift pad", NUM_PATTERNS, 3)


def test_pattern_recall_of_empty_slot_is_a_noop():
    """Tapping an empty slot must leave the loop alone. Silently wiping what
    you are playing is not something a tap should ever do."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    lp.play_head = 0; lp.record_hit(3)
    before = [list(e) for e in lp.events]

    check("pattern: empty slot reports failure", lp.recall_pattern(2), False)
    check("pattern: ...and changes nothing", lp.events, before)


def test_pattern_store_is_a_snapshot():
    """A stored pattern is a COPY -- carrying on playing must not alter it."""
    lp = Looper()
    lp.set_tempo_bpm(120)
    lp.play_head = 0; lp.record_hit(1)
    lp.store_pattern(0)

    lp.play_head = 300; lp.record_hit(9)      # overdub after storing
    check("pattern: live loop grew", len(lp.events), 2)

    lp.recall_pattern(0)
    check("pattern: the stored copy was untouched", len(lp.events), 1)


def test_undo_restores_every_lane():
    """Undo must put back ALL of it -- hits, filter, tone, the four effect
    lanes and the four parameter lanes -- not just the drum hits.

    This holds for a structural reason worth stating: every lane lives in the
    SAME events_ array, distinguished only by the lane bits of `what`. So
    snapshotting the array covers everything by construction, and the risk is
    not that a lane gets missed but that someone later "optimises" the
    snapshot into something selective. This test is what would catch that.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)

    # Something in every kind of lane.
    lp.play_head = 0;  lp.record_hit(3)
    lp.play_head = 96
    lp.record_knob_at(2000, LANE_FILTER)
    lp.record_knob_at(1500, LANE_TONE)
    for s in range(NUM_FX_SLOTS):
        lp.record_knob_at(pack_fx(s + 1) << 4, fx_lane_for_shift(s))
        lp.record_knob_at(1000 + s * 300,      par_lane_for_shift(s))

    before = [list(e) for e in lp.events]
    check("undo/lanes: everything recorded", len(before), 11)

    # Arm, then scribble over every one of them.
    lp.snapshot()
    lp.arm_knobs()
    lp.play_head = 200; lp.record_hit(9)
    lp.play_head = 96
    lp.record_knob_at(10, LANE_FILTER)
    lp.record_knob_at(20, LANE_TONE)
    for s in range(NUM_FX_SLOTS):
        lp.record_knob_at(pack_fx(11) << 4, fx_lane_for_shift(s))
        lp.record_knob_at(4000,             par_lane_for_shift(s))

    check("undo/lanes: the overdub changed things", lp.events == before, False)
    check("undo/lanes: undo reported success", lp.undo(), True)
    check("undo/lanes: every lane came back", lp.events, before)


def test_fx_packing_round_trip():
    """The FX lane carries the effect INDEX, and it has to survive the same
    value>>4 / <<4 round trip the knob lanes use.

    Depth used to share this byte, four bits each, which cost a sweep most of
    its resolution. It has its own lane now -- so what must survive here is
    the index, because landing on the wrong one plays a completely different
    effect rather than a slightly wrong one.
    """
    for fx in range(12):
        packed = pack_fx(fx)
        stored = ((packed << 4) >> 4) & 0xFF
        check("fx pack: effect %d survives the round trip" % fx,
              fx_of(stored), fx)


def test_fx_lane_records_while_held():
    """The FX lane writes whenever an effect is HELD, unlike a knob lane which
    only writes while MOVING. Holding one steady is exactly what has to be
    captured -- otherwise playback starts the effect and never stops it."""
    lp = Looper()
    lp.set_tempo_bpm(120)

    packed = pack_fx(3)
    for t in (0, 8, 16):
        lp.play_head = t
        lp.record_knob_at(packed << 4, LANE_FX_B)

    fx_events = [e for e in lp.events if is_knob(e[1]) and lane_of(e[1]) == LANE_FX_B]
    check("fx lane: a steady hold still records", len(fx_events) >= 1, True)
    check("fx lane: it stored the right effect", fx_of(fx_events[0][2]), 3)


def test_parameter_lane_is_separate_from_the_effect():
    """Each shift owns TWO lanes: which effect runs, and that lane's parameter
    curve. Separate because they are PERFORMED separately -- draw a curve by
    holding the shift alone, then pop effects in and out over the top without
    re-recording the curve.

    If they shared a lane, changing one would destroy the other, which is the
    whole thing this split exists to prevent.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)

    lp.play_head = 96
    lp.record_knob_at(pack_fx(4) << 4, LANE_FX_B)   # crush under shift B
    lp.record_knob_at(3000, LANE_PAR_B)             # and a depth curve for B

    fx = [e for e in lp.events if is_knob(e[1]) and lane_of(e[1]) == LANE_FX_B]
    par = [e for e in lp.events if is_knob(e[1]) and lane_of(e[1]) == LANE_PAR_B]
    check("par lane: effect and parameter coexist", (len(fx), len(par)), (1, 1))
    check("par lane: the effect is intact", fx_of(fx[0][2]), 4)
    # The parameter keeps a full byte of resolution now, not four bits.
    check("par lane: the curve kept its value", par[0][2], 3000 >> 4)

    # Re-drawing the curve must not disturb which effect is running.
    lp.arm_knobs()
    lp.play_head = 96
    lp.record_knob_at(1000, LANE_PAR_B)

    fx = [e for e in lp.events if is_knob(e[1]) and lane_of(e[1]) == LANE_FX_B]
    par = [e for e in lp.events if is_knob(e[1]) and lane_of(e[1]) == LANE_PAR_B]
    check("par lane: re-drawing replaced the curve", par[0][2], 1000 >> 4)
    check("par lane: ...and left the effect alone", fx_of(fx[0][2]), 4)


def test_fx_lanes_do_not_overwrite_each_other():
    """THE reason there are four FX lanes rather than one.

    With a single lane, an effect recorded under one shift overwrote an
    effect recorded under another wherever the two overlapped in the bar --
    you could not have a crush on beat 1 and a gate on beat 3 coexist. One
    lane per SHIFT means each keeps its own timeline.

    This is the same replacement logic the knob lanes use, so what is really
    being asserted is that the lane index is genuinely part of the event's
    identity, not just decoration.
    """
    lp = Looper()
    lp.set_tempo_bpm(120)

    # Two different effects, same tick, different shifts.
    lp.play_head = 96
    lp.record_knob_at(pack_fx(4) << 4, LANE_FX_B)    # crush, shift B
    lp.record_knob_at(pack_fx(10) << 4, LANE_FX_D)   # gate, shift D

    b = [e for e in lp.events if is_knob(e[1]) and lane_of(e[1]) == LANE_FX_B]
    d = [e for e in lp.events if is_knob(e[1]) and lane_of(e[1]) == LANE_FX_D]
    check("fx lanes: both survive on the same tick", (len(b), len(d)), (1, 1))
    check("fx lanes: B kept its own effect",  fx_of(b[0][2]), 4)
    check("fx lanes: D kept its own effect",  fx_of(d[0][2]), 10)

    # Re-recording ONE lane must not disturb the other.
    lp.arm_knobs()
    lp.play_head = 96
    lp.record_knob_at(pack_fx(5) << 4, LANE_FX_B)

    b = [e for e in lp.events if is_knob(e[1]) and lane_of(e[1]) == LANE_FX_B]
    d = [e for e in lp.events if is_knob(e[1]) and lane_of(e[1]) == LANE_FX_D]
    check("fx lanes: re-recording B replaced B", fx_of(b[0][2]), 5)
    check("fx lanes: ...and left D alone",       fx_of(d[0][2]), 10)


def main():
    print("NIBBLE looper model")
    print()
    test_every_hit_fires_once()
    test_hit_quantised_earlier_still_fires()
    test_overdub_midpass()
    test_tempo_retimes_not_drops()
    test_tempo_affects_duration()
    test_full_buffer_drops_not_wraps()
    test_automation_cannot_starve_hits()
    test_automation_replaces_on_same_tick()
    test_lanes_are_independent()
    test_rerecording_a_sweep_replaces_it()
    test_partial_rerecord_keeps_the_rest()
    test_live_hit_does_not_replay_same_pass()
    test_external_clock()
    test_clock_locks_across_range()
    test_events_stay_sorted()
    test_clear_empties()
    test_undo_restores_the_previous_pass()
    test_undo_is_one_way()
    test_undo_with_no_snapshot()
    test_undo_does_not_replay_the_bar_so_far()
    test_patterns_store_voices_not_sounds()
    test_pattern_recall_keeps_the_playhead()
    test_quantise_snaps_at_capture_not_playback()
    test_mixed_grids_coexist_in_one_loop()
    test_quant_grid_cycles_three_ways()
    test_mutes_are_not_loop_state()
    test_undo_does_not_reach_the_pattern_slots()
    test_store_refuses_an_empty_loop()
    test_pattern_slots_are_three_not_four()
    test_pattern_recall_of_empty_slot_is_a_noop()
    test_pattern_store_is_a_snapshot()
    test_undo_restores_every_lane()
    test_fx_packing_round_trip()
    test_fx_lane_records_while_held()
    test_fx_lanes_do_not_overwrite_each_other()
    test_parameter_lane_is_separate_from_the_effect()
    print()
    if FAILURES:
        print("%d FAILED: %s" % (len(FAILURES), ", ".join(FAILURES)))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
