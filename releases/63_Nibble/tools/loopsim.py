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
QUANT_TICKS = TICKS_PER_BEAT // 4                 # 12
NUM_VOICES = 12          # the looper stores VOICES, not key combos
MAX_EVENTS = 512
MAX_KNOB_EVENTS = 192
KNOB_EVENT = 0x80
THIS_PASS = 0x40
KNOB_REPLACE_WINDOW = 12
NUM_LANES = 2
LANE_FILTER, LANE_TONE = 0, 1


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


def fire_tick(ev):
    """When an event actually sounds. Mirrors Looper::FireTick in looper.cpp.

    The event array is sorted by THIS, not by the raw stored tick: quantisation
    can move an event across the loop boundary, and an array sorted by raw tick
    is then not sorted by the order things sound - which double-fires events.
    """
    if is_knob(ev[1]):
        return ev[0]
    q = ((ev[0] + QUANT_TICKS // 2) // QUANT_TICKS) * QUANT_TICKS
    return q % LOOP_TICKS


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
        self.insert([self.play_head, what | THIS_PASS, value])

    def record_hit(self, voice, vel=100):
        # A hit the player just performed outranks stale automation.
        if len(self.events) >= MAX_EVENTS and self.knob_count > 0:
            for i, e in enumerate(self.events):
                if is_knob(e[1]):
                    self.remove(i)
                    break
        assert 0 <= voice < NUM_VOICES, "loop stores voices, not combos"
        self.insert([self.play_head, voice, vel])

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
    print()
    if FAILURES:
        print("%d FAILED: %s" % (len(FAILURES), ", ".join(FAILURES)))
        return 1
    print("all passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
