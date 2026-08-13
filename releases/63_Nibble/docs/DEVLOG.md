# NIBBLE devlog

## v1.11.4 — the collision warning, quieter still

Still fussy on the bench at 40, so down to **32** (~95 mV).

Worth recording the shape of this, because it took three passes: 64 was a guess
made before hardware existed, 40 was reasoned from `kSettleTol`, and 32 comes
from actually playing it. Theory said 48 was the limit; the bench says
combinations far tighter than that stay perfectly distinct.

Checked for the failure mode that matters — a gap that is broken *and*
unflagged. There isn't one. Real misclassification starts at 16 and under, both
of which are still caught, so 32 leaves a factor of two rather than cutting it
fine. "Nothing patched in" continues to fail on the span check regardless.

`tools/ghostsim.py` mirrors the constant and moved with it.

---

## v1.11.3 — demo videos, and panel labels

Two-part demo and walkthrough added. The registry's `demo-link` takes a single
URL rather than a list, so Part 1 gets the playable thumbnail on the card's page
and both parts are linked in the README, where matching YouTube URLs are
embedded inline anyway.

Panel labels tightened throughout — asterisks dropped, sockets named for what
they do rather than which mode they belong to (`Retrigger / ClkIn`,
`Envelope1 / Drums`, `Envelopes / DJFilter`).

One label was inconsistent with the rest: CV Out 1 had become just "Pitch" while
still carrying the PERCUSSION bassline its own description documents. Now
`Pitch / Bass`, matching the dual-name pattern every other shared socket uses.

---

## v1.11.2 — the collision warning was crying wolf

Reported that combinations flagged with the LED 4+5 collision warning were then
reading fine and staying distinct in play. They were.

`kCollisionMin` was 64 — a number picked before any hardware existed and never
revisited. The detector's actual limit is set by `kSettleTol`: a reading counts
as settled within ±24, so two levels only become genuinely ambiguous once their
gap falls below about 48, where a reading could plausibly land on the wrong side
of the midpoint. 64 was 1.3× stricter than even that.

Now 40, deliberately *below* the theoretical limit rather than above it. The
trade is accepted knowingly: a pair between 40 and 48 apart might occasionally
flicker under drift without having been flagged. That is worth it, because a
warning which fires on combinations you can play reliably teaches you to ignore
the warning — which is strictly worse than not warning at all.

Checked on the model across gaps: silent from 40 up, warns below, and genuinely
misclassifies only at 8–16. "Nothing patched in" still fails on the span check
regardless, since all ten captures land on the same value and span zero.

`tools/ghostsim.py` mirrors this constant and was updated with it — the two
drifting apart is exactly the failure that file exists to prevent.

---

## v1.11.1 — the calibration alerts were blinking at 47Hz

Reported that the calibration warnings did not seem to fire — including with
nothing patched in, which should collide on every step after the first.

They were firing. They were invisible.

Every animated LED in learn mode is `(timer >> shift) & 1`, and the timer ticks
at the 3kHz control rate. A shift of 5 is therefore **47Hz**, far above flicker
fusion — it reads as a dim steady glow, not a blink. The patterns were:

| Alert | Shift | Rate | |
|---|---|---|---|
| Failure (columns alternating) | `>>4` | 94Hz | invisible |
| Collision (markers) | `>>5` | 47Hz | invisible |
| Abort (all six) | `>>6` | 23Hz | barely a shimmer |
| Which button to press | `>>7` | 12Hz | **visible** |

That last row is the tell: the one animation anyone had ever seen working was
also the only one slow enough to see. The rates are named constants now
(`kBlinkFast` 12Hz, `kBlinkSlow` 6Hz) with the arithmetic written down, because
"pick a shift" is exactly the kind of decision that looks arbitrary and is not.

### The two warnings looked identical anyway

"Voltage still moving" reused `LearnPhase::Collision` — the same LEDs, differing
only in duration, which is no difference at all to look at. They call for
completely different reactions: one is *hold your hand steadier*, the other is
*these two combinations cannot be told apart, move the Four Voltages knob*.

`NotSettled` is its own phase now and flutters the four BUTTON LEDs, while a
collision blinks the two MARKER LEDs. Your hand versus the card.

### Checked while in there

The collision count does include the final step — `collisionsThisLearn_++` sits
above the branch. The tenth capture gets no per-step flash (it goes straight to
the Done or Failed announcement) but it is still counted, so the end-of-learn
warning does not under-report. Comment added to say so, since the placement
looks accidental and is not.

---

## v1.11.0 — documentation, after calling it working

No code. The card is played and behaving; this is the pass that makes the
documentation match it.

**A one-liner, in the three places that must agree.** `short-description`, the
README title block, and the GitHub repo description. "Four buttons. Fifteen
voltages. Ten notes." stays underneath as the mechanic — it earns its place once
the purpose has been stated first.

**Boot LEDs got their own section.** They were one sentence under a table, and
that sentence omitted the state that matters when something is wrong: the splash
BLINKS rather than holding if the Computer's own CV outputs have no factory
calibration, which means 1V/oct will not track. That is not a NIBBLE fault, and
an hour spent blaming the learn pass for it would be an hour wasted.

