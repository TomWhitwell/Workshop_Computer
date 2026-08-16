# What NIBBLE learned — a handover for the next card

> **This document is INHERITED, not NIBBLE-KO's own.** It is a verbatim copy
> of NIBBLE's handover (`../WoskshopButtons/docs/LESSONS.md`), kept here
> because NIBBLE-KO is the card it was written for and because most of this
> repo's design decisions cite it. Everything below describes work done on
> NIBBLE and verified on NIBBLE's hardware.
>
> NIBBLE-KO's own findings are not here yet. When they accumulate they belong
> in a sibling file rather than mixed into this one — the value of this
> document is that every claim in it was paid for, and diluting it with
> untested additions would spend that.
>
> Where NIBBLE-KO has deliberately DIVERGED from the advice below, the
> divergence is explained at the point of departure:
>   - calibration is flash-persisted here, reversing §"Calibration is not
>     persisted" — see CLAUDE.md
>   - the singles are mode selectors, not just drum shifts — see main.cpp

Written for whoever builds the next Workshop System card on this lineage,
starting with **NibbleDrumMachine**: percussion only, with real samples rather
than synthesised voices.

This is not a summary of what NIBBLE *does* — the README covers that, and
`docs/DEVLOG.md` has the full version-by-version account. This is the part that
was expensive to find out: eleven versions and several hardware sessions of
things that were wrong, and the handful of techniques that turned out to matter.

Read this before writing any code. Most of it is not guessable.

---

## 1. The parts to reuse wholesale

### The level detector and the ghost rule (`levels.h` / `levels.cpp`)

This is the reusable heart of the card, and it is not specific to a drum machine
or a keyboard. It turns one Four Voltages output into a clean stream of
"combination pressed" events:

- ten combinations learned, four singles and six pairs
- a settle detector, so a reading is only believed once it holds still
- a Schmitt band so two close levels cannot flicker between each other
- **the ghost rule** — the thing that makes it work at all

Copy `levels.h`, `levels.cpp` and `tools/ghostsim.py` together. The Python model
is a line-by-line port and it has caught four real bugs; the two drifting apart
is exactly the failure it exists to prevent.

**The ghost rule, in one paragraph.** Four Voltages does not return to a rest
voltage when you let go — the output sits at the last-pressed button's level. So
releasing A+B leaves the CV exactly where pressing A alone would put it, and a
naive detector fires a spurious note on every release. The fix: when a *pair* is
released onto one of *that pair's own two members*, suppress that one level, and
only until the CV moves anywhere else. In `Step()`, `ghost_ = kComboNone`
executing **first and unconditionally** is what implements it. That ordering is
the rule; everything else follows.

**The shift trick, which doubles your gestures for free.** A+B and B+A close the
same switches and produce an identical voltage — press order is genuinely not in
the signal. But the level the CV came *from* is the button that was already
held. Latching that at the moment a pair triggers recovers the ordering, turning
six combinations into twelve distinguishable gestures for the cost of one
`int8_t`. See `LevelTracker::Shift()`.

For a drum machine this is the whole playing technique: **hold one button as a
bank select, tap the others**. Percussion is mostly repeated hits on one drum,
and holding C while tapping A repeatedly gives exactly that — which a keyboard
reading of the buttons cannot do, because to play AC twice you must pass back
through C.

### The event looper (`looper.h` / `looper.cpp` + `tools/loopsim.py`)

Four bars, events not audio. Take it as-is; it is the most heavily debugged file
in the card and every fix is in the comments. In particular it already handles:

- overdub that never degrades (nothing is re-recorded)
- tempo as a playback parameter, so the knob re-times rather than pitches
- two lanes of knob automation that do not erase each other
- an external clock that overrides the knob and hands back when it stops

### The verification tools (`tools/`)

There is no host C++ compiler on this machine. These fill the gap and they are
the reason the card works:

| Tool | What it does |
|---|---|
| `syntax.sh` | Type-checks every `.cpp` with the ARM cross-compiler in ~1s. Does not link, so it cannot catch a missing symbol — but it catches nearly everything else. |
| `ghostsim.py` | The level detector and learn pass |
| `loopsim.py` | Event ordering, overdub, tempo, clock |
| `dspsim.py` | Filter stability, soft clip |
| `checkyaml.py` | `info.yaml` parses **and** is structurally complete |
| `kittable.py` | Generates the README's voice tables from the source |

Write the model **before** the C++ for anything with tricky ordering. It is
faster to get right in Python and it stays as a regression test.

---

## 2. The bugs that will happen again

Every one of these was found on hardware, and most were invisible in the code.

### The `+1` in an exponential decay caps your range

