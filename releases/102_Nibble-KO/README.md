# NIBBLE-KO

**A twelve-voice synth/sample percussion looper for the Music Thing Modular
Workshop System Computer, played by shift-and-tap on the Four Voltages
module — with a browser sample manager.**

*Four buttons. Ten voltages. Twelve drums, yours to fill.*

> **Status: 1.2.0, released.** Drums, the looper, mute groups, twelve
> performance effects, three pattern slots, sample playback, the browser
> sample manager, flash-saved calibration and the CV expansion all work on
> hardware. Patterns save to and load from your computer as JSON, and the loop
> keeps playing while the browser tool is connected. **New in 1.2.0:** the loop
> length is settable from 4 to 16 beats, so odd time signatures are playable.
> See [CLAUDE.md](CLAUDE.md) for exactly what's here and what isn't.

A program card for the [Music Thing Modular Workshop System
Computer](https://www.musicthing.co.uk/workshopsystem/) that expands the
Percussion half of [NIBBLE](https://github.com/uglifruit/WorkshopNibble)
into a dedicated drum machine: twelve voices, each independently synthesised
or sample-based, freely assigned and uploaded from a web interface — no
toolchain, no reflashing.

## Lineage

NIBBLE-KO combines two existing cards rather than starting from nothing:

- **The playing technique and looper** come from
  [NIBBLE](https://github.com/uglifruit/WorkshopNibble)'s Percussion half:
  Four Voltages' four non-latching buttons read as ten learned voltage
  levels, with a *shift* trick that recovers press-order from a signal that
  otherwise throws it away — hold one button as a bank select, tap another to
  play it, turning six two-button combinations into twelve distinguishable
  gestures. A four-bar event looper with lossless overdub sits behind it.
- **The sample management** comes from
  [BioMimicry](https://github.com/uglifruit/WorkshopBio)'s USB-MIDI SysEx
  upload pipeline: drop a WAV in the browser, it's converted, uploaded, and
  written into a reserved flash region that survives reflashing the firmware.

## Playing it

The momentary switch is a **mode selector**, not a trigger. Its three
positions:

| Switch | |
|---|---|
| **Down** | choose a mode — hold it, press a button, and the choice lands when you **release** |
| **Middle** | play the chosen mode |
| **Up** | play it *and record* into the loop |

The mode **latches**, so Down and Up never need to be held at once — which is
what lets every mode be recorded through one mechanism.

| Hold switch, press | Mode |
|---|---|
| **A** | **Drums** — the kit (where you start) |
| **B** | **Mute** — three mute groups |
| **C** | **FX** — twelve performance effects |
| **D** | **Pattern** — three stored loops |

And with a **pair** held, one-shot actions:

| Hold switch, press | |
|---|---|
| **A + B** | **Undo** — revert the last recording pass (not pattern stores; see below) |
| **A + C** | **Quantise** — cycle the record grid: 16th → 12th → 8th |
| **C + D** | **Play / Stop** |
| **B + D** | **WebUI** — hand the card over to USB for the browser setup tool |

The adjacent pairs carry what you reach for mid-take. The WebUI takes the
awkward diagonal, since it is a setup activity done with both hands free.

> **Why singles commit on release but pairs fire instantly.** Four Voltages
> holds its last voltage, so the CV may already be sitting on the single you
> are trying to pick — pressing it produces no change at all, and a
> press-driven select would never fire. Reading the state on *release* is
> immune to that. A pair cannot get stuck the same way, because letting go of
> one falls back onto a single, so a pair press is always a real transition.

### Drums

Singles are **shifts**, not sounds. Hold one and tap another to play: twelve
ordered gestures from four buttons, because hold-C-tap-A and hold-A-tap-C are
different gestures on an identical voltage.

Main is a DJ filter (low-pass left, high-pass right, bypass at centre), X is
tempo, Y is kit character. All three are recorded when the switch is Up.

### Mute

Hold **B**, tap **A**, **C** or **D** to toggle one of three mute groups. The
pads show each group's hits going past — full brightness when it sounds, half
when it is muted, so you can see the pattern you have silenced and time
bringing it back.

**Mutes are not recorded, and they persist across pattern recalls.** A mute
is a mixer move rather than part of the music: it says "not this group, right
now". Recording it would bake a live judgement into the loop and make you
fight the recording to change your mind. Keeping it as live state instead
makes mutes an arrangement layer *above* the patterns — drop the hats, swap
patterns, and the hats stay dropped.

### Pattern

Three slots, and the **switch is the verb**:

| | |
|---|---|
| **Middle** + hold D, tap A/B/C | **recall** that slot, instantly |
| **Up** + hold D, tap A/B/C | **store** the live loop into it |

That reuses what Up already means everywhere else — *commit this to the
loop* — so there is no new gesture to learn.

Recall keeps the playhead where it is, so switching mid-bar reads as the band
changing part rather than a stop and start. It **discards** whatever you were
playing, which is what lets you try something over a pattern and abandon it.

**Undo does not reach the slots.** It is about *recording* — the hits,
effects and knob curves a pass with the switch Up put into the live loop.
Storing a pattern is a separate deliberate act on separate state, and a
gesture that sometimes meant "undo my playing" and sometimes "un-store that
slot" would be two features sharing one name. What protects a slot instead is
that storing refuses an empty loop, so the destructive case cannot happen by
accident.

The pads show three states: **full** is the pattern playing, **half** is a
slot with something in it, **dark** is empty — so what you can jump to is
readable at a glance.

> **Patterns live in RAM only** — they do not survive a power cycle. Getting
> them in and out over the web app is the next natural step, and a pattern is
> a good candidate for it: a bare event list under 2KB, carrying no audio.

> A hold-to-store gesture would not work on this hardware. Four Voltages
> *latches*: a held button is a level that sits there indefinitely, so any
> "held for a second" test passes eventually and every recall would become a
> store.

### Quantise

**switch + A+C** cycles the grid new hits snap to: **16th → 12th (triplet) →
8th**, and round again. The two status LEDs show which, as a pattern you read
at a glance rather than a count:

| LEDs | Grid |
|---|---|
| **4** alone | 16th |
| **4 and 5** | 12th (triplet) |
| **5** alone | 8th |

The middle setting sits visually between the outer two, which is also where
it sits musically.

**It only affects hits recorded from that point on.** A hit is snapped once,
at the moment you play it, and never moves again. That is what lets a
straight part recorded under 16ths and a triplet fill recorded under 12ths
live in the same loop, each on the grid it was played against — which a
single live "playback grid" setting could not do, since one divisor would
apply to every hit at once and switching to triplets would drag the straight
part along with it.

The trade is that a hit's original unquantised timing is gone the moment it
is recorded, so a future "loosen the grid" control could not recover it.

### FX — twelve effects, four banks

Hold any single as a shift, press another for its effect. The shift picks the
**family**, which is what makes twelve gestures learnable as four groups:

| Hold | Tap | Effect | Main knob |
|---|---|---|---|
| **A** *filters* | B | Low-pass | cutoff |
| | C | High-pass | cutoff |
| | D | Band sweep | centre |
| **B** *destruction* | A | Bit-crush | bits discarded |
| | C | Decimate | sample-rate reduction |
| | D | Wavefold | drive |
| **C** *rhythmic* | A | Stutter | repeat rate |
| | B | Flam | gap, 5–35 ms |
| | D | Gate | chop rate |
| **D** *transport* | A | **Reverse** | — |
| | B | **Tape-stop** | fall time, 50 ms–2.4 s |
| | C | Silence | — |

Effects are **momentary** — held, not toggled — and one per bank, so up to
four layer at once, processed in series.

**The transport bank means different things to different voices**, which is
the point of grouping it: the gesture is "play it wrong on purpose", and what
that means depends on what the voice is made of.

| | Sampled voice | Synthesised voice |
|---|---|---|
| **Reverse** | plays backwards | envelope backwards — a *swell* |
| **Tape-stop** | rate winds to nothing | *pitch* winds to nothing |

Since five voices are sampled and seven synthesised, one reverse press gives
you backwards kick and hats over swelling toms and cymbals at the same time.

Both are armed at **trigger time**, so they catch hits that start while the
effect is held and leave anything already sounding alone.

### Recording effects

Each shift owns **two** recorded lanes — which effect is running, and that
lane's parameter curve — performed separately:

- **shift + tap**, switch Up: records the effect popping in and out
- **shift alone** + Main, switch Up: draws that lane's parameter curve

So you can sweep a filter curve once, then punch the effect in and out over
the top of it on later passes without re-recording the sweep. Turning Main
with *no* shift held does nothing, deliberately: with four curves and no
button down, which one you meant is ambiguous.

**There are four parameter curves, one per shift — not one per effect.** The
three effects under a shift share it: `B+A`, `B+C` and `B+D` all read and
write the same curve. That is what makes the shift a coherent *layer* rather
than three unrelated things, but it has a consequence worth knowing before
it surprises you — recording a depth twiddle under `B+C` overwrites a curve
you recorded earlier under `B+A`, wherever the two overlap in the bar.

**To play over a recorded curve without disturbing it, jam with the switch
at Middle.** Your hand takes the depth live while you hold the gesture, the
recorded curve is left alone, and it picks up again the moment you let go.
Only the switch at Up writes. That is the difference between trying
something and committing it, and it is the same distinction everywhere else
on the card.

## Calibration is the whole game

Patch **output 1** of Four Voltages into CV In 1, with its **knob at about
twelve o'clock**. A normal power-up loads the last saved calibration from
flash and is playable immediately. Holding the switch **Down** through the
first moment of boot forces a fresh calibration instead: the LEDs show a
combination, you hold it and tap the switch to capture, ten times over. A
successful run is saved, and stays saved across power cycles until the next
alt-boot learn replaces it — so recalibrate only when the Four Voltages knob
has actually moved. Aborting a re-calibration (hold the switch 2s) keeps
whatever was saved before, rather than losing it.

> **Give it well-separated voltages, or nothing else will work properly.**

That knob moves all four outputs at once, and towards either extreme several
combinations collapse onto nearly the same voltage. When two learned levels
sit close together the symptom is not a clean failure but an intermittent
one: a pad that plays the wrong drum, fires twice, or does nothing — and does
it differently each time, because a few millivolts of drift moves the reading
across the boundary. It reads as a flaky card rather than a mis-set knob.

The card tells you when this has happened. **LEDs 4 and 5 blinking** during
calibration means that capture was too close to an earlier one. A run that
finishes with no warnings will behave predictably; one that warns will not —
so move the Four Voltages knob and calibrate again rather than trying to play
around it.

## Twelve voices, your choice of source

Every voice slot can be either a synthesised drum voice (phase-accumulator
body, noise mix, pitch sweep, the same DSP as NIBBLE's kit) or a sample.
Five are currently sampled from the Cheetah SpecDrum's original kit — kick,
snare, closed hat, open hat, clap — and seven stay synthesised. A mix, not a
wholesale swap: the synthesised voices are the ones whose Y-knob reshaping
earns its keep, and a bit-crushed synth tom next to a sampled clap is the
point of the card.

The Y knob is playback rate on a sampled voice and pitch-plus-decay on a
synthesised one — the same gesture either way, making the whole kit
higher-and-shorter or lower-and-longer.

Which sample a voice plays is a mapping of indices, not baked audio, so the
browser will be able to re-point it at runtime. See
[samples/README.md](samples/README.md).

## Panel

| | |
|---|---|
| **CV In 1** | one output of Four Voltages — the instrument |
| **CV In 2** | takes over the Main knob while patched |
| **Pulse In 1** | external clock, one edge per beat |
| **Pulse In 2** | while high, a random effect is applied |
| **Audio In 1** | depth of that random effect |
| **Audio In 2** | chaos — how busy the glitch outputs are |
| **Main** | DJ filter, or FX depth while in FX mode |
| **X** | tempo, 40–240 BPM (ignored while clocked) |
| **Y** | kit character — pitch and decay on synth voices, playback rate on samples |
| **Audio Out 1 / 2** | the drum bus, mono, both the same |
| **CV Out 1** | glitch gates, sparse and beat-anchored |
| **CV Out 2** | glitch gates, dense and syncopated |
| **Pulse Out 1** | gate on every hit |
| **Pulse Out 2** | click, one per beat |
| **LED 4 / 5** | recording / beat |

## Patching it

Everything above works unpatched — the card is played from its own panel and
nothing here is required. But six sockets turn it into something you can
drive from the rest of the rack.

**CV In 2 takes the Main knob.** Whatever Main does in the current mode is
then under voltage: the DJ filter in DRUMS and MUTE, the held effect's depth
in FX. It overrides rather than offsets, the same way an external clock
overrides the tempo knob — a cable wins.

**Pulse In 2 fires a random effect** for exactly as long as it is high. It is
level-sensitive, not edge-triggered, so the gate's *width* is the effect's
duration. The effect is rolled once when the gate opens and held until it
closes, and it sits below both your hands and anything recorded — so it
colours the slots you are not using rather than fighting you for one.
**Audio In 1** sets its depth.

**Audio In 2 is chaos**, and it drives the two glitch outputs. Turning it up
raises the odds of any division firing *and* widens the pool: at the bottom
only beats are candidates, at the top every half-division is, so more chaos
is finer as well as busier. Past about two thirds the dense output starts
producing ratchets. It never reaches certainty — a stream that fires on every
candidate is a pulse train, not a random one.

**CV Out 1 and 2 are the glitch gates.** Both follow the live quantise grid,
so they always agree with what the card is recording to. CV Out 1 is sparse,
weighted hard toward the downbeat, with longer gates — usable as an accent
track on its own. CV Out 2 is dense, syncopated, deliberately skipping the
beat that CV Out 1 already owns.

These are **gates, not triggers**: each one lasts most of the division it
fired on, so it scales with the tempo and is long enough to be a musical
effect rather than a click. That matters because the gate's width is exactly
how long Pulse In 2 applies its random effect for.

> **The patch worth trying first:** CV Out 2 into Pulse In 2. The card's own
> glitch generator now fires its own random effects, and Audio In 2 is the
> single knob that takes it from occasional to falling apart.

## Patterns are sound-agnostic

Worth stating because it shapes what everything else can be: a recorded
pattern holds **voice indices**, never sounds. "Slot 4 fired at tick 96" —
not which sample slot 4 happens to play. The chain to audio is

```
pattern event → voice index → voice/sample map → bank index → audio
```

and only the last two links know anything about sound.

So re-pointing a voice at a different sample, or uploading a new one, changes
what every existing pattern *plays* without altering a byte of the pattern.
The same four bars can be a Cheetah kit or your own recordings.

That falls out of NIBBLE's decision to record the *voice* rather than the
*gesture*, which it made for an unrelated reason: so re-arranging the gesture
map couldn't silently change an old loop.

## Saving patterns *(new in 1.1.0)*

The card's three pattern slots live in RAM and are lost at power-off. The
browser tool is where they persist: open the **Patterns** tab, and each slot
has **Save…** and **Load…**.

Save writes a plain JSON file to your computer — that file is the permanent
copy. Load pushes one back onto a slot, ready to play immediately with
**hold D, tap A/B/C**. Neither touches flash, so neither reboots the card.

Because patterns are sound-agnostic (above), the file holds voice numbers and
no audio at all — a few KB of readable JSON:

```json
{
  "card": "nibble-ko", "kind": "pattern", "slot": 0,
  "knobCount": 4,
  "events": [ { "tick": 12, "what": 3, "value": 100 } ]
}
```

So a pattern saved against one kit plays against a completely different one,
and patterns can be shared, versioned or hand-edited like any other text
file. Events are validated on the way in, and the card re-sorts and
range-checks whatever arrives, so a hand-edited file cannot corrupt playback.

The loop **keeps playing** while the browser tool is connected, so a voice
can be re-pointed at a new sound and judged by ear against the pattern it has
to sit in. Only recording stops; an upload still silences the card, because
writing flash stops everything.

## Loop length *(new in 1.2.0)*

The **Patterns** tab sets how many beats the loop plays before repeating,
anywhere from **4 to 16**. Sixteen — four bars of 4/4 — is the default and
what every earlier version did.

This is what makes odd metres playable. The click on Pulse Out 2 is
**unstressed** and nothing on the card marks beat one, so the length is a
count rather than a time signature: fourteen beats is 7/4 twice, or
5/4 + 2/4 + 3/4 + 4/4, or an eleven and a three. The card does not decide,
and neither does the click — you do, by what you play against it.

**Shortening is non-destructive.** The card always records against the full
sixteen beats and the setting only moves where the playhead wraps, so hits
past the new end are hidden rather than erased and restoring the length brings
them back exactly as they were. Pattern slots always hold the full sixteen.

The caveat is inherent rather than a bug, and worth knowing before you reach
for it mid-take: since nothing marks beat one, changing the length moves the
join to an arbitrary point in what you played. The part that vanishes — or the
gap a longer setting exposes — can land anywhere in the phrase.

## The browser setup tool

Hold the switch **Down** and press **B + D**: all four pads glow and the card
appears over USB as a MIDI device. Open [`web/index.html`](web/index.html)
in Chrome or Edge and click Connect. **The loop carries on playing**, so you
can hear each change against it; only recording stops.

Uploaded samples form a **library**, not a per-pad slot: one recording can be
played by several voices at once and costs the space of one. Every gesture
can be pointed at anything the card holds — its own synth voice, a built-in
sample, or one of yours — and you hear the change immediately, with Save Kit
making it survive a power cycle.

WAVs are converted in the browser to the same 8-bit 48kHz format the built-in
samples use. See [`web/README.md`](web/README.md).

## What isn't done


**Patterns live in RAM, and that is now a choice rather than a gap.** They
still do not survive a power cycle on the card — but since 1.1.0 the browser
tool saves a slot to a JSON file on your computer and loads it back, so the
file *is* the permanent copy. An on-card flash store was considered and
rejected: it would duplicate persistence the file already provides, at the
cost of a flash region and a write path. See **Saving patterns** above.

Two smaller things, unrelated to the roadmap above:

- **Only the Kit and Samples are configurable from the browser.** Mute
  groups and loop settings need SysEx messages that do not exist yet, so
  those tabs are reference displays.
- **Deleting a sample frees the slot, not the space.** Uploads append and
  nothing compacts the region; only Erase All reclaims bytes. The Samples tab
  shows both numbers.
- **Mute groups are hardcoded** three ways by voice index.

## Changelog

### 1.2.0

**Loop length is settable, 4 to 16 beats**, on the Patterns tab of the browser
tool. This is what makes odd time signatures playable: the click is unstressed
and nothing marks beat one, so fourteen beats is 7/4 twice, or 5/4 + 2/4 +
3/4 + 4/4, or whatever you hear in it. See
[Loop length](#loop-length-new-in-120).

It is a **playback** setting, so shortening is non-destructive: the card always
records against the full sixteen beats, and putting the length back brings the
hidden hits with it, unchanged. The caveat is inherent rather than a bug —
since nothing marks beat one, changing the length moves the join to an
arbitrary point in what you played.

**Fixed: Undo put the filter and tone knobs in arbitrary places.** Undo drops
the automation it deleted, but it did that by handing the lane back to the
PHYSICAL KNOB — whose resting position may be minutes old and unrelated to the
music, so the tone jumped. It now holds the last replayed value instead: an
undo is silent in the tone, and the next hand movement or recorded event takes
over as usual.

**Fixed: saving a pattern failed on busy patterns.** The dump sent one chunk
per USB service pass, and on a dense patch core 0 is busy enough that core 1
gets very few passes — so the transfer could not finish before the browser gave
up, and the failure appeared on exactly the patterns most worth saving. The
dump now drains in one pass.

**Minor optimisations to the control path**: software division removed from
the per-tick code (the Cortex-M0+ has no divide instruction, so each one was a
function call), the control-rate functions moved into RAM, and the automation
replace-scan bounded to its window rather than walking the whole event array.

### 1.1.0

**Patterns save to and load from your computer.** The Patterns tab in the
browser tool gains **Save…** and **Load…** per slot, writing plain JSON.
The card still keeps its three slots in RAM, so the file on disk is the
permanent copy. See [Saving patterns](#saving-patterns-new-in-110).

**The loop keeps playing while the browser tool is connected.** Entering the
WebUI used to stop it, which made every audition a stop/reassign/restart —
wrong for the thing the tool is mostly used for, since choosing a sound is a
judgement made by ear against the pattern it sits in. Only recording stops
now; an upload still silences the card, because writing flash stops
everything.

**Fixed: Undo could silence the loop until the start of the next pass.**
Undo replaces the event array and rebuilds the playback cursor from the
playhead. If the undone pass had added its hits *behind* the playhead, the
shortened array ran out and the cursor was left past the end — and since it
is only rewound when the loop wraps, nothing sounded for the rest of the bar.
It presented as intermittent, because it only bit when the undone hits were
behind the playhead. Recalling a *sparser* pattern mid-bar dropped out the
same way and is fixed by the same change.

**Fixed: the browser could read a reply meant for an earlier request.**
Replies share one queue, and a burst reply (the sample library, a pattern
dump) is drained only as far as its terminator — so anything queued behind it
was handed to the next request instead. It showed up as saving a pattern
before any slot had been stored failing with an unexpected-reply error.

**Firmware now advertises its capabilities.** An unknown SysEx message is
answered with silence, which a browser cannot tell apart from a card that
failed to reply — so a page talking to older firmware reported "the card did
not respond" and looked like a bug in working code. `MSG_INFO` now carries a
feature-bits byte, and the Patterns tab says *"this card's firmware predates
pattern transfer"* instead of timing out.

### 1.0.1

A latched voltage was overriding recorded FX depth.

### 1.0.0

First release.

## Credits

The baked drum samples are Cheetah SpecDrum ROMs (1986), from
<https://samples.kb6.de/>. See [samples/README.md](samples/README.md).

The browser setup tool's look — dark theme, EB Garamond, the whole visual
language, and a fair amount of the actual CSS — follows
[Johan Eklund's Resonator](https://johaneklund.io/resonator/) web UI for his
own Workshop System card. Its connection handling, ack queueing and upload
sequence are ported from `WorkshopBio/web/index.html` (Andy Jenkinson), whose
hard-won comments this card's `docs/LESSONS.md` leans on throughout — see
that file for the fuller lineage from NIBBLE and BioMimicry.

## Repo

`https://github.com/uglifruit/Nibble-KO` (public).

## License

CC BY 4.0 — see [LICENSE](LICENSE) — **except `web/`**, which is GPLv3
because its CSS is a derivative of Resonator's own GPLv3-licensed source
rather than an independent design. See [`web/LICENSE`](web/LICENSE) and
[`web/README.md`](web/README.md#provenance-and-licence).