**Calibration failure was restructured to lead with the fix.** The knob and
output advice — output 1, knob at twelve o'clock — did not appear anywhere at
all, despite being the single most useful sentence for a new owner. It is now
the first thing in the section, ahead of the explanation, because someone
holding a card with alternating LED columns wants the remedy rather than the
theory. The four end-of-calibration LED patterns are tabulated with what to do
about each.

**The two audio outputs.** Asked directly whether they are the same. In MELODY
they are not — filter and loudness envelopes, and that asymmetry is what Knob X
balances. In PERCUSSION they are identical by design. Both tables already listed
this correctly; what was missing was the plain statement that the PERCUSSION
pair are *intentionally* the same, so nobody hunts for the setting that splits
them. Considered making Audio 2 a dry, unfiltered bus and decided against it:
"patch either socket, get the whole kit" is worth more than the extra routing.

**Melody / Percussion** in prose, `KEYS` / `DRUMS` in the source. The mode names
are noted once in each place so the two stay matchable. The rename stops at the
documentation deliberately — churning working source for a naming preference is
how working things stop working.

The Status section had been claiming the card was untested on hardware since
v1.0.0, ten versions ago.

---

## v1.10.1 — re-recording a sweep never actually replaced it

Asked for a wider window on overwriting knob automation, on the reasoning that
two passes will never land on exactly the same instant. That was right, and the
arithmetic is worse than "rarely": automation samples every 8 ticks, a second
pass starts on a different phase, and of **96 samples per lane per loop, exactly
zero** coincide with an existing event.

So the replace test — an exact tick match — replaced nothing, ever. Both sweeps
survived, interleaved, and playback alternated between two different values on
adjacent ticks. That is what a re-recorded sweep sounded like.

`kKnobReplaceWindow` is 12 ticks, slightly wider than the 8-tick sample
interval so a pass always subsumes the one beneath it however the two line up.
About 125ms at 120bpm, which is also roughly the resolution a hand can place a
move at. It wraps the loop boundary properly, since the seam between passes is
exactly where two sweeps would otherwise both survive.

### The window ate its own tail

First attempt left **one** event after a full re-record instead of 96: each new
event fell inside the next one's window and was deleted by it.

Events written during the pass in progress are now tagged (`kThisPass`, a spare
bit in `what` — checked against the voice indices and the lane bit for
collisions). The window clears earlier passes only. `ArmKnobs()` strips the tag
from everything, so "this pass" always means the one being recorded now.

Four existing tests failed on this and all four were stale assumptions rather
than breakage: they recorded repeatedly at one tick *within a single pass* and
expected each write to replace the last. Protecting the current pass is exactly
what stops the window eating its own sweep, so the tests were rewritten to arm
between passes.

---

## v1.10.0 — the knob is an override, not a negotiation

### Why handing control back was glitchy

The pickup design released the knob to the player on a move, and then the very
next recorded event handed it straight back to playback. With both active,
control alternated between hand and recording every few ticks — measured at **65
transitions** during a single grab-and-release. That is the glitching.

The replacement is a state, not a handshake: **moving the knob mutes that lane's
playback**, for as long as you move and for a 250ms hold after. Only one of them
is ever driving. The same grab-and-release now measures **5 transitions**.

The hold matters. Without it a slow, deliberate sweep would flicker every time
the knob paused between samples; 250ms bridges that and still feels immediate
when you let go.

### Recording overwrites only where you move

A lane now writes only while the player has hold of that knob. Sweep across half
a bar and that half is replaced; the rest keeps what it had.

The bug this closes is subtle: recording the knob's position every tick meant a
knob *sitting still* wrote a flat line over an earlier sweep, simply because
record was armed. Only moving knobs write anything now.

Note the value recorded is the raw knob, not the lane's output — recording the
output would re-record playback on top of itself on every pass.

### The DRUMS tap re-strikes the bass

It re-fired the last drum, which duplicated something the buttons already do
better: tapping a pair again is faster and more accurate than reaching for the
switch.

The bassline had no way to be struck twice at all. A note only fires when the
combination CHANGES, so holding one button gives a single note and nothing more.
The tap is now a repeat key for the bass, which is what makes it playable over a
running pattern.

`lastVoice_` went with it — it had become state that was written and never read.

---

## v1.9.1 — the external clock never actually worked

Reported that Pulse In 1 did not override the X knob. It did not, and three
separate faults had to line up for it to fail as completely as it did.

**1. Most clock pulses were never seen.** `PulseIn1RisingEdge()` is true for
exactly ONE 48 kHz sample, and it was being polled from the 3 kHz control tick —
so an edge only registered when it happened to land on the 1-in-16 sample the
tick ran on. About 6% of pulses caught. `ClockPulse()` needs two consecutive
edges to measure an interval, so at 120 bpm it was seeing roughly one edge every
eight seconds and could essentially never lock.

The edge is latched at audio rate now and consumed by the control tick. **This
also silently broke the KEYS retrigger**, which was dropping the same 94% of its
triggers — nobody had reported it, presumably because a missed retrigger is much
less obvious than a clock that does nothing.

**2. The two clock timers were ordered wrongly.** The interval sanity limit was
4 s and the timeout 3 s, so any clock slower than 20 bpm timed out *before* its
next pulse arrived and could never establish an interval. They are named
constants now with a `static_assert` that the timeout outlasts the longest
measurable gap.