`e -= (e >> shift) + 1` reaches zero because of the `+1` — and that same `+1`
dominates as soon as `e >> shift` rounds to zero. For a 4095 peak that happens
around shift 12, so **every longer setting decays in the same ~85ms**. Shifts 12
and 13 are identical. A "crash" is a blip and the top half of a decay knob does
nothing.

This bit **twice**: once in the KEYS envelopes, then again in the drum voices
three versions later, because nothing had needed a long drum until then.

**Fix:** carry fractional headroom in the accumulator (`kEnvFrac = 8` for
envelopes, `kDrumEnvFrac = 6` for drums) and shift down only at the output.

### A one-pole slew stalls, asymmetrically

`v += (target - v) >> shift` stops moving once the difference drops below
`2^shift`, permanently short of its target. On a *signed* shift the stall is
directional: exact from above, `~2^shift` short from below. So a settled reading
depends on which direction it was approached from.

Measured at **17 units of direction-dependent error** in the level detector,
which put two combinations on the wrong side of a learned threshold.

**Fix:** `slew_exact()` in `fastmath.h`. Use it anywhere a value is later
compared against a threshold. The plain `slew()` is kept for audio smoothing
where the residual is inaudible — the header says which is which.

### Check your phase accumulator width against your pitch numbers

The drum kit was carried over from another card without checking that its
accumulator matched. Every voice came out **an octave and a half sharp** — a
"kick" at 2578 Hz, a crash at 19.9 kHz, above most people's hearing.

In a column of bare integers there is nothing to notice: `220` looks as
plausible as any other number until you divide by the accumulator width.

**Fix, and the durable half of it:** write the table as `HzToInc(62)` rather
than `220`, so it reads and reviews as frequencies. Also: 12 bits gives an
11.7 Hz step, which is hopeless at the bottom of a kit (a kick wants ~55 Hz and
the nearest available are 47 and 59). **18 bits** gives 0.18 Hz.

### Peak amplitude is not loudness

Two separate corrections, both reported as "too loud" despite numbers that
looked balanced:

1. **Equal energy is not equal loudness once pitches differ.** The ear is ~24 dB
   more sensitive at 800 Hz than at 55 Hz. A cowbell — a pure tone in that band
   — was the loudest thing in the kit while measuring the same as the kick.
2. **A long voice needs to be quieter still**, because the ear integrates over
   roughly a tenth of a second. The crash survived *three* rounds of "make it
   quieter" because every target was set against an energy metric far too
   generous for a bright, long voice.

**Fix:** set levels from an A-weighted model with a duration term. Rule of
thumb now in `drums.cpp`: an octave higher wants about half the level; twice as
long wants about two thirds.

### Voice allocation: never round-robin blindly

Six slots taken in rotation, regardless of whether a voice was still sounding.
A crash rings ~900 ms — seven sixteenths at 120 bpm — so any six subsequent hits
silenced it. Reported as "recording silences previously recorded stuff", and it
got worse with more overdub passes because more passes meant more hits.

Modelled: 1 pass cut nothing off, 3 passes cut 23 voices, 8 passes cut 46.

**Fix:** more slots (16), and steal the **quietest** rather than the next. A
voice near the end of its decay contributes almost nothing, so cutting it is
inaudible; round robin cut the freshest about as often as not.

### Polling a one-sample flag from a divided tick

`PulseIn1RisingEdge()` is true for exactly **one 48 kHz sample**. It was polled
from the 3 kHz control tick, so it registered only when the edge happened to
land on the 1-in-16 sample the tick ran on — **about 6% of pulses**. The
external clock could essentially never lock, and the KEYS retrigger had been
silently dropping the same 94% without anyone noticing.

**Fix:** latch the edge at audio rate, consume the flag at control rate.

### LED blink rates must be computed, not guessed

`(timer >> shift) & 1` where the timer ticks at 3 kHz: shift 5 is **47 Hz**,
far above flicker fusion. It reads as a dim steady glow, not a blink. Every
calibration alert was written at shift 4, 5 or 6 and was therefore *invisible* —
reported from the bench as "the alerts aren't happening", including with nothing
patched in, which should collide on every step.

The tell: the one animation that visibly worked was the only one at `>>7`.

**Fix:** named constants with the arithmetic written down. `kBlinkFast` = 12 Hz,
`kBlinkSlow` = 6 Hz. Anything faster than ~12 Hz at this tick rate is not a
blink.

### Two alerts that look the same are one alert

"Voltage still moving" and "these two levels collide" shared a LED pattern and
differed only in duration — which is no difference at all to look at. They call
for opposite reactions: *hold steadier* versus *move the Four Voltages knob*.

**Fix:** button LEDs flutter = your hand; marker LEDs blink = the card.

