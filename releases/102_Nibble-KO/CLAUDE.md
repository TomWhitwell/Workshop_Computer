# NIBBLE-KO — working notes for Claude Code

A program card for the **Music Thing Modular Workshop System Computer**
(RP2040), built on the header-only **ComputerCard** library. Sibling project
to `../WoskshopButtons` (NIBBLE, repo `WorkshopNibble`), `../WorkshopBio` and
`../WorkshopZX` — reuse their conventions and structure where they fit.

**NIBBLE-KO is the expanded percussion half of NIBBLE**: a twelve-voice drum
machine reading one output of the Workshop System's Four Voltages module,
where every voice is independently synthesised or sample-based, chosen and
uploaded from a browser WebUI — the sample-management pattern from
`../WorkshopBio`.

## Current status: RELEASED, v1.2.0

Builds to `build/nibbleko.uf2` — **7.78% flash, 87.64% RAM**. Everything is
played and confirmed on a Workshop Computer:
drums, the four-bar looper with lossless overdub, three mute groups, twelve
performance effects with two-lane recording, three pattern slots, sample
playback, the browser sample manager (connect, assign, upload, rename,
delete, erase), flash-saved calibration, and the whole CV expansion
including the CV Out 2 → Pulse In 2 self-patch.

**Shipped.** `info.yaml` is `draft: false`, `Status: Released`. The card is
in the community catalogue as `releases/102_Nibble-KO` in
`TomWhitwell/Workshop_Computer` — merged there at 1.0.0 (PR #362), with
1.0.1 following in PR #365. Own repo: `uglifruit/Nibble-KO`.

**The RAM figure is the WebUI's 160KB upload staging buffer**, not a leak.
It only fits because USB is modal — nothing instantiates `WebUI` until
switch+B+D. Same figure WorkshopBio ships on identical hardware. Watch
`--print-memory-usage`.

Note that entering the WebUI **no longer stops the loop**. It used to, and
that was wrong for the job the WebUI mostly does: re-pointing a voice at a
different sound is a judgement made BY EAR, against the pattern the sound
has to sit in, and `MSG_SET_SOURCE` is instant and flash-free precisely so
the change is audible immediately. Only RECORDING stops on entry. An UPLOAD
still silences and parks the card, but through `EnterUploadMode()` /
`core0Parked` — the real mechanism — rather than through the mode change.

**Pattern slots are RAM-only on the card, by design** — they die at power-off,
and the browser is where they persist: the Patterns tab saves a slot to a JSON
file on the PC and loads one back over SysEx (`MSG_PAT_GET`/`MSG_PAT_SET`).
Confirmed on hardware, including on dense patterns, which is where it first
failed — the dump now drains in one `Task()` pass because core 1 gets very
little time while core 0 is busy.

Note that **a reboot clears the slots**, and every flash write reboots — so
leaving the WebUI, saving the kit or uploading a sample all lose unsaved
patterns. The Patterns tab says so; it is the one sharp edge in the feature.

**Loop length is settable, 4-16 beats** (`MSG_LOOP_GET`/`MSG_LOOP_SET`),
which is what makes odd metres playable. It is a PLAYBACK setting: events are
always recorded against the full sixteen beats and only the wrap point moves,
so shortening hides rather than erases. See `Looper::SetLoopBeats`.

**Calibration is flash-persisted** (`calibstore.h`) — a normal boot loads the
last saved levels and is playable within the splash; only an alt-boot (switch
Down through the boot-settle window) forces a fresh learn, and a successful
learn is saved automatically.

## Build

Toolchain comes from the Pico VS Code extension install at `~/.pico-sdk/`.
`CMakeLists.txt` includes `~/.pico-sdk/cmake/pico-vscode.cmake`, which pins
SDK 2.2.0 / GCC 14_2_Rel1 / picotool 2.2.0-a4.

From PowerShell:

```powershell
$env:PICO_SDK_PATH = "$env:USERPROFILE\.pico-sdk\sdk\2.2.0"
$env:PATH = "$env:USERPROFILE\.pico-sdk\cmake\v3.31.5\bin;$env:USERPROFILE\.pico-sdk\ninja\v1.12.1;$env:USERPROFILE\.pico-sdk\toolchain\14_2_Rel1\bin;$env:PATH"
cmake -B build -G Ninja
cmake --build build
```

Output: `build/nibbleko.uf2`. Copy to `FLASHME/` for flashing (git-ignored).
`cmake`/`ninja` are **not** on the default PATH — always set it as above.

## Hard rules

Identical platform constraints to NIBBLE and WorkshopBio — see
`docs/LESSONS.md` §3 for the full list with the reasoning. The short version:

- `ProcessSample()` runs at **48 kHz** on core 0, inside a DMA interrupt.
  Allocation-free, no `malloc`, no blocking, no `float` in the hot path —
  fixed-point only.
- Audio/CV I/O is signed 12-bit (`-2048..2047`). `KnobVal()` is unsigned 12-bit
  (`0..4095`).
- **Never** do hardware setup in the `ComputerCard` constructor — it wedges the
  chip. Setup goes in `main()`.
- `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64` is required for the Workshop
  Computer's crystal. Don't remove it.
- The switch reads **Down for the first few milliseconds of every boot**
  (ComputerCard derives it from `knobs[3]`, off a ~60 Hz filter starting at
  zero, and zero decodes as Down). Latch any alt-boot from **one** reading
  after a settle window — never "Down seen at any point". Three sibling cards
  have shipped that bug between them.
- `CVOutMillivolts()` / `CVOutMIDINote()` are **flash-resident**. Cache the
  last value and only call them on a change, or they put XIP reads in the hot
  loop.
- Writing flash while ComputerCard runs **will hang the card** unless the
  five-step protocol in `docs/LESSONS.md` is followed. Two places do it:
  `calibstore.h` (no USB in that path, so it needs fewer steps — its header
  says which) and `webui.cpp`'s `EnterUploadMode()`/`WriteStagedBuffer()`
  (all five). Read `docs/LESSONS.md` before touching either.
- **Every flash write with USB up ends in a reboot. There is no exception for
  small writes.** `USBCTRL_IRQ` must be masked, and once it is, TinyUSB
  cannot be resumed. An attempt to keep USB alive across a header-only write
  hung the card and left the sample directory half erased — the story is in
  `docs/LESSONS.md` and `webui.cpp`'s `CommitHeaderAndReboot()`. If you find
  yourself reasoning "the handler is not reached during the erase", stop:
  interrupts are not control flow.
- **Never call `tud_task()` from inside a SysEx handler.** `HandleSysex()`
  runs inside `Task()`'s packet loop, so a nested call re-enters it and
  corrupts the parser mid-message. A reply too big for the 256-byte TX FIFO
  must be deferred to `Task()` and drip-fed instead — see
  `SendNextLibraryEntry()`. Doing this wrong made an upload land its audio
  and then report an empty library, which looks like a flash bug and is not.
- **USB runs on core 1 and is MODAL.** TinyUSB is not initialised, and the
  card does not enumerate, until switch+B+D sets `WebUI::usbMode` — which is
  what keeps `USBCTRL_IRQ` (flash-resident) off the audio path while the card
  is being played. Core 1 is launched from `main()`, never the constructor.

## What's here

Ported and adapted from `../WoskshopButtons` (NIBBLE) and `../WorkshopBio`
(BioMimicry), namespace `nko` throughout (NIBBLE's was `nib`):

| Path | Status | Source |
|------|--------|--------|
| `nibbleko.h` | Ported, trimmed | NIBBLE's `nibble.h`, minus `BootMode`/scale vocabulary (KEYS-only) |
| `levels.h/.cpp` | Ported unchanged | NIBBLE's level detector + **the ghost rule** + `Shift()` |
| `looper.h/.cpp` | Ported unchanged | NIBBLE's event looper |
| `drums.h/.cpp` | Ported + PCM backend | NIBBLE's synth engine, plus sample playback and the D-bank transport effects |
| `fastmath.h/.cpp` | Ported unchanged (namespace only) | NIBBLE's fixed-point helpers |
| `samples_default.h` | Written new | The baked sample BANK plus `kVoiceSample`, the voice→bank mapping |
| `samplestore.h` | Written new, wired in | `ResolveSample()` is called per hit from `DrumKit::TriggerVoice` |
| `calibstore.h` | Written new, wired in | Flash-persisted calibration, sibling layout to `samplestore.h`; `SaveCalibration()`/`LoadSavedCalibration()` called from `main.cpp`'s `LearnTick()` and boot splash |
| `webui.h` | Written new | WorkshopBio's message set on 12 flat slots + `MSG_SET_SOURCE` |
| `webui.cpp` | Ported, tested on hardware | WorkshopBio's, with mode×variant collapsed to flat voices |
| `tusb_config.h`, `usb_descriptors.c` | Ported | WorkshopBio's, byte-identical but for the product string |
| `web/index.html` | Written new | Mockup's visual shell + WorkshopBio's connection/upload logic |
| `tools/importbank.py`, `mksamples.py` | Written new | WAV → numbered bank entries; `importwav.py` supplies the DSP |
| `fx.h/.cpp` | Written new | Twelve performance effects, four slots chained in series |
| `tools/checksize.cmake`, `tools/bin2h.py` | Ported unchanged | WorkshopBio |
| `tools/ghostsim.py`, `loopsim.py`, `dspsim.py`, `syntax.sh`, `checkyaml.py`, `kittable.py`, `crosscheck.py` | Ported unchanged | NIBBLE — all pass against the ported `.cpp` files (see Verifying changes) |
| `tools/patsim.py` | Written new | Pattern transfer across the `webui.cpp` ↔ `web/index.html` seam, plus `SetPatternRaw()`'s trust boundary |
| `ComputerCard.h`, `pico_sdk_import.cmake` | Vendored, byte-identical | NIBBLE — **do not edit** |
| `main.cpp` | Written new | The mode machine, Drum Performance, LEDs, calibration. Structure follows NIBBLE's `main.cpp` closely |
| `CMakeLists.txt` | Written new | Builds, with TinyUSB + `pico_multicore` and the `checksize` guard |
| `info.yaml` | Written new | `draft: false`, `Status: Released`, v1.0.1 |

## The control surface

The switch is a **mode selector**, not a trigger — this is the single biggest
departure from NIBBLE, and `main.cpp`'s file header explains it in full.
Briefly: hold the switch **Down** and press a button to choose a mode, which
**latches** on release; then **Middle** plays that mode and **Up** plays and
records it. Latching is what lets every mode be recorded through one
mechanism, since Down and Up never need to be held at once.

    switch+A  DRUMS (default)    switch+AB  UNDO
    switch+B  MUTE               switch+AC  QUANTISE (cycle record grid)
    switch+C  FX  (twelve)       switch+CD  PLAY/STOP
    switch+D  PATTERN (three)    switch+BD  WebUI setup

**Singles commit on RELEASE; pairs fire IMMEDIATELY.** That asymmetry is not
an oversight and should not be "fixed" — both halves fall out of the ghost
rule. Four Voltages holds its last voltage, so the CV may already be sitting
on the single you are trying to select, in which case pressing it produces no
transition and a press-driven select would never fire. Reading the *state* on
release is immune to that. A pair cannot get stuck the same way, because
releasing a pair falls back onto one of its members and `levels.cpp` sets
`current_` to that **single** — so the resting state is never a pair, and a
pair press is always a genuine transition. See `main.cpp`'s header.

## Gotchas — the ones that have already bitten

Every one of these was found on hardware, and most were invisible in the
code. They are here because the same shapes will recur.

### FOUR VOLTAGES LATCHES. Almost everything traces back to this.

The output holds the last-pressed level indefinitely. There is no rest
voltage, and a "held" button is indistinguishable from one pressed and
released. Three separate bugs came from forgetting it:

- **A hold-to-store gesture cannot work.** Any "held for N ticks" test passes
  eventually, because the level just sits there. The pattern store was
  written this way and every recall became a store. The switch decides the
  verb instead.
- **A press-driven mode select never fires** when the CV is already on the
  button you want. Hence commit-on-release for singles.
- **A bare-press toggle can turn something on and never off**, because
  pressing the same button twice needs a transition that never happens.
  Hence shift-and-tap for mutes.
- **A latched single is NOT evidence that a finger is on the button**, and
  anything that treats it as intent will misfire once the hand has gone.
  `FxUpdate()` assigns `fxParShift_` from a bare `Current()`, which is
  correct — but `FxSlotDepth()` then read the LIVE Main knob for that slot on
  that basis alone, so one slot permanently ignored its recorded depth curve
  and followed the knob's resting position instead. It presented as "FX depth
  records under shift C but not under B": C is mode C's own button, so the
  latch usually rested there, and what sounded like C working was the live
  knob being heard rather than the recording.

  The fix is the rule the recording side already followed — **the hand only
  wins while it is actually MOVING the knob** (`AutoKnob::HandOwns()`). A
  latched voltage plus a still knob means the recording drives. Any future
  "the player is doing X because the CV says so" needs the same test.

### Ordering inside ControlTick is load-bearing

`FireCombo()` runs BEFORE `PlayControl()`, so anything `PlayControl()`
assigns is one tick stale when a button handler reads it. That is harmless
for values that only play, and destructive for values that decide whether to
*write*: `PatternPress` read `recording_` this way and a stale-true reading
silently overwrote a pattern slot. **Read `SwitchVal()` directly** in a
button handler rather than the cached flag.

### Reverse and TapeStop are no longer the same shape

They were grouped as "the transport effects, applied at trigger time" — and
that was right for Reverse, which has to know where a recording ENDS before
it can play backwards from it, so it can only ever be set as a voice starts.

TapeStop had no such constraint. `SetTapeStop()` just installs a Q16 ramp, so
applying it to a voice already ringing is legal and cheap — and a real tape
stop grabs whatever is spinning. Applied only at trigger time, holding D+B
during playback did nothing audible between recorded hits, which is exactly
how it was reported from the bench.

It now does both: `DrumKit::TapeStopAll()` brakes what is sounding, fired on
the effect's RISING EDGE from `PlayControl()`, while `TriggerVoice()` still
catches hits that start during the hold. **Edge, not level** — calling it
every tick reinstalls the ramp at full scale and the kit never stops.

### `Current()` versus `Sounding()` is not a style choice

`Sounding()` reports the pair for as long as the ghost is armed — i.e. after
the tapping finger has lifted. Right for a DRUM, whose hit is still ringing.
Wrong for anything momentary: FX read `Sounding()` at first and effects
stayed latched after release, and re-tapping the same button produced no
change at all, so one whole bank appeared not to work.

### Replacing the event array invalidates the cursor

`cursor_` is an index into `events_`. Undo and pattern recall both replace
that array wholesale, so it must be **rebuilt from the playhead**. Resetting
it to zero looks safe and makes `Fire()` re-fire everything between the loop
start and the current position, all on one tick.

**And the rebuild has two ends, not one.** Walking forward from the playhead
is only half of it: if the new array is SHORTER than the playhead's position
— undoing a pass whose hits were all behind it, or recalling a sparser
pattern mid-bar — the walk runs off the end and leaves `cursor_ == count_`.
That is not a harmless resting place. `Fire()` only ever moves the cursor
forward, and `Advance()` only rewinds it when `playHead_` reaches 0, so the
loop plays NOTHING for the rest of the pass. It shipped that way and came
back from the bench as "undo sometimes silences the loop til start of next
pass" — intermittent-sounding, because it only bites when the undone pass
was behind the playhead.

Both paths now go through `Looper::RebuildCursor()`, which wraps to 0 on
running out. Wrapping is correct rather than merely non-silent: the array is
a RING sorted by fire time, so "nothing left this pass" and "the next event
is the first one, next pass" are the same statement. `tools/loopsim.py`
covers both ends — `test_undo_does_not_replay_the_bar_so_far` for the
forward walk, `test_undo_past_every_surviving_event_does_not_go_silent` and
`test_recall_a_sparser_pattern_does_not_go_silent` for the wrap.

### The Python models are not decoration

They have caught, before hardware: a stalling slew, a 0.33x soft clip, a
looper sorting by the wrong key, a double-fire, and a lane-encoding drift
where `loopsim` stored raw knob values while `looper.cpp` stored `knob >> 4`
— invisible until the FX lane packed data into all eight bits. **If you
change `levels.cpp`, `looper.cpp` or `drums.cpp`, change the model too**, or
delete it rather than let it lie.



### The control tick has no divide instruction, and no flash either

Two things bite anything added to `ControlTick()`/`PlayControl()`, both found
by reading the disassembly rather than by timing:

- **Cortex-M0+ has no divide.** Every `/` and `%` on a runtime value is a call
  into `__aeabi_idiv`/`ldivmod` at 100+ cycles, and a 64-bit divide is worse.
  `SamplesPerBeat()` is cached for exactly this reason (`RecacheTempo()` — any
  new writer of `tickInc_` must call it), and `GlitchTick()` uses lookup
  tables and reciprocal multiplies instead of the obvious arithmetic.
- **`__not_in_flash_func` does not propagate to callees.** A RAM-resident
  function calling a flash-resident one still stalls on XIP. The control-rate
  path — `Looper::Advance/Fire/RecordHit/RecordKnobs`, `DrumKit::TriggerVoice`,
  `LevelTracker::Match`, `FxRack::SetSlot` — is annotated for that reason.
  Check the map, not the annotation on the entry point:

```sh
grep -oE "^[0-9a-f]{8} <_ZN3nko[^>]+>:" build/nibbleko.dis | sort
```

`0x2000…` is RAM, `0x1000…` is flash. What may stay in flash is genuinely
cold: the WebUI, calibration setup, and one-shot gestures.

A third trap is in the compiler rather than the chip: `RecordLane`'s scan
reloaded `playHead_` and `loopTicks_` from memory on EVERY iteration, because
nothing proved the loop left them alone. Hoisting them into locals turned the
body into register work. If a hot loop reads members, check what it compiled
to before assuming it is cheap.

### Units: loop ticks are not control ticks

Eight loop ticks is 42ms at 240bpm and 250ms at 40bpm — 125 versus 750
control ticks. Anything measured in one and counted in the other is wrong
across most of the tempo range. The FX playback hold was written in control
ticks first and expired instantly at slow tempos.

## What's NOT here — the actual next work

Roughly in dependency order:

1. **Patterns are saved to the PC, not to the card.** The three slots are
   still RAM only and still die at power-off — deliberately. Persistence is
   the browser's job: the Patterns tab saves a slot as a JSON file and loads
   one back (`MSG_PAT_GET`/`MSG_PAT_DATA`/`MSG_PAT_SET`), so the file on
   disk is the permanent copy. No flash write is involved anywhere in that
   path, which is why it needs none of `docs/LESSONS.md`'s five-step
   protocol and never reboots the card — the same "instant, RAM only" shape
   as `MSG_SET_SOURCE`.

   A pattern carries no audio (`what` is a voice index), so one saved
   against any kit plays against any other. **Not yet exercised on
   hardware** — the codec round-trip and the wire framing are verified in
   software only.

   An on-card flash store was considered and rejected: it would duplicate
   the persistence the file already provides, and cost a flash region plus
   a write path for it.

2. **The WebUI covers the KIT, SAMPLES and PATTERNS.** Connect, assignment
   (`MSG_SET_SOURCE`, instant), saving the kit (`MSG_SAVE_MAP`), the whole
   sample library — upload, name, delete, erase — and pattern save/load all
   work. What is still missing needs NEW SysEx messages, none of which exist:
   - **mute-group assignment** — `MuteGroupOf()` is still a compile-time
     function, see item 4
   - **loop setup** (bars, swing, quantise grid) — card-side only

   The Mutes and FX tabs, and everything on the Patterns tab below the
   save/load panel, are reference displays that say so rather than controls
   that quietly do nothing.

3. **Deleting a sample frees the SLOT, not the space.** Uploads append, and
   nothing compacts the region — only `MSG_ERASE` reclaims bytes. The
   Samples tab reports both numbers (live audio vs append watermark) rather
   than letting the difference look like a bug. A compaction pass would mean
   rewriting the whole region with audio moving under live offsets, which is
   a much bigger job than it looks.

4. **Loop LENGTH is still fixed at four bars.** `kLoopTicks` is only ever
   used in ordinary wrap arithmetic in `looper.cpp` — nothing is sized by it
   — so making it runtime-settable (2 bars, 1 bar) is a small change, and a
   natural WebUI setting. The quantise grid has already made this move; see
   `QuantGrid` for the shape.

5. **Mute groups are hardcoded.** `MuteGroupOf()` splits the kit three ways
   by voice index so the mode is testable; the real mapping belongs in the
   WebUI alongside sample assignment.

   Note that mutes NOT being recorded is a decision, not a gap — see the
   note in `MutePress`. A mute is a mixer move rather than part of the
   music, so it stays card state, persists across pattern recalls, and is
   outside Undo. Do not "finish" it.

6. **The published Map artifact's layout was never fixed.** Asked for on the
   bench and deliberately deferred: lay the ABCD gesture pads out as a 2×2
   grid and the LEDs as 2×3 (matching `nibbleko.h`'s stated physical layout,
   `0 1 / 2 3 / 4 5`), and set the type larger. This applies to the
   standalone **mockup artifact**, not `web/index.html` — the shipped tool
   already got the 2×2 pads and the +3px type. Cosmetic, no code depends on
   it.

## USB: what is proven, and what is not

**Confirmed on hardware, v2 library protocol:** entering WebUI mode
(switch+B+D), enumeration, `MSG_HELLO`/`MSG_INFO`, live re-assignment to any
source (synth/baked/user), `MSG_SAVE_MAP`, a WAV upload including the
five-step flash write, uploaded audio and the slot map both surviving a power
cycle, `MSG_LIBRARY`'s drip-fed listing, and `MSG_ERASE`.

Two real bugs were found this way and are not hypothetical:
`CommitHeaderLive()`'s attempt to keep USB up across a flash write hung the
card (reverted — see "every flash write reboots", above), and `MSG_LIBRARY`'s
first drip-feed attempt called `tud_task()` from inside `HandleSysex()` and
corrupted the reply stream, so an upload would land its audio and then report
an empty library. Both are written up in `docs/LESSONS.md`.

**Not yet exercised on the current protocol:** `MSG_NAME` and `MSG_DELETE`
(same commit path as `MSG_SAVE_MAP`, which is proven, but not separately
confirmed), a library at or near `kMaxUserSamples` (32 entries) — everything
tested so far has been a handful of samples — and the whole pattern-transfer
trio `MSG_PAT_GET`/`MSG_PAT_DATA`/`MSG_PAT_SET`.

Pattern transfer touches no flash, so it cannot hang the card the way an
upload can; what is unproven is the drip-fed reply under a real browser's
timing, which is exactly where `MSG_LIBRARY` went wrong the first time. The
codec and the chunk framing are verified in software (a 512-event round trip
through both implementations, high bits and the `kKnobEvent` flag included),
but software cannot tell you whether the burst survives WebMIDI.

**GET and SET share one buffer and one cursor**, so the two directions are
not independent: `patBuf_`/`patCursor_`/`patBytes_` mean "the transfer in
progress", whichever way it is going. A `MSG_PAT_SET` header therefore clears
`patSending_` — without it, a load started while a save was still draining
had `SendNextPatternChunk()` transmitting the ARRIVING pattern back out and
advancing the cursor underneath the receive path, corrupting both at once.
The browser serialises the two, so this only shows up on a retry or a second
tab: it is a defence, not a normal path, and it will not appear on a bench
test that saves and loads one at a time. Any new message reusing that buffer
needs the same guard.

**A v2 card cannot read a v1 upload.** `kUserMagic` changed, so
`HaveUserSamples()` answers false and the card falls back to baked samples —
which is the intended failure, not a bug.

## Known gaps in what IS written

Things that compile and look finished but are not, worth knowing before
trusting them:

- **Reverse on a synth voice is hard to judge.** The swell runs over roughly
  the length the decay would have taken, derived from each voice's decay
  shift — one expression in `SetReverse()`. Whether that ratio is musical
  across the kit is unsettled.
- **`kSelectMinTicks` (50ms) is a guess.** It is the debounce that stops a
  knock against the switch re-latching the mode.
- **Gate is not tempo-synced.** Its rate divides the sample clock, so it does
  not lock to the loop. Deliberate — a gate that slowed with the pattern is
  just tremolo — but worth revisiting.
- **The CV expansion is confirmed working on hardware** — CV In 2 override,
  Pulse In 2's random effect, and the CV Out 1/2 glitch gates (including the
  CV Out 2 → Pulse In 2 self-patch) have all been played, not just built.
  Two real bugs were found and fixed this way before this note was last
  updated: gate width was first a fixed millisecond figure (too short to
  hear the effect it triggered) and then sized against the beat (which
  overran the next candidate and held the output permanently high) — see
  `nibbleko.h`'s `kGateLongNum`/`kGateShortNum`/`kGateRatchetNum` for the
  fraction-of-the-division fix that actually worked.
- **The glitch probabilities are tuned on paper, not by ear.** The curve
  itself (5% unpatched, 75% ceiling, divisions widening at
  `kChaosDivisionOpens`, ratchets at `kChaosRatchetOpens`) was checked
  arithmetically but not tuned by listening — confirmed present and audible
  on hardware, not confirmed as the *right* feel. Treat the constants in
  `nibbleko.h` as a first draft.
- **An intermittent fault is recorded in `docs/OPEN-BUGS.md`**, seen once and
  never reproduced.

## Why the namespace is `nko`, not `nib`

Every ported file's `namespace nib { ... }` was renamed to `namespace nko`
and `#include "nibble.h"` to `#include "nibbleko.h"`. Nothing else changed in
`levels.h/.cpp`, `looper.h/.cpp`, or `fastmath.h/.cpp` — they're logically
identical to NIBBLE's, just renamed to avoid colliding if both projects are
ever built against each other or diffed side by side.

## Layout

| Path | Purpose |
|------|---------|
| `main.cpp` | Card entry, `ProcessSample()`, boot latch, the mode machine, switch gestures, LEDs, output routing |
| `nibbleko.h` | Shared constants, combo indices, rates, LED helpers |
| `levels.h/.cpp` | Level detection, settle/match, **the ghost rule**, `Shift()` |
| `drums.h/.cpp` | Twelve voices: synth engine (working), sample backend (TODO), DJ filter |
| `looper.h/.cpp` | Event loop: record, overdub, tempo, external clock |
| `samplestore.h` | Flash layout for user-uploaded samples, per voice slot (not wired in) |
| `calibstore.h` | Flash layout for the saved calibration levels — wired in |
| `samples_default.h` | `__has_include` shim so the build works with or without baked samples |
| `webui.h/.cpp` | USB-MIDI SysEx transport, upload state machine, pattern transfer — runs on core 1 |
| `tusb_config.h`, `usb_descriptors.c` | TinyUSB setup; the product string is what the browser matches on |
| `web/index.html` | Browser setup tool. Kit, Samples and Patterns save/load are live; other tabs are reference — see `web/README.md` |
| `fastmath.h/.cpp` | Fixed-point helpers, sine LUT, PRNG |
| `ComputerCard.h` | Vendored MTM library — **do not edit** |
| `tools/` | Python verification models, `syntax.sh`, `checkyaml.py`, sample pipeline |
| `info.yaml` | Workshop System card registry metadata (`draft: false`, released) |
| `docs/LESSONS.md` | NIBBLE's handover doc — **read this first**, most of the reasoning behind what's ported here lives there |

## Starting work on this card

Read **`docs/LESSONS.md`** first — it's NIBBLE's handover, written explicitly
anticipating this card ("NibbleDrumMachine": percussion only, with real
samples). §4 in particular ("For NibbleDrumMachine specifically") is the
closest thing to a design brief that exists: what to take wholesale, what
NIBBLE deliberately chose that should be *reconsidered* for a sample-based
card (not inherited blindly), and the exact five-step flash-write protocol
for uploads.

## Verifying changes

**There is no host C++ compiler on this machine.** Same two things fill the
gap as on NIBBLE:

```sh
sh tools/syntax.sh          # type-check every .cpp with the ARM compiler, ~1s
python tools/ghostsim.py    # the ghost rule + learn round-trip
python tools/dspsim.py      # DJ filter stability, soft clip
python tools/loopsim.py     # event ordering, overdub, tempo
python tools/patsim.py      # pattern transfer: wire framing + the trust boundary
python tools/checkyaml.py   # info.yaml parses AND is structurally complete
```

`patsim.py` is the one model that spans a language boundary rather than
mirroring a single `.cpp`. Pattern transfer is split between `webui.cpp` and
`web/index.html` with no compiler across the seam, so it checks the bytes one
puts on the wire against the offsets the other reads — and it reads the chunk
size out of BOTH source files, because a mismatch there is silent: each side
stays self-consistent, so a round trip within either one still passes. It was
written by mutation-testing the checks, which is what caught `webui.cpp`
claiming a chunk size was "the largest that fits" when the cap is not reached
until twice that.

All pass, and the card builds clean with `-Wall -Wextra -Wdouble-promotion
-Wfloat-conversion`. `tools/syntax.sh` does **not** link, so it cannot catch a
missing symbol — run a real `cmake --build` before believing anything. It also
missed the duplicate `kFlashBase` in `calibstore.h`/`samplestore.h`, because
that only became an error once one translation unit included both.

**There IS a node on this machine** (`/c/Program Files/nodejs/node`), so
`web/index.html`'s script can be genuinely parsed rather than eyeballed:
extract the `<script>` block to a `.js` and `node --check` it. Worth doing
after any edit there — it is otherwise the only code in the repo with no
compiler in front of it.

The Python models are **line-by-line ports** of the C++ they mirror. If you
change `levels.cpp`, `drums.cpp` or `looper.cpp`, change them too — or delete
them rather than let them drift into telling you a comfortable lie. Between
them they caught four real bugs on NIBBLE that would each have been hard to
diagnose by ear.

Nothing yet models `main.cpp`'s **mode machine** — the commit-on-release
select, the pair-fires-immediately path, the record-arm transition. That is
the obvious next thing to model, and the reasoning is the same one that
motivated `ghostsim.py`: it is ordering-sensitive logic where a wrong answer
is silent.

## Repo

`https://github.com/uglifruit/Nibble-KO` (public). Commit as
Andy Jenkinson (uglifruit).