**3. It never handed the tempo back properly.** `SetTempo()` tracked the knob
position while clocked, meaning to be helpful. The effect was the opposite: when
the clock stopped, the knob compared equal to its remembered value, read as "not
moved", and the tempo stayed wherever the clock had left it until the knob was
physically wiggled. It now forgets the position while clocked, so the first call
after release always recomputes.

Modelled end to end in `tools/loopsim.py`: knob at 239 bpm, a 90 bpm clock takes
over, the clock stops, and 3.0 s later it is back at 239. Locks cleanly across
30–240 bpm.

---

## v1.9.0 — recording played every hit twice

### THE bug behind "recording silences the loop"

Three versions of this have now been chased, and this was the real one. The
clue was the right one: *"is it playing sounds twice when I'm recording?"*

It was. `FireCombo()` records a hit at the current playhead, and `DrumsControl()`
calls `Fire()` **later in the same control tick**. The cursor was left pointing
*at* the event just inserted, so the playback walk landed on it immediately and
played it again — on top of the live hit already heard.

Two consequences, and the second is the one that was being reported:

- a flam, two hits a few milliseconds apart;
- **two voices consumed per hit instead of one**, so the polyphony ran out in
  half the passes and the loop appeared to silence itself.

The fix is one character: `if (i < cursor_)` became `if (i <= cursor_)`.
Inserting *at* the cursor must step it past, not just inserting before it.
`tools/loopsim.py` now asserts that a hit recorded on a pass does not sound
again until the next one.

Worth recording why this took three attempts: the two earlier fixes (the filter
gate in v1.3, the arm-time write in v1.6) were both real bugs with the same
symptom, so each one seemed to explain it. The tell that something remained was
"even when I haven't touched the filter".

### Levels are set by perceived loudness now

The cowbell was reported as very loud while measuring the *same energy as the
kick*, which is what finally showed the metric was wrong. The ear is about 24 dB
more sensitive at 800 Hz than at 55 Hz, so equal energy is nowhere near equal
loudness once pitches differ — and the cowbell was a pure tone sitting in the
most sensitive band.

That also explains why the crash kept coming back too loud across three rounds
of "reduce it": each reduction was measured against an energy target that was
itself far too generous for a bright, long voice.

Levels are now set from an A-weighted model with a duration term. The kit moves
a long way: cowbell 90% → 16% (and up to 1500 Hz, a real cowbell's register, as
asked), crash 20% → 9% with a shorter decay, hats to 18–31%, toms to ~45%. The
table in `drums.cpp` carries the rule of thumb — an octave higher wants about
half the level, twice as long about two thirds.

---

## v1.8.1 — the LED flash lit the wrong pad

Reported from KEYS: a single press flashes the right LED, but B+C flashes
**LED 3 (D)** — a button not even in the combo.

The cause was `ledFlash_[combo & 3]`, masking a COMBO index (0..9) down to an
LED index (0..3). Singles were right by coincidence, because 0..3 mask to
themselves. Every pair was wrong, and two of them (BD, CD) landed on a pad
belonging to neither button pressed.

`ComboLedMask()` already existed and does this properly — it was written for the
held-combo display and simply never used by the flash path. The flash now goes
through it, so a pair lights both of its buttons.

One consequence worth noting: a hit replayed from the LOOP knows its *voice*,
not the gesture that made it, and the same voice is reachable from more than one
gesture. `FlashVoice()` looks up a gesture that plays it, so playback lights the
same pads the performance did rather than nothing at all.

---

## v1.8.0 — the loop was eating itself, and Y automation

### Voice stealing, which is what "recording silences previous stuff" was

Reported as the loop silencing earlier hits, intermittently, and getting worse
with more passes. Neither of the two filter bugs already fixed explains
"intermittently" — that pattern points at resource contention, and it was.

Voice allocation was a blind round robin: `next_ = (next_ + 1) % 6`, taking the
next slot whether or not it was still sounding. Six slots was reasoned from "you
only have four fingers", which is the wrong question once a LOOP is playing — a
four-bar pattern with several overdubbed passes fires far more overlapping hits
than a pair of hands ever could.

The arithmetic: at 120bpm a sixteenth is 125ms and the crash rings for nearly a
second. That is seven sixteenths, so one crash needs seven slots to itself while
a hat pattern runs underneath. With six slots, any six subsequent hits took it.

Modelled across overdub passes:

| passes | old (6, round robin) | new (16, steal quietest) |
|---|---|---|
| 1 | 0 cut off | 0 |
| 3 | 23 | 0 |
| 5 | 27 | 0 |
| 8 | 46 | 0 |

One pass was fine, which is exactly why it read as intermittent.

Two changes. Sixteen slots rather than six — 40 bytes and one add per sample
each, both noise here. And when they are all busy, steal the **quietest** rather
than the next: a voice near the end of its decay contributes almost nothing, so
cutting it is inaudible, whereas round robin cut the freshest about as often as
not.

### Crash, third time