### A warning that fires on things that work is worse than no warning

`kCollisionMin` started at 64, a number picked before any hardware existed. The
detector's real limit comes from `kSettleTol`: two levels are genuinely
ambiguous only below about 48. It took three passes — 64, then 40, then **32** —
to stop it flagging combinations that played perfectly.

**The general lesson:** tune warning thresholds against the bench, not the
theory. The theory tells you where it breaks; only playing it tells you whether
the warning is useful or merely correct.

---

## 3. Platform rules that are not negotiable

From `CLAUDE.md`, repeated here because they cost real time to rediscover:

- **`ProcessSample()` is a DMA interrupt at 48 kHz.** Allocation-free, no
  `malloc`, no blocking, **no float**. Fixed point only.
- **A control-rate divider buys throughput, not slack.** On the sample where
  the control tick fires, everything must still finish inside that one 20.83 µs
  slot. Never quote an amortised cycles/sample figure as headroom.
- **Never do hardware setup in the `ComputerCard` constructor** — it wedges the
  chip. Setup goes in `main()`.
- **`PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`** is required for the Workshop
  crystal. Without it the card fails to boot cold but works from a warm reset,
  which is exactly the kind of bug that wastes a day.
- **The switch reads Down for the first few ms of every boot** (ComputerCard
  derives it from `knobs[3]`, off a filter starting at zero, and zero decodes as
  Down). Latch any alt-boot from **one** reading after a settle window — never
  "Down seen at any point". Both sibling cards shipped that bug.
- **`CVOutMillivolts()` / `CVOutMIDINote()` are flash-resident.** Cache the last
  value and only call on a change, or they put XIP reads in the hot loop.
- **`hardware_flash` must be linked** even if you store nothing — `ComputerCard.h`
  needs it to read the factory CV calibration.
- Build clean with `-Wall -Wextra -Wdouble-promotion -Wfloat-conversion`.

### Pitch, if the new card has any

- Convert with `(semis * kMvPerSemiQ8 + 128) >> 8`. **The `+128` rounds**;
  truncating is biased the same way at every octave and lands every C a full
  millivolt flat, which is 1.2 cents and audible against a tuned oscillator.
- **Put the root at 0 V.** NIBBLE's was at MIDI 36, i.e. 3 V, which read as an
  octave jump when tuning oscillators together *and* silently clipped the top of
  wide scales — the widest case asked for 7.3 V against a ~6 V output.
- Size any transpose cap against the genuine worst case, and check it across
  every scale rather than reasoning about it. The voice that reaches highest was
  the *harmony*, not the played note.

---

## 4. For NibbleDrumMachine specifically

### The sample pipeline already exists

`../WorkshopBio` has a complete, working one. Take it rather than writing
another:

| File | Does |
|---|---|
| `tools/importwav.py` | WAV → 8-bit signed mono 48 kHz `.raw`, trimmed, normalised, faded. Standard library only, no ffmpeg. |
| `tools/mksamples.py` | Globs `samples/*.raw` → one `samples.h` with per-variant offsets |
| `tools/checksize.cmake` | POST_BUILD guard that fails if the image reaches the flash boundary |
| `samples_default.h` | `__has_include` shim so the build works **with or without** the samples present |

The CMake wiring is in `WorkshopBio/CMakeLists.txt` — note the
`set_source_files_properties(... OBJECT_DEPENDS samples.h)`, without which
editing a sample does not relink.

### If you let users upload samples, read this first

`WorkshopBio/webui.cpp` is the reference and the comments are hard-won.
Writing flash while ComputerCard runs **will hang the card** unless you do all
of it:

1. Raise a flag and **wait for core 0 to acknowledge it has parked in RAM** —
   `ComputerCard`'s callback lives in flash and can be executing. Raise the flag
   *before* masking anything, or the handler never runs to see it.
2. Disable **three** interrupts: `DMA_IRQ_0`, `PWM_IRQ_WRAP` (a second
   flash-resident handler) and `USBCTRL_IRQ` (TinyUSB's is in flash too).
3. Prime the SDK's boot2 RAM copy with a zero-length
   `flash_range_erase(offset, 0)` while XIP is definitely still up.
4. Wrap the actual writes in `__not_in_flash_func` with interrupts saved.
5. **Buffer the whole upload in RAM first** — nothing can talk to the host after
   step 2.

Put the user region at a **fixed offset**, not after the code, so reflashing
firmware does not move or wipe it.

**There is no "small" flash write that can skip step 2.** NIBBLE-KO tried
exactly this: save-kit/rename/delete touch only the 4KB header sector and no
audio, so it seemed obvious that USB could stay up and the card could carry on
playing instead of rebooting. The justification written in the comment was
"the USB handler is not reached during the erase, because `tud_task()` is not
called until XIP is back".

That is false. `USBCTRL_IRQ` is an **interrupt** — it fires when the host
sends a packet, which is not something the firmware schedules. TinyUSB's
handler is in flash, so a packet arriving mid-erase is a hard fault. On the
bench it read as "saving the kit hung the Workshop Computer", and because the
erase had completed but the program had not, the directory was left blank:
the audio was still in flash, unreferenced, so the library came back empty
while a slot still named an entry in it.

The tell is that the reasoning was about *when the code calls something*,
when the hazard is *when the hardware interrupts it*. If a justification for
skipping one of these five steps talks only about control flow, it is wrong.

**The same mistake, in the other direction: never call `tud_task()` from
inside a SysEx handler.** `HandleSysex()` runs *inside* `Task()`'s packet
loop, so a nested `tud_task()` re-enters that loop and corrupts `rx_`/`rxLen_`
half way through parsing the message being replied to. `Send()`'s comment
says exactly this — and NIBBLE-KO then added a `tud_task()` to the middle of
its library-listing handler anyway, justified with "this is a burst of
replies with no incoming message being parsed around it". There is one: the
request that reached the handler.

On the bench that read as an upload landing its audio (space consumed) and
then reporting an **empty library** — the corrupted parse lost the reply
stream. It looks like a flash bug and is not.

Any reply too big for the 256-byte TX FIFO therefore has to be **deferred to
`Task()`**, sent a piece at a time with a real `tud_task()` between pieces,
rather than pumped from the handler. See `SendNextLibraryEntry()`.

Once `USBCTRL_IRQ` is masked, TinyUSB's state is inconsistent anyway — the
host has seen the device stop responding mid-transfer — so **rebooting is the
honest recovery, not a convenience**. Every flash write on this card ends in
`watchdog_reboot()`, and the browser is told to expect the disconnect and
treat it as success.

### Decisions worth reconsidering, not inheriting

NIBBLE chose **synthesised** voices deliberately, and the reasoning does not
carry over to a sample-based card:

- It kept the Y knob as one multiply on live variables. With samples that same
  gesture becomes a resampling problem, and pitch and decay stop being
  independent. **Decide early what Y does.**
- It avoided the whole upload apparatus. If you want uploads, you are taking on
  all of §4 above — budget for it rather than discovering it.
- The synthesised kit's honest weakness is that hats and cymbals never sound as
  good as recordings. That is precisely what a sample-based card fixes, so this
  is the right reason to build one.

### Things NIBBLE settled that are probably still right

- **The loop stores the VOICE, not the gesture.** A pattern is a list of sounds;
  how each was played belongs to the performance. It also means re-arranging the
  gesture map cannot silently change what an old loop plays.
- **Singles are shifts, not sounds** — see the shift trick above.
- **Calibration is not persisted.** The Four Voltages knob invalidates it, and a
  card that looks calibrated while playing wrong notes is worse than one that
  admits it knows nothing. Start calibration automatically at power-on.
- **Both audio outs carry the same mono bus.** Patch either, get the whole kit.
- **Single core.** The whole per-sample load is a few hundred cycles against a
  budget of 4000 at 192 MHz. Only TinyUSB ever justified a second core
  (~36000 cycles) — so if you add uploads, that changes.

### Setup advice that took a hardware session to learn

**Four Voltages output 1, knob at about twelve o'clock.** That gives ten
well-spread, reliably separable voltages. The knob moves all four outputs at
once and towards either extreme several combinations collapse together. If one
output is stubborn, try another — they respond differently to the same buttons.

---

## 5. How to work on this

The thing that made the difference, more than any single technique:

**Model the risky logic in Python first, then port it.** Not as a test
afterwards — as the way of getting it right. `ghostsim.py`, `loopsim.py` and
`dspsim.py` between them caught the slew stall, the 0.33× soft clip, the looper
sorting by the wrong key, the double-fire, and the automation starvation. None
of those were visible in the C++ and several would have been hard to diagnose by
ear.

Two habits worth copying:

- **When a test fails, ask whether the test is wrong.** The DSP monotonicity
  check failed three times against a filter that was correct — measuring at
  1 kHz caught the resonant peak, at 50 Hz caught nothing, and a sparse grid let
  the peak sweep between sample points. "Test failed, change the code" would
  have made the card worse three times running.
- **Assert the round trip, not the mechanism.** The test that caught the most
  was not "did it capture ten numbers" but "learn ten levels, then replay all
  ten and check each classifies back to itself".

And one about documentation: **generate tables from the source**
(`kittable.py`). Writing the frequency column that way is what exposed the
octave-and-a-half tuning error. A number nobody can check is a number that
drifts.