Down to 10% (0.73× a kick's energy, from 13.6× originally). Deliberately *under*
parity rather than near it — a 0.7-second wash that competes with the beat is
the complaint, and it does not need to match a kick to read as a crash.

### Y is automated too

The event format already had spare bits in `what` and the filter lane proved the
mechanism, so this generalised rather than duplicated: `kKnobEvent | lane`, two
lanes, one code path.

Two things needed care. The replace-on-tick rule had to become **per lane**, or
recording a Y move would delete a filter move sitting on the same tick and the
two knobs would erase each other. And the sample countdown had to be checked and
reset **once for both lanes** — a shared countdown consumed by whichever lane
asked first would starve the other completely. `tools/loopsim.py` asserts both.

The pickup logic also became a small `KnobPickup` struct rather than being
duplicated. It carries three fixes that were each a separate bug: playback
applies while recording, the reference is latched once, and the comparison is
smoothed.

---

## v1.7.0 — the kit was an octave and a half sharp

### Writing the documentation found a real bug

Asked to write up the sounds properly. Generating the frequency column from the
voice table rather than describing it from memory turned up the actual numbers:
a **kick at 2578 Hz**, a snare at 4688 Hz, and a **crash at 19.9 kHz**, which is
above most people's hearing. The whole kit was roughly an octave and a half
sharp.

The cause: the pitch numbers were carried over from Wild Pebble without checking
that its phase accumulator was the same width as ours. In a column of bare
integers there is nothing to notice — `220` looks as plausible as any other
number until you divide by the accumulator width and get kilohertz.

Two fixes. The accumulator is **18 bits** rather than 12, because at 12 bits the
frequency step is 11.7 Hz and a kick wants ~55 Hz — the nearest available
pitches were 47 and 59 Hz, with nothing in between. Six extra fractional bits
bring the step to 0.18 Hz.

And the table is now written as `HzToInc(62)` rather than `220`, so it can be
read and checked as pitches. That is the durable half of the fix: the unit is in
the source, so the next person to touch it sees a frequency rather than an
opaque increment.

### tools/kittable.py

The voice table is exactly the sort of thing that drifts — someone nudges a
decay shift and the README quotes the old number for a year. It is now parsed
out of `drums.cpp` and printed as markdown, so the documentation is generated
from the thing it describes.

It also computes the derived figures the source does not state: audible length,
pitch-fall time, and level as a percentage. Those are the numbers a player
actually wants, and none of them are readable off the raw parameters.

### Retuning fell out of having real numbers

With frequencies visible, several voices were obviously wrong beyond the octave
error: the closed hat lasted 6 ms and the metallic hi-hat 3 ms — clicks rather
than hats — and tom 1's pitch fall (97 ms) outlasted its own body (46 ms), so
most of the sweep happened after the sound had gone. Fixed by ear-plausible
numbers checked against the model rather than guessed.

---

## v1.6.0 — arming record, and Simmons toms

### Arming record silenced the loop, again — different cause

v1.3 fixed the version of this where recorded automation stopped applying while
recording. This was the mirror image: `RecordFilter()` had no reference on its
first call after the switch went up, so it read as a move and immediately
stamped the knob's *physical* position into the loop at the playhead. Park the
knob at a closed low-pass and the pattern goes quiet the moment you arm.

Arming now only seeds the reference. Automation is written when the knob
genuinely moves while recording — reach for it and it records, leave it alone
and the existing sweep is untouched. `ArmFilter()` re-seeds on both transitions,
so releasing and re-arming cannot compare against a stale value from an earlier
pass.

Worth noting the pattern: both bugs were "a transition into record does
something to the filter". That is the place to look first if it happens again.

### The toms fell linearly, which is the wrong shape

Asked for a Simmons-style pitch fall. The sweep was `pitch--` every N samples —
a straight line, which for tom 1 reached the floor in about **11 ms** and then
sat there. Almost the whole note was at one pitch and the sweep was just a click
at the front.

Decaying the *distance to the floor* instead gives an exponential glide: fast at
first, easing in at the bottom. That is the "pew". Tom 1 and syn tom 2 now fall
over ~95 ms, syn drum 3 over ~48 ms, while the kicks keep a fast 6–12 ms thump
and the snares 3 ms.

The `sweepRate` field became `sweepShift` and changed meaning (bigger is now
slower), so every voice's value had to be re-derived rather than carried over.
The difference is a Q8 accumulator for the same reason the envelopes carry
headroom — a plain shift on a small integer stalls, and the pitch would stop
short of the floor.

### Crash

Shortened to shift 14: the audible tail drops from ~1.5 s to ~0.9 s, and total
energy from 2.1× a kick to about 1.5×. Not the 75% asked for — decay shifts are
a factor of ~1.7 apart, and hitting 75% exactly would have meant raising the
level back up, undoing last round's loudness fix. Took the shorter, quieter
option deliberately.

---

## v1.5.0 — levels, and the loop stores sounds

### The crash was not slightly hot

Reported as "far far far too loud". Measured, it carried **13.6× the total
energy of a kick** and 433× a closed hat.

The cause was structural rather than a bad number: every voice fires at the same
peak, and the ear integrates over roughly a tenth of a second, so a 1.9-second
decay simply *is* more sound than a 230 ms one. Peak amplitude stops being a
proxy for loudness once decay times differ by two orders of magnitude.

Each voice now carries its own Q8 `level`, set by energy-equalising against the
kick and then trimmed so cymbals still read as cymbals. The crash is 40/256,
which brings it to 2.1× a kick — still a big sound, no longer burying the kit.
Its peak is 317 against the kick's 2045, which is what a long wash should look
like.

### The loop stores voices, not gestures

Previously an event recorded the *combination*, and replay re-derived a sound
from it — which meant a recorded hit could come back as a different voice from
the one played, because the shift that selected it was long gone.

The kit is now a flat list of twelve voices with a separate gesture→voice map,
and the loop stores the voice index. A pattern is a list of sounds; how each one
was played is a property of the performance. It also means re-arranging the
gesture map later cannot silently change what an existing loop plays.

### Filter pickup needed two fixes, not one

The knob reclaims the filter from recorded automation on a real move. The first
version compared against a reference that was **updated every time the threshold
was crossed**, so ADC dither could ratchet: each small step moved the reference,
the knob "travelled" without being touched, and playback was yanked away
mid-loop.

Latching the reference once, on the handover, fixes the ratchet — but modelling
it showed that is not sufficient on its own. With noise comparable to the
threshold, single samples still cross it: at ±70 counts a still knob handed back
17,000 times per 200k ticks even with the reference held.

So the comparison is against a **smoothed** reading. That gives zero spurious
hand-backs up to ±100 counts of dither, while a genuine 400-count move still
crosses in about a millisecond.

### CV Out 2 gates the bassline

Idle in DRUMS, so it now blips 5 V on each shift press. That lets the bass voice
have its own envelope rather than sharing Pulse Out 1, which opens on every drum
hit. Live presses only — a recorded pattern contains no shift presses, and
firing the gate from playback would be claiming something that did not happen.

---

## v1.4.0 — twelve voices out of six combinations

### Press order is not in the voltage, but it is recoverable

Asked whether the card could tell A-held-tap-B from B-held-tap-A. The honest
first answer was **no**: both close the same two switches, so the resistor
network produces one identical level. Order is not in the signal.

But it is in the *approach*. Holding A and tapping B arrives at the AB level
**from A**; holding B and tapping A arrives at the same level **from B**. The
detector already knew the previous level — it just threw it away. Latching it at
the moment a pair triggers recovers the ordering the voltage lost, and turns six
combinations into twelve gestures for the cost of one `int8_t`.

That is the whole change, and it is worth noticing that the information was
sitting there the entire time: the ghost rule was already using the same
"where did we come from" signal to suppress releases.

The kit is now indexed `[shift][tap]` rather than by combination. The loop still
records combinations — a fifth byte per event to store the ordering was not
worth it, and a replayed pattern keeps its voice either way.

### The drum envelopes had the same bug as the KEYS ones

Found while checking the crash would actually ring. `e -= (e >> shift) + 1` caps
out once the `+1` dominates, which at a 4095 peak is around shift 12 — so shifts
12 and 13 were **identical**, everything topped out at 85 ms, and a "crash" was
a blip. Exactly the bug fixed in `keys.h` three versions ago, still sitting in
`drums.cpp` because nothing had needed a long drum until now.

Six bits of headroom (`kDrumEnvFrac`) puts the range at ~6 ms to ~1.9 s. The
crash is 1.9 s, the kicks have real body at 230–400 ms, the closed hat is 12 ms.
The Y knob's ±3 shift is meaningful at both ends now instead of doing nothing
above the middle.

### A metallic voice type

Cymbals need more than filtered noise, which reads as "shh". The `metal` flag
ring-modulates the noise against a second phase accumulator at a deliberately
non-integer ratio (about 1.47×, plus an offset so it never locks), and squares
the body so the voice is all edge and no thud. Two accumulators and a sign flip
— no extra filters, no tables.

### Layout notes

The mapping is the player's, chosen for the hand rather than for tidiness: a
right thumb on **C** leaves closed hat, snare and kick under the fingers, which
is a whole beat without moving. **D** is the tom row for fills, **A** and **B**
carry colour and the crash.

---

## v1.3.0 — playability

### Recording muted the loop, which made overdubbing nearly impossible

Reported as the loop going quiet on arming record. Nothing muted the hits — the
cause was one gate on the *filter*: recorded automation stopped applying while
`recording_`, so the DJ filter snapped to wherever the knob physically sat. Park
it at a closed low-pass and everything goes silent, which is exactly when you
are trying to play along.

The gate was protecting against playback fighting the live knob. But the knob
already wins the instant it *moves*, which is the real requirement, and it does
that without the loop having to disappear first. Playback now applies while
recording too.

### The tap fires on press

It fired on *release*, so that a tap and a hold could share one control without
the hold also firing a tap on its way past. Logically tidy, and completely wrong
to play: the retrigger arrived when you let go, so every hit landed late by
however long your finger stayed down.

Firing on press costs exactly what the old design was avoiding — beginning a
hold now also fires one tap. That is fine here because the two never conflict: a
retrigger just before entering calibration is harmless, and a capture just
before aborting one is discarded with the abort.

One real consequence, found by tracing rather than by ear: the stray capture
sets `phaseTimer_` and a Confirm flash, and `LearnTick()` returns early while a
phase timer runs — so the abort's own timer was being overwritten and **the
abort was silently swallowed by the gesture that triggered it**. `AbortLearn()`
now clobbers both fields unconditionally.

### A bassline on the spare output

The four singles make no drum sound, so CV Out 1 was doing nothing in DRUMS.
They now play four notes — root, a tone below, the fifth, the octave — gated on
Pulse Out 1 alongside the hits. No combos, no scale, no quantiser.

Rooted an octave up rather than at 0V, because B is a tone *below* the root and
the output cannot usefully go negative: at a 0V root that note would be −167mV,
which most oscillators ignore. At 1V all four sit between 0.83V and 2V.

It also falls out of the shift gesture for free — holding a button to reach the
kit holds its bass note too, so the root sustains while you play drums over it.

### Calibration starts at power-up

The learned levels are RAM-only by design, so every power-up begins
uncalibrated and holding the switch for two seconds was the first thing you did
anyway. Now it just happens. Still escapable with the same hold, falling back to
the evenly-spaced default.

### tools/checkyaml.py

`info.yaml` broke on an unquoted colon inside a description for the second time
in two sessions — `KEYS: the thing` is a YAML mapping, not prose. There is now a
validator that catches it, names the likely cause, and also checks the things
that would parse fine while still being wrong: unknown socket ids, missing
switch positions, and the version string diverging between its two homes.

---

## v1.2.0 — tuning, and getting out of the way

### The root is 0 V now, and the old root was clipping

Reported as "feels like there's an octave jump" when tuning oscillators
together. It was worse than an offset: `kBaseNote` was MIDI 36, converted
straight to millivolts, so **the root sat at 3 V**. Matching an oscillator meant
winding it down an octave and a half.

The part that only showed up on inspection: 3 V of a ~6 V output was simply
gone. The top of the widest scale with full transpose asked for **7.3 V** and
clipped silently — the highest notes were flat and nothing said so.

Root is now 0 V, so an oscillator at its own zero is already in tune, and
transpose runs upward from something neutral. The transposed root is capped at
36 semitones, sized against the genuine worst case rather than a guess: the
Maj7 arpeggio's *harmony* voice reaches degree 11, which on a four-note scale is
35 semitones above the root. Checked across all twelve scales, every degree and
both transpose extremes — the full range is now 0 V to 5.92 V, with nothing
clipped.

The arpeggios are the ones that bite. Few notes per octave means degrees climb
fast, and it is the harmony voice, not the played note, that reaches highest.

### LEDs 4 and 5 go dark in KEYS

LED 4 flashed on every note — information LEDs 0–3 already carry, since the
combo lights up as you play it. A second light saying "a note happened" only
competes with the one that says *which* note.

LED 5 sat on permanently to mean "calibrated". True, but not actionable
mid-performance, so it was a light to learn to ignore.

Both keep their jobs elsewhere — phase markers during calibration, and LED 4 is
the beat in DRUMS. Here they say nothing, which is the right amount.

### Pulse Out 2 is a click track in DRUMS

One blip per crotchet, running whenever the loop is, so there is something to
record along to.

Driven from a new `BeatEdge()` rather than the existing `OnBeat()`. That
distinction matters: `OnBeat()` is a *level* that stays true for the whole tick,
and at 40 bpm a tick is dozens of control steps — a click that is on more than
it is off. The edge is latched inside `Advance()` where the crossing happens
exactly once, and consumed by reading.

### A note on the docs

`info.yaml` broke on an unquoted colon inside a description. Caught by validating
it rather than by eye, which is the second time that check has earned its keep.

---

## v1.1.0 — after the first hardware session

Calibration and triggering both worked first time on real hardware, which is the
part that was genuinely uncertain. Everything below is what playing it revealed.

### Singles become SHIFTS in DRUMS

The biggest change, and it came from a rationale worth recording: **percussion
is mostly repeated hits on the same drum**, and a keyboard reading of the
buttons cannot do that. To play AC twice you have to pass back through C — and
if C is itself a sound, every repeat is interrupted by a spurious one.

Making the four singles silent turns them into bank-selects that can be *held*
indefinitely. Hold C, tap A, tap A, tap A: three clean hits.

Two consequences that were not obvious until it was written:

- **The kit had to be remapped.** With singles silent, the six pairs are the
  entire playable kit — and they were holding a rim, three toms, a clap and a
  cowbell while kick, snare and both hats sat on the now-unreachable singles.
  Six sounds you cannot build a beat from. Kick is now the top row, snare the
  bottom row, hats a column and a diagonal, so every shift gives three usable
  voices under one hand.
- **The erase gesture had to move.** It was "hold the switch UP with no hits
  played", which cannot survive shifts being held for long stretches — "no hits
  played" becomes true far more often than the player means. Erase is now
  re-entering calibration, which is the one moment a stale loop is *guaranteed*
  meaningless: the levels are about to change, so the combos the pattern refers
  to may not exist afterwards.

### The envelopes could not be made long — a real bug

Reported as "too short"; the cause was worse than tuning. `e -= (e >> shift) + 1`
reaches zero because of the `+1`, but that same `+1` dominates as soon as
`e >> shift` rounds to zero — which for a peak of 2047 happens around shift 11.
**Every setting above that decayed in the same 43 ms.** The whole top half of the
macro knob did nothing at all.

Running the accumulator 8 bits higher (`kEnvFrac`) keeps the shift meaningful:
the range is now ~25 ms to 5.7 s.

Two related fixes while in there. The peak no longer scales with the macro —
scaling both meant a short setting was also a quiet one, so there was no way to
get a short *loud* note, which is most percussive playing. And the LENGTH
envelope is gone: on the panel it was indistinguishable from LOUDNESS, both
being "how long does this note last", so it cost an output to say nothing new.

### CV Out 2 is a harmony voice now

The freed output carries a second 1V/oct pitch, two scale degrees above the note
played, resolved through the *same* scale — so it lands major or minor according
to position, and follows the Y knob for nothing.

One wrinkle found by checking rather than by ear: two degrees is a third in
anything diatonic, but in **Chromatic** it is a whole tone, which reads as a
detune rather than a harmony. Chromatic gets four degrees instead. The arpeggio
scales give fifths at two degrees, which is correct and left alone — on a chord
tone, the "third up" *is* a fifth.

### Overdub "overwriting" was automation starvation

Reported as the looper overwriting previously recorded hits. It never overwrote
anything: filter automation and drum hits share one 512-event array, a
continuous knob sweep emits ~96 events per pass with **no upper bound**, and
after about five passes of idle twiddling the array was full — at which point
`Insert()` silently dropped everything and new hits stopped being recorded.

Fixed three ways: automation is capped at 128 slots, it *replaces* itself on a
tick rather than accumulating, and a hit evicts the oldest automation if the
array is full. A performed hit always outranks a knob position from three passes
ago. `tools/loopsim.py` now asserts all of it.

### Smaller things

- **LED 5 stopped blinking.** It meant "calibrated, but two combos collided" —
  correct information, delivered as a standing fault light that pulled the eye
  while playing. The warning now happens once, at the end of the calibration,
  where it can actually be acted on.
- **The LEDs showed the ghost, not the sound.** While a ghost is armed the
  tracker's `Current()` is the released-onto single, but the *pair* is what you
  can still hear — so the display contradicted the sound on every release.
  `Sounding()` reports the pair for as long as the ghost holds.
- **Calibration can now fail.** Ten captures landing on the same voltage means
  nothing was patched in; the card used to accept that and then play one note
  forever, looking calibrated and behaving broken. It now rejects a span under
  ~1.2 V, keeps the previous calibration, and says so with the two LED columns
  alternating — deliberately unlike the fade of success or the double blink of
  an abort.
- **Pulse In 1 is an external clock in DRUMS.** One pulse per beat, overriding
  the X knob while it runs and handing back ~3 s after it stops. It nudges into
  phase rather than snapping, so locking up never stutters the pattern. Verified
  to track within 0.05 BPM across 60–240.

---

## v1.0.0 — first build

### What the card is for

The Workshop System's **Four Voltages** is described by Music Thing as a
"minimum viable keyboard". On its own it is less than that: four non-latching
buttons into a resistor network, producing voltages that are arbitrary, that
every position of its knob changes, and that nothing downstream can interpret.
NIBBLE is the missing half — it learns the voltages and turns the combinations
into notes.

### The problem that shaped everything

Four Voltages **does not return to a rest voltage**. Let go of everything and
the output sits at the last-pressed button's level. So releasing A+B leaves the
CV exactly where pressing A alone would put it.

There is no signal-level way to tell those apart, which means "detect a level
change, play a note" fires a spurious note on every release. Every design that
starts from "quantise the incoming CV" runs straight into this.

The resolution — the **ghost rule** — is to suppress exactly one level at a
time: when a pair is released onto one of *its own two buttons*, that level is
ignored, and only until the voltage moves elsewhere. Everything else plays,
including a bare single that was part of the pair just released.

It is worth recording that this turned out to be a *feature*, not damage
control. Hold C, tap A, tap B: you get AC and BC, with silent releases, forever.
The held finger becomes a bank-select and the tapping finger plays. That falls
straight out of the rule with no special-casing, and it is now the technique the
card is documented around.

### Decisions worth keeping

**Degrees, not pitches.** The obvious design quantises the incoming voltage to
the nearest scale note. That is wrong here: the levels are resistor-divider
outputs whose spacing is a property of the network, not of music. Nearest-note
quantising would remap the whole keyboard every time the Four Voltages knob
moved, and would collapse several combinations onto the same note whenever two
levels landed inside a semitone. Indexing by degree means combination → degree
is fixed forever, and the voltage only ever decides *which combination*.

**No persistence.** Tempting, and wrong. The Four Voltages knob invalidates
every learned level, so a saved calibration would silently restore a *wrong*
one. A card that looks calibrated while playing the wrong notes is worse than
one that admits it knows nothing.

**Tap to confirm, rather than auto-capture.** The first design waited for the CV
to move by a threshold and then settle. That could not distinguish "the player
has not pressed yet" from "the player pressed something that reads nearly the
same as the last combination" — so two close combinations caused a *timeout* and
an aborted learn. With an explicit tap the card captures what it is told to,
flags the collision, and **completes**. Degrading to a warning beats stalling on
the one thing you cannot separate.

**Events, not audio, in the looper.** Overdub is lossless, tempo re-times rather
than pitches, and the whole four-bar loop costs 2KB instead of ~400KB.

**One core.** WorkshopBio needed a second one only for TinyUSB. There is no USB
here, so there was nothing to move — and launching a core you do not need adds a
boot-order hazard for free.

### Four bugs the models caught

There is no host C++ compiler on this machine, so the risky logic was written in
Python first and ported. That paid for itself four times over — each of these
would have been miserable to diagnose by ear on hardware.

**1. A one-pole slew that stalled, asymmetrically.** `v += (target - v) >> shift`
stops moving once the difference drops below `2^shift`. On a signed shift the
stall is *directional*: exact from above, ~`2^shift` short from below. A settled
reading therefore depended on which combination it was approached from — 17
units of error, over a quarter of the collision threshold, which put two
combinations on the wrong side of a learned boundary. Fixed with `slew_exact()`;
the level detector now round-trips all ninety approach directions with zero
error. The stalling version is kept, documented, for audio smoothing where it is
harmless.

**2. A soft clipper with 0.33× gain.** The input was normalised by `3*kLimit`
and the output scaled back by only `kLimit`. Every drum sound played a third too
quiet — plausible enough to have shipped unnoticed. Now unity below the knee,
asserted by test.

**3. A looper sorted by the wrong key.** Events were sorted by their raw
recorded tick, but quantisation moves an event's *fire* time, sometimes across
the loop boundary. The playback cursor walk assumes sorted-by-fire-time, so it
misfired. Sorting by `FireTick()` restores the invariant.

**4. A looper that double-fired.** The fire condition was "anything overdue"
(`when <= playHead`), which looks like the safe choice. It is not: an event
firing at tick 0 then matches at *every* tick until the cursor passes it. Exact
matching is correct, and safe because the tick increment is well under one tick
even at 240 BPM.

Two of the four (1 and 3) were caught by a test that does not check a mechanism
at all, but a *round trip*: learn ten levels, then replay all ten and assert each
classifies back to itself. Worth remembering as a pattern.

### Two tests that were wrong before the code was

The DJ filter's monotonicity check failed twice against a filter that was
correct:

- measured at 1 kHz, it saw the resonant peak legitimately swell and fall as the
  corner swept past. Asserting otherwise would have meant deleting the resonance
  that makes it sound like a DJ filter.
- measured at 50 Hz, it saw nothing, because 50 Hz is inside the passband at
  every knob position.
- and with a sparse frequency grid, the Q≈2.7 peak swept *between* the sample
  points, so the total wobbled by tens of percent from measurement artefact
  alone.

Broadband energy over a dense log grid is the honest measure. Worth a note
because "the test failed, so change the code" would have made the card worse
three times running.

### Where it stands

Builds clean at `-Wall -Wextra -Wdouble-promotion -Wfloat-conversion`: ~4.6% of
flash, ~8.3% of RAM. Nothing has touched hardware yet — everything above is
model-verified and compiler-verified only.

### Untested on hardware

In rough order of how likely they are to need attention:

1. **The tolerance constants in `levels.h`.** `kSettleTol`, `kMatchWindow` and
   `kCollisionMin` are derived from the expected divider spacing, not measured.
   They are grouped at the top of that file precisely because they are expected
   to move.
2. **Whether ten combinations are separable at all** on a real Four Voltages,
   and at which knob positions. This is the card's central gamble.
3. Drum voice tuning — the ten presets are plausible numbers, not auditioned
   ones.
4. Envelope decay ranges, and whether the macro's three anchor relationships
   feel distinct in a patch.
5. Loop feel: whether 1/16 quantisation is right, and whether hold-UP-to-erase
   is comfortable or alarming.

### Synthesised drums, not samples — decided, not deferred

Considered and settled for v1.0: the kit stays parametric.

Baking PCM in is entirely practical — WorkshopBio's `tools/mksamples.py`
pipeline would drop straight in, and there is plenty of flash spare at 4.6%
used. It was rejected because of what it *takes away*:

- **The Y knob stops working the way it does.** "Kit character, lower and longer
  ↔ higher and shorter" is one multiply on parameters that are already live
  variables. On samples the same gesture is a resampler plus an envelope
  re-shaper, and pitch and decay stop being independent.
- **It drags the whole upload apparatus back in.** A reserved flash region, a
  USB stack, a web editor, and the flash-write dance where core 0 has to park
  itself in RAM before XIP drops. That is most of WorkshopBio's complexity, for
  a card whose point is that it needs one patch cable and no computer.
- **Ten sounds cost nothing today.** Adding an eleventh is a row in a table.

The honest counterargument is that synthesised hats and claps never sound as
good as recorded ones, and if the kit disappoints on hardware that is the first
thing to revisit. Revisit it as a *variant build* rather than by bolting uploads
onto this one.

### Ideas deliberately left out

- External clock into Pulse In 2 for DRUMS (Pulse In 2 is unused and reserved).
- Variable loop length; four bars is currently fixed.
- Swing / humanise — the raw untouched tick is already stored for it.
- Anything on Pulse Out 2.
