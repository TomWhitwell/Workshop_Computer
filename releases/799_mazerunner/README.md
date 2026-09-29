# Maze Runner

Generate a maze with one of four algorithms, then solve it **twice** -
independently, with two separately-chosen algorithms - to drive two full
voices at once. Path A (CV/Pulse Out 1) and Path B (CV/Pulse Out 2) each
turn their own solved route into music the same way: the length of each
straight run between turns sets how many clock steps pass before the
next note, every turn fires a gate, and reaching the exit ping-pongs
that path back the way it came - independently of the other path, so
the two voices drift in and out of phase with each other rather than
staying locked together. A shared internal clock (or an external one,
if patched) drives both, with Path B able to run at its own
multiplied/divided pace on top of that.

## ⚠️ Status: checked against the real ComputerCard.h, still unbuilt

The first draft of this card was written purely from the platform
directive's documentation. It's since been checked line-by-line against
the actual `ComputerCard.h` (v0.3.0), which turned up a few real
mismatches, now fixed:

- `KnobVal`, `SwitchVal`, `CVOut1MIDINote`, `PulseIn1RisingEdge`, and
  friends are all `protected`, not `public` — fine to call from inside
  `MazeRunnerCard`'s own methods (as this card does), but **not** from a
  free `main()` holding a plain instance.
- `UniqueCardID()` and `HardwareVersion()` are protected too. The
  original draft called both from `main()` to seed the RNG and check for
  Proto 1.2 boards — that would not have compiled. Fixed by doing the
  seeding inside `MazeRunnerCard`'s own constructor instead, and
  dropping the hardware-version check (it wasn't doing anything
  functional anyway).
- `CVOut1MIDINote`/`CVOut2MIDINote` take `uint8_t`, not `int` — added
  explicit casts rather than relying on an implicit narrowing
  conversion.
- `HardwareVersion_t` values are `{Proto1=0x2a, Proto2_Rev1=0x30,
  Rev1_1=0x0C, Unknown=0xFF}` — nothing in this card's directive
  write-up ("Proto 1.2 / Rev 1.0.0 / Rev 1.1") lines up numerically with
  that, so if you ever need hardware-version branching, compare against
  the named enum values (`ComputerCard::Proto1` etc.), not the numbers
  from the directive.
- Everything else checked clean: `Knob`/`Switch` enum names and values,
  `LedOn`/`LedBrightness` signatures, `PulseIn*RisingEdge`/`PulseOut*`,
  `CVIn1()`/`CVIn2()` returning `int16_t`, the LED grid numbering.
- **Sequencer timing fix (unrelated to the header check):** the original
  `Step()` had an off-by-one - a run of length N actually took N+1 clock
  pulses to fire its turn. Fixed so a run of length N takes exactly N
  pulses, and `MazeSequencer` now tracks an exact (row, col) position
  per step (needed for the USB visualizer below, and arguably more
  correct regardless).

`ComputerCard.h` itself is now vendored at `src/ComputerCard.h` (the
copy checked against) so the build doesn't depend on guessing this
repo's real relative path to the canonical copy — see the note at the
top of `CMakeLists.txt` if you'd rather point at a shared copy instead.

**What's still unverified:**

- It has **not** been built — no pico-sdk in the environment this was
  written in, so no compiler ever saw this code. Expect at least minor
  syntax/type issues on a real build.
- Timing has been reasoned about (see "Why generation runs on core1"
  below) but not measured on a scope.
- The knob-to-parameter mapping constants are first guesses, untuned by
  ear or by hand on real pots.

Please build it, read it end to end, and confirm on hardware before
treating this as a real release.

**Newest additions, also unbuilt:** Tremaux's, Pledge, and Random-walk
solvers (`maze_solve.h`), plus the exit-verification fallback that
covers all four non-Direct solvers. These are logic-only (no new
ComputerCard or TinyUSB API surface), so the risk is algorithmic
correctness rather than platform-API mismatches - reasoned through and
commented in `maze_solve.h`, but not run.

**Newer still, also unbuilt: dual-path mode.** CV Out 2 no longer plays
a harmony of Path A - it's now Path B, an entirely independent solve of
the same maze with its own algorithm, position, and ping-pong state.
This replaced the harmony feature outright rather than sitting alongside
it, which also freed the Y knob/CV2 in to become Path B's algorithm
select instead. The `MazeSequencer` class itself didn't need to change
for this (each instance was already self-contained - dual-path mode
just runs two of them); the real surface area was solving twice on
core1, a second double-buffered `PreparedPath`, and the SysEx protocol
gaining a `pathIndex` byte throughout so the visualizer can tell the two
apart. Logic-only again, same caveat as above.

**Newest, also unbuilt: selectable scales via the web app.** Each
path's quantization scale (Major, five other modes, two pentatonics,
blues, whole-tone, chromatic - see `scales.h`) is now chosen
independently from `web/index.html`, live, with no knob involved -
there wasn't one left. This is the first *browser-to-device* SysEx
traffic in the project (everything before was card-to-browser only), so
`sysex_io::PollIncoming()` in `main.cpp` was rewritten from a fixed-
pattern matcher into a proper small state machine that can parse a
message type plus a variable-length payload, not just one fixed 5-byte
frame. `MazeSequencer::DegreeToMidiNote()` also had to stop assuming a
hardcoded 7-note scale - see the comment there for the one genuinely
interesting consequence (denser scales cover less absolute pitch range
for the same scale-degree budget, which is intentional, not a bug).
Same unbuilt/logic-adjacent caveat as the rest of this list, plus the
usual "new SysEx message direction, never tested against a real USB
host" risk that applies to anything touching `usb_descriptors.cpp`.

**Newest, also unbuilt: full control-scheme overhaul.** This was a big
one - see "Controls" below for the resulting layout, and the header
comment in `main.cpp` for the fullest description. In outline:

- **Pulse In 2 (reset) no longer rebuilds anything** - it only restarts
  both paths' playback from their entry cell. `MazeSequencer` gained a
  `RestartPlayback()` (which `AdoptPrepared()` now also calls internally,
  so the two stayed in sync rather than duplicating logic).
- **A new one-shot "generate" trigger replaces the old momentary-press
  behaviour.** Switch-Down, quick press/release, fires exactly one
  regenerate using whatever's currently pending - it no longer cycles
  the generation algorithm (that's now a live knob, Up bank). This is
  the *only* thing that ever rebuilds the maze or re-solves the paths,
  besides power-on.
- **The switch is now a proper 3-bank selector.** Every position (Up/
  Middle/Down) exposes its own Main/X/Y meanings, held live the whole
  time the switch sits there - see the table below. This roughly triples
  the knob-controllable parameter count (9 slots instead of 3ish).
- **An internal clock** (40-400 BPM, Middle+Main) drives both paths
  whenever Pulse In 1 has nothing patched into it - jack presence
  (`Connected(Input::Pulse1)`, via the normalisation probe already
  enabled in the constructor), not signal timing, decides this: patch a
  cable in and internal clock generation goes silent unconditionally,
  whether or not that cable is actually carrying pulses.
- **Path B gets its own clock multiply/divide** (Down+Y, 21-way: x16
  x13 x11 x8 x7 x6 x5 x4 x3 x2 x1 /2 /3 /4 /5 /6 /7 /8 /11 /13 /16),
  independent of Path A's pace. Division is a simple
  tick-counter; multiplication has to subdivide the time *between* main
  clock ticks evenly, which needs to know how long that interval
  actually is - tracked via a running sample counter, recomputed every
  main tick regardless of whether that tick came from the internal
  clock or an external one, so it adapts immediately either way. See the
  period-tracking block in `ProcessSample` if you want to change how
  this behaves.
- **Scale is now a single value shared by both paths** (previously
  independent per path), settable from a knob (Middle+X) *or* the web
  app - whichever was touched more recently wins. The knob only writes
  the shared atomic when its own computed value actually changes (not
  every sample), so an idle knob can never fight a value the web app
  just set; a `SET_SCALE` message from the browser always writes
  unconditionally. `sysex_protocol.h`'s `SET_SCALE`/`SCALE_INFO` dropped
  their `pathIndex` byte accordingly.
- **A second maze-to-CV mapping mode**: alongside the original fixed
  +-1-scale-degree-per-turn behaviour ("Direct"), "Run-length-as-leap"
  makes a longer straight run before a turn produce a bigger melodic
  jump (capped at 7 degrees so one very long corridor doesn't just slam
  the pitch to its clamp). Shared by both paths (Middle+Y). See
  `MazeSequencer::LeapAmount()`.
- **While rewriting the braid-amount knob math to match the new shared
  helper functions, found and fixed a latent issue**: the old formula
  needed the knob *and* CV1 both near their maximum to ever reach 100%
  braid - the knob alone topped out around 50%. The new version lets the
  knob alone reach the full 0-100% range, with CV1 modulating around
  that. Not something you asked for, but worth knowing since it changes
  a control's feel from before this round of changes.

Same unbuilt caveat as everything else - this is a large, purely
logic-and-control-flow change (no new ComputerCard or TinyUSB surface),
reasoned through carefully but not run.

**Newest: three fixes from hands-on testing feedback.**

- **Scale changes on the module now reach the web app immediately.**
  Previously the browser only learned the current scale on connect or
  on the next regenerate - a knob-driven change sat invisible until you
  happened to tap Down. `Core1Entry`'s main loop now watches
  `scaleIndexAtomic` every iteration and broadcasts a `SCALE_INFO` the
  moment it changes, regardless of source (knob or web).
- **Every knob-controlled parameter now has real soft-takeover
  ("pickup"), not just scale.** This was a genuine, reproducible bug,
  not a rough edge: every physical knob is shared across all three
  switch banks, and the previous code read each one live, unconditionally,
  the instant its bank became active - so leaving a knob anywhere other
  than its "proper" spot while adjusting a *different* parameter on a
  *different* bank would silently overwrite whatever that knob now
  controlled the next time you switched back. A new `SoftTakeover`
  struct fixes this everywhere: each parameter starts (and re-arms,
  every time you leave and return to its bank) locked to its current
  value, ignoring the knob entirely until it's turned back to exactly
  that value - only then does it start tracking live. Scale gets one
  more trigger for this: it also re-arms whenever the web app changes
  it, so the knob has to "re-find" a web-set value too rather than
  instantly stomping it back.
- **Path B not following an external clock was traced to the same root
  cause**, not a separate clock-handling bug: Path B's clock ratio
  (Down+Y) had no protection at all, so it was very likely knocked away
  from its ×1 default the first time that knob was touched for any
  reason while adjusting braid or maze size on the same bank - and
  would have silently stayed there. With soft-takeover now covering it
  too, this should no longer drift on its own. **This is the one fix in
  this round I'd most like confirmed on hardware** - I'm confident in
  the mechanism, but I never observed the actual symptom myself, so if
  it persists after this update, it's something else and I'd want to
  know exactly what you're seeing (does CV Out 2 freeze entirely, or
  does Path B still move but at the wrong rate?) to keep looking. While
  in that code, also fixed a smaller, related issue: the period estimate
  used for Path B's *multiply* ratios now gets discarded (not silently
  reused across the transition) whenever the clock source itself
  switches between internal and external.

**Down-tap vs. Down-hold weren't actually distinguished.**
Regenerate used to fire the instant Down was *pressed* - which meant
every hold-to-adjust-maze-size-or-braid gesture fired one too, right at
the start of the hold, before you'd even had a chance to turn a knob.
It now fires on **release**, and only if the total time held was under
~300ms (`kTapMaxSamples` in `main.cpp`) - a genuine quick tap still
fires exactly one regenerate; holding Down for longer to adjust its
bank's knobs fires nothing at all when you let go.

**Newest: the web app shows current vs. pending, and everything live
updates immediately.** Two related changes, both from hands-on
feedback:

- Tempo, mapping mode, and Path B's clock ratio now broadcast the
  instant they change, the same way scale already did - previously
  they only reached the browser on the next regenerate or reconnect.
- The visualizer now distinguishes **current** (what's actually baked
  into the maze/paths playing right now) from **pending** (the live
  knob preview of what current becomes on the next Down-tap) for
  Generator, Solver A, Solver B, Maze size, and Braid - shown side by
  side in a settings panel, with the pending value only appearing
  (highlighted yellow) once it actually differs from current, so you
  can see exactly what a tap is about to change before committing to
  it. This needed real firmware changes, not just a UI relayout: the
  device now has to remember the *exact* values a maze was actually
  built from, separately from whatever the live knob atomics currently
  read - see `currentGenAlgo`/etc. in `Core1Entry`. Along the way, this
  also fixed a subtle pre-existing race: the old code re-read the live
  atomics a second time when reporting "current" state, which could
  disagree with what was actually just generated if a knob was being
  turned on core0 at that exact moment.
- New SysEx messages `PENDING_INFO` (0x06) and `LIVE_INFO` (0x07);
  `ALGO_INFO` (0x04) narrowed to just the four CURRENT-only fields it
  now represents. See `sysex_protocol.h`'s updated header comment for
  the full current/pending/live three-way split.
- The web app's layout changed to match: a right-hand settings column
  next to the maze (stacks below it on narrow screens) replaces the old
  flat key-value strip and separate scale panel.

Same unbuilt caveat as always for the firmware side; the web page
itself needs no build step, but hasn't been opened in a browser here
either.

**Newest: Path B's clock ratio expanded to 21 options, plus a tempo-lag
fix on the internal clock.**

- Down+Y's clock multiply/divide went from 7 options (÷4 ÷3 ÷2 ×1 ×2 ×3
  ×4) to 21 (×16 ×13 ×11 ×8 ×7 ×6 ×5 ×4 ×3 ×2 ×1 ÷2 ÷3 ÷4 ÷5 ÷6 ÷7 ÷8
  ÷11 ÷13 ÷16). `kClockRatios` is generic over table size, so nothing
  in the divide/multiply logic itself changed - just the table, the
  default index (×1 moved from position 3 to position 10), and the web
  app's label list.
- **Fixed: turning the tempo knob while on the internal clock could
  leave Path B's multiply ratios (×2 and up) stepping at the old rate
  for up to a full beat.** Root cause: those ratios subdivide the time
  *between* main clock ticks, and that subdivision period was only ever
  refreshed from a real measured tick-to-tick interval - which, on the
  internal clock, is one full beat behind whatever the tempo knob is
  currently sitting at. Path A and the divide ratios don't have this
  problem since they react to the tempo atomic every sample. Fix: on
  the internal clock (only - external still has no choice but to
  measure, since there's no bpm to read off a jack), the multiply
  subdivision period is now computed directly from the current tempo
  every sample, so a knob turn reaches Path B's multiply steps
  immediately, the same way it already reached Path A and the internal
  clock itself. See `bSubdividePeriod` in `ProcessSample`.

**Newest: a third maze-to-CV mapping mode, Cross-path leap.** Direct
and Run-length-as-leap both size a path's own turn from its own just-
finished run. Cross-path leap (mode 2, Middle+Y) instead sizes each
path's turn from the *other* path's most recently finished run - Path A
holds a straight of 2 before its next turn, and the next corner Path B
hits leaps by 2, regardless of what Path B's own approach looked like.
Same 7-degree cap as Run-length-as-leap, for the same reason (an
uncapped long corridor shouldn't slam straight to the pitch clamp on
one turn). Implementation: `MazeSequencer` now tracks
`LastRunLength()` - the length of the run it most recently finished -
and `Step()` takes the sibling path's `LastRunLength()` as a parameter
so it's available the instant a turn needs it (see `sequencer.h` and
the `seqA_.Step(seqB_.LastRunLength())`/`seqB_.Step(seqA_.LastRunLength())`
calls in `ProcessSample`). On a sample where both paths happen to turn
at once, whichever path steps second sees the other's brand-new run
length while the first sees the other's previous one - a minor,
one-sample ordering quirk, not worth chasing given how rarely both
paths turn on exactly the same sample.

**Newest: a full I/O rewire - dedicated modulation jacks, a CV trigger,
and two new derived audio outputs.** Several changes at once, all part
of the same idea (put the four analogue jacks and two audio jacks to
work, now that dual-path mode has left most of them unused):

- **Audio In 1/Audio In 2/CV In 1 are each now dedicated to exactly one
  LIVE parameter** - Path B's clock ratio, the mapping mode, and the
  shared scale respectively - live and active regardless of which
  switch bank is currently held, unlike the old CV1/CV2-sums-with-
  whichever-knob-X/Y-currently-is pattern. That old pattern is gone:
  solve algorithm A/B and braid lose CV modulation entirely (knob-only
  now). See "Controls" above for the full jack table and
  `ProcessSample`'s "Dedicated live modulation inputs" block for the
  implementation - each jack sums onto that bank's knob-committed value
  (via the new `BucketCenterRaw` helper) and re-buckets, live, every
  sample; an unpatched jack is a true no-op since ComputerCard zeroes it.
- **CV In 2 is now a regenerate + reset trigger**, not a modulation
  input - crossing ~1.2V fires exactly what a Down-tap fires. No
  hardware edge-detector exists for an analogue jack, so this is a small
  hand-rolled Schmitt trigger with hysteresis (`kCv2TriggerHighThreshold`/
  `kCv2TriggerLowThreshold`) to stop a noisy or slowly-drifting signal
  from re-triggering itself.
- **Audio Out 1/2 now carry sum and absolute-difference signals**
  derived from the two paths' pitch - previously unused entirely. See
  "Audio Out: sum & difference, with slew" below for the details,
  including the new per-channel slew setting (Off/Fast/Medium/Slow,
  web-app-only, card-remembered-while-powered) and the new SysEx
  messages (`SLEW_INFO`/`SET_SLEW`) that carry it.

## Controls

The switch is a 3-way **bank selector**, not a momentary trigger (with
one exception - see the last row). Every position is held: turn a knob
while the switch sits there and that bank's parameters update live.

| Control | Switch Up | Switch Middle (default) | Switch Down |
|---|---|---|---|
| Main | Generation algorithm: Prim / Kruskal / Wilson / DFS | Tempo, 40-400 BPM (internal clock) | Maze size (pending) |
| X | Path A solve algorithm, 5-way | Quantizer scale, 10-way, shared by both paths | Braid amount 0-100% (pending) |
| Y | Path B solve algorithm, 5-way (independent of A) | Maze-to-CV mapping mode, shared, 3-way: Direct / Run-length-as-leap / Cross-path leap | Path B clock multiply/divide, 21-way: ×16 ×13 ×11 ×8 ×7 ×6 ×5 ×4 ×3 ×2 ×1 ÷2 ÷3 ÷4 ÷5 ÷6 ÷7 ÷8 ÷11 ÷13 ÷16 (live, not pending) |
| Switch Down, tap (≤~300ms) | — fires one regenerate, on release, using whatever's currently pending — | | |

"Pending" means the value previews live (LEDs, visualizer) but doesn't
touch the actual maze/paths until the next Down-press. Tempo, scale,
mapping mode, and Path B's clock ratio are **not** pending - they take
effect immediately, since none of them are maze/path properties.

**CV1/CV2 no longer sum into whichever of X/Y they used to ride.**
Instead, each analogue jack is dedicated to exactly ONE of the three
LIVE parameters, always active regardless of which bank the switch is
currently on - that's the whole reason those three are LIVE in the
first place, so a permanently-patched modulation source is worth more
than a bank-gated one:

| Jack | Dedicated to | Live contribution to... |
|---|---|---|
| Audio In 1 | Path B clock multiply/divide | Down+Y's knob-committed value |
| Audio In 2 | Maze-to-CV mapping mode | Middle+Y's knob-committed value |
| CV In 1 | Quantizer scale | Middle+X's knob-committed value |

Each jack's reading is summed onto that bank's own knob-committed value
(taken from the CENTER of its current bucket, so an unpatched jack -
which ComputerCard zeroes automatically via the normalisation probe -
is a true no-op, not a small unwanted nudge) and re-bucketed, live,
every sample. Solve algorithm A/B and braid **lose** the CV modulation
they used to get from CV1/CV2 in this change - they're knob-only now.
See `main.cpp`'s "Dedicated live modulation inputs" block in
`ProcessSample`.

CV In 2 isn't a modulation source at all - it's a dedicated **trigger**:
crossing above ~1.2V fires exactly what a Down-tap fires (a fresh maze,
both paths re-solved, playback reset to the entry cell). There's no
hardware edge-detector for an analogue jack (unlike Pulse In 1/2), so
this is a small software Schmitt trigger with hysteresis (fires above
~1.2V, has to fall back below ~0.6V before it can fire again) - see
`kCv2TriggerHighThreshold`/`kCv2TriggerLowThreshold`.

| Jack | Function |
|---|---|
| Pulse In 1 | Clock. Internal clock drives playback whenever nothing is patched in here; patching a cable in hands control to it unconditionally (jack presence, not signal activity, decides this). |
| Pulse In 2 | Reset - restarts both paths' playback from their entry cell. Does **not** rebuild the maze or re-solve. |
| CV In 1 | Live addition to the shared quantizer scale (see above). |
| CV In 2 | Regenerate + reset trigger (see above) - **not** a modulation input. |
| Audio In 1 | Live addition to Path B's clock multiply/divide (see above). |
| Audio In 2 | Live addition to the maze-to-CV mapping mode (see above). |
| Pulse Out 1 | Path A turned (either direction) |
| Pulse Out 2 | Path B turned (either direction) |
| CV Out 1 | Path A melody, calibrated MIDI note (`CVOut1MIDINote`) |
| CV Out 2 | Path B melody, calibrated MIDI note (`CVOut2MIDINote`) - independent of Path A |
| Audio Out 1 | Sum of Path A + Path B's pitch, uncalibrated (see "Audio Out: sum & difference" below) |
| Audio Out 2 | Absolute difference between Path A and Path B's pitch, uncalibrated, always >=0 |

LEDs: 0 blinks on every main clock tick (internal or external,
whichever is driving), 1 shows Path A's ping-pong direction (lit =
forward; Path B's direction isn't shown on LEDs, only in the
visualizer), 2 brightness shows which bank is held (off=Middle,
~half=Up, full=Down), 3 brightness shows a headline value for the
current bank (generation algorithm / quantizer scale / braid amount),
4/5 flash when Path A/B turns. All 6 flash together once (~150ms) right
after a Down-press fires a regenerate, as a "generating" confirmation.
See the comment block at the top of `src/main.cpp` for the exact
mapping.

## Audio Out: sum & difference, with slew

Audio Out 1/2 were unused until now - both are derived, uncalibrated
combinations of the two paths' pitch, entirely separate from CV Out
1/2's actual melody lines:

- **Audio Out 1 = sum** of Path A + Path B's pitch. Leans towards the
  two melodies' shared motion - they move apart as the paths diverge,
  together when they don't.
- **Audio Out 2 = |Path A − Path B|** - a rough, always-non-negative
  measure of how far apart the two voices currently are.

Both are converted from MIDI note numbers via a simple, deliberately
uncalibrated ~1V/oct-ish formula (`MidiNoteToAudioRawUnclamped`) -
Audio Out has no calibration path at all on this hardware (only
`CVOutMIDINote`/`CVOutMillivolts` are calibrated; input calibration
isn't implemented either per the project directive), so there was no
point chasing precision for a derived, secondary pair of outputs.

Each of the two gets its own independently-selectable **slew** - Off
(default) / Fast (~30ms) / Medium (~200ms) / Slow (~1.4s) - a one-pole
exponential lag (`SlewLimiter`) that can turn a jumpy source signal into
a gliding one. There's no spare knob for this, so it's **settable only
from the web app** (two dropdowns at the bottom of the settings panel) -
but the card is authoritative for it, the same as every other LIVE
setting: reopening or reconnecting the web app shows whatever the card
currently has, not just whatever the browser tab last set. Like every
other LIVE parameter on this card, though, it does **not** survive a
power cycle - it resets to Off on boot, it isn't written to EEPROM or
flash. See `SlewLimiter` and `sysex_protocol.h`'s `SET_SLEW`/`SLEW_INFO`.

## How the maze becomes music

1. **Generate**: build a perfect maze (a spanning tree - exactly one
   route between any two cells) using the selected algorithm, from
   (0,0) to (dim-1,dim-1).
2. **Braid** (optional, X knob only - see "Controls" above): knock some dead ends through into a
   neighbouring passage, adding loops. At 0% this is a pure perfect
   maze, so Path A and Path B - whatever algorithms they're set to -
   find the *same* single route and play in unison; higher values open
   up more than one possible route, which is what actually lets the two
   paths diverge from each other.
3. **Solve, twice** - Path A and Path B each pick independently from the
   same five options, each with a genuinely different character:
   - **Direct**: BFS shortest path. Always exact, and the only one of
     the five that has no chance of falling back (see below).
   - **Wall-follower**: right-hand rule. Wanders into every dead end
     before finding the exit - usually a much longer path than Direct.
   - **Tremaux's**: the classic "chalk marks" method - mark each
     corridor as you walk it, prefer unmarked ones, and only take a
     corridor a third time... you never do, corridors close after two
     passes. Unlike wall-follower, this is provably correct even with
     loops in the maze (braid > 0).
   - **Pledge**: tries to walk straight toward the exit; when blocked,
     wall-follows while counting net turns, and goes back to walking
     straight once that count returns to zero. Escapes loops/islands
     that can trap plain wall-following. Adapted here to steer toward a
     point (the exit) rather than the textbook's fixed compass heading -
     see the comment in `maze_solve.h` for exactly how.
   - **Random walk**: mostly free/chaotic direction choice, with a
     lightly-weighted (not guaranteed) pull toward the exit so it
     actually finishes within the step budget on a real maze instead of
     wandering for a statistically enormous number of steps - see the
     comment in `maze_solve.h` for why a *truly* unbiased random walk
     isn't practical here.

   None of the four non-Direct solvers is guaranteed to reach the exit
   within this card's fixed step budget on every possible maze
   (heavier braiding adds more corridors than a perfect maze has, and
   random walk has no hard bound at all in principle) - so every one of
   them is checked against where it actually ended up, and silently
   replaced with Direct if it came up short. See `ReachedExit()`/
   `Solve()` in `maze_solve.h`.
4. **Sequence, twice**: each solved path is broken into runs between
   turns, independently. A run's length is how many clock pulses to
   wait; each turn on that path fires its gate (Path A → Pulse Out 1,
   Path B → Pulse Out 2 - either direction, since only 2 pulse outputs
   exist for what would ideally be 4) and moves that path's melody by
   some number of scale degrees, quantized into the shared scale (Major
   by default - see "Selectable scales" below). How many degrees depends
   on the mapping mode (Middle+Y, shared by both paths, 3-way): **Direct**
   always moves exactly 1 degree per turn (the original behaviour);
   **Run-length-as-leap** moves by the length of the run that just
   finished instead (capped at 7 degrees), so a quick zigzag barely
   moves the melody while a long corridor leaps; **Cross-path leap**
   does the same size-from-run-length idea, but each path's leap is
   sized from the *other* path's most recently finished run instead of
   its own - so e.g. Path A holding a straight run of 2 steps means the
   next turn Path B hits leaps by 2, whatever Path B's own approach to
   that corner looked like (also capped at 7 degrees; see
   `MazeSequencer::LastRunLength()`/`LeapAmount()` in `sequencer.h`).
   Reaching its own exit pulses that path's gate and reverses its own
   direction (ping-pong) - Path A and Path B do this on their own
   schedules, not each other's, which is the source of the
   drifting/phasing relationship between the two voices - and, since
   this round of changes, Path B can also be running at a different
   clock pace entirely (see "Path B's clock multiply/divide" below).

## Internal clock

Pulse In 1 is the clock, same as always - but now, whenever nothing is
physically patched into that jack, an internal clock generated from the
tempo knob (Middle+Main, 40-400 BPM) drives playback instead. Patch a
cable in and the internal clock goes silent immediately: this is
**jack-presence** detection (`Connected(Input::Pulse1)`, via the
normalisation probe already enabled in the constructor), not
"is a pulse actually arriving" detection - a cable that's plugged in but
not currently sending pulses still silences the internal clock, on the
theory that a patched cable represents deliberate intent to clock
externally.

The internal clock is a simple free-running sample counter (`samples
per beat = 48000 * 60 / bpm`), reset each time it fires. Changing tempo
takes effect on the *next* beat, not instantly mid-beat - a deliberate
simplification (see `internalClockAccum_` in `main.cpp`) rather than
tracking exact fractional phase.

## Path B's clock multiply/divide

Down+Y and Audio In 1, live, regardless of bank (21-way: ×16 ×13 ×11 ×8
×7 ×6 ×5 ×4 ×3 ×2 ×1 ÷2 ÷3 ÷4 ÷5 ÷6 ÷7 ÷8 ÷11 ÷13 ÷16) let Path B run
at its own pace relative to the shared main clock. Division just counts main clock ticks and steps Path B
every Nth one - straightforward, and since it just watches `clockEdge`
it already tracks tempo changes immediately. Multiplication is more
involved: it has to fire Path B N times evenly spaced *between* main
clock ticks, not bunched up at the start of each interval, so it needs
a period to subdivide. On the **internal clock** that period is read
straight from the tempo knob/CV every sample, so a tempo change reaches
Path B's multiply subdivisions immediately, same as Path A. On an
**external clock** there's no bpm to read - the period can only be
known by measuring the actual time between the last two main ticks -
so a tempo change on the external source (i.e. whatever's driving that
clock) necessarily takes up to one beat to be reflected in Path B's
subdivision rate. See `bSubdividePeriod` in `ProcessSample` if you want
to change how this behaves - in particular, the ratio resets its
counters on Pulse In 2 (reset) but not automatically when you turn the
knob mid-play, so changing the ratio can cause one slightly-off-timed
step right at the moment of the change before it settles into the new
pattern. That's an accepted, minor transient, not a bug.

## Why generation runs on core1

Maze generation and solving are graph algorithms with no fixed time
bound - completely wrong to run inside the ~20μs `ProcessSample` audio
interrupt on core0. Per the directive's multicore guidance, this card
puts all of that on core1: core0's interrupt only ever does array reads,
a few comparisons, and an O(1) pointer handover when core1 finishes a
new maze (see the `PreparedPath` double-buffer and `std::atomic`
handshake at the top of `main.cpp` and in `sequencer.h`). Nothing
maze-sized (loops over cells, up to `MAX_DIM`×`MAX_DIM`) ever runs on
core0.

`MAX_DIM` is 16 (256 cells) by default - comfortably inside RAM even
with two generation buffers and, since dual-path mode, *four*
prepared-path buffers (two paths × double-buffered). Raise `MAX_DIM` in
`maze_types.h` if you want bigger mazes; everything else scales with it
automatically.

## Selectable scales

Both paths now share a single scale (previously independent per path -
see the changelog above) - Major, Natural minor, Dorian, Mixolydian,
Harmonic minor, Major pentatonic, Minor pentatonic, Blues, Whole tone,
or Chromatic (see `scales.h`). It's settable three ways: the Middle+X
knob, CV In 1 (live, regardless of which bank is held - see "Controls"
above), or the web app's dropdown - whichever you touch most recently
wins, since a knob-write only actually happens when the knob's own
computed value changes (an idle knob never overwrites a web-app choice
just by sitting there being read every sample). The root note is
fixed at C3 - only the scale (which notes are available) is selectable,
not the key.

Changing scale takes effect immediately, mid-sequence - it's purely a
note-mapping change (see `MazeSequencer::SetScale()`), not something
that touches path/playback state, so there's nothing to glitch. The
selection is **not saved to flash**: it resets to Major on every
power-cycle, and the web app re-syncs to whatever the card currently
has on connect (via `REQUEST_STATE`) - worth adding persistent storage
later if that turns out to matter in practice, but out of scope for
what was asked here.

## Live USB visualizer

`web/index.html` is a standalone page (Chrome/Edge only - Web MIDI API)
that connects to the card over USB-MIDI SysEx. The maze and both solved
paths (Path A solid, Path B dashed, distinct colors) sit on the left
with two live cursors, each color-coded for its own ping-pong direction;
a settings panel runs down the right-hand side listing every parameter
as **current / pending** columns:

- **Generator, Solver A, Solver B, Maze size, Braid** are "pending"
  parameters (gated behind the card's next Down-tap): the current column
  always shows what's actually baked into the maze/paths playing right
  now; the pending column stays empty unless the live knob preview
  differs from that, in which case it fills in with a yellow highlight
  showing exactly what a tap would change it to.
- **Tempo, Mapping mode, Path B's clock ratio** have no pending concept
  - they're never gated behind a regenerate - so they just show one
  live value each, updating the instant their knob, or their dedicated
  Audio In 2/Audio In 1 jack, moves.
- **Scale** is an interactive row: a dropdown, shared by both paths,
  settable from here, the card's knob, or CV In 1 (whichever was
  touched most recently wins - see `SoftTakeover` in `main.cpp`).
- **Sum slew / Diff slew** are the other two interactive rows: two more
  dropdowns (Off/Fast/Medium/Slow) for Audio Out 1/2's slew - see "Audio
  Out: sum & difference" above. Unlike scale, there's no physical knob
  for these at all, so the web app is the only way to set them - but the
  card still remembers whichever value it was last told, for as long as
  it stays powered.

Open the file, pick the card from the dropdown, click Connect.

**This is the newest and least-verified part of the whole project.**
Specifically:

- `src/usb_descriptors.cpp` and `src/tusb_config.h` set up a bare
  USB-MIDI device via TinyUSB, following the standard pattern from
  TinyUSB's own MIDI example - but USB enumeration is notoriously easy
  to get subtly wrong (endpoint numbers, descriptor lengths, string
  table indices) and there is no way to test it without a real device.
  If the card doesn't show up as a MIDI device at all, this is the
  first place to look - check `dmesg` (Linux) or Device Manager
  (Windows) for enumeration errors.
- The VID/PID (`0xCafe`/`0x4004`) uses TinyUSB's own placeholder vendor

  ID, which is the conventional choice for hobby projects without a
  registered USB VID - not a functional risk, just worth knowing if you
  ever see it in a device list.
- Core1 now does four jobs at once: maze generation, solving twice,
  `tud_task()` servicing, and draining the (now dual-tagged) position
  queue from core0. None of these block each other for long (generation
  and solving together are still a few thousand operations at most;
  sending one SysEx message is fast), but it's untested under real USB
  host timing.
- The core0→core1 step-position queue (`stepqueue::` in `main.cpp`) is a
  lock-free single-producer/single-consumer ring buffer - a standard
  pattern, but exercised here for the first time in this project.
- `sysex_io::PollIncoming()` parses two different browser→device
  message shapes (a bare `REQUEST_STATE` and a 1-byte-payload
  `SET_SCALE`) with a small state machine, rather than a fixed-pattern
  matcher. Straightforward, but it's new-ish and, like everything else
  touching USB here, untested against a real host.

See `src/sysex_protocol.h` for the exact message format if you want to
extend it (e.g. adding a "connected" handshake, or sending knob values
live).

## Known limitations / things to double check

- **Knob-to-parameter mapping constants** (`4 + (k * (MAX_DIM-4))/4095`,
  the various bucket-selection scalings) are reasonable first guesses,
  not tuned by ear - expect to want to adjust the feel once it's on
  hardware, especially the internal clock's tempo curve (currently
  linear 40-400 BPM; an exponential/log taper would give finer control
  at slow tempos, at the cost of complexity this wasn't asked for).
- **Wilson's algorithm** has a bounded retry loop (`guardIterations`) as
  a safety valve against a pathological RNG state; it should never be
  hit in practice, but if you ever see a maze with an isolated unreachable
  pocket, that's the symptom to look for.
- **Scale has no selectable root** - both paths are always rooted on
  C3; only which scale (Major, Dorian, pentatonic, etc.) is selectable.
  Adding a selectable root would follow the same pattern (another SysEx
  message, another atomic) if wanted later.
- **Both paths only ever solve the one shared maze** - there's no way to
  give Path A and Path B different entry/exit points or different maze
  sizes; the only source of divergence between them is algorithm choice
  plus braid %. At braid 0%, every algorithm finds the identical unique
  route, so both paths play in unison - worth knowing if dual-path mode
  seems to be doing nothing.
- **Path B's clock ratio doesn't resync automatically when you turn the
  knob mid-play** (only on Pulse In 2) - see "Path B's clock
  multiply/divide" above for the minor transient this can cause.
- **Internal clock tempo changes apply on the next beat, not instantly**
  - see "Internal clock" above.
- **CV In 2's trigger thresholds are fixed, not tuned by ear**
  (`kCv2TriggerHighThreshold`/`kCv2TriggerLowThreshold`, ~1.2V/~0.6V) -
  a reasonable first guess for "this counts as a trigger", not verified
  against real Eurorack gate/trigger sources on hardware.
- **Audio Out 1/2's slew settings don't survive a power cycle** - like
  every other LIVE parameter on this card, they reset to Off on boot;
  they're only remembered for as long as the card stays powered (see
  "Audio Out: sum & difference, with slew" above). Adding real
  persistence (EEPROM/flash) would be a separate, bigger change if
  wanted later.
- The RNG (xorshift32) is seeded from `UniqueCardID()` - fine for
  musical variety, not intended to be cryptographically random.
- **Soft-takeover means a knob can feel "dead" until it finds its
  stored value** - this is expected, standard pickup-knob behaviour
  (see `SoftTakeover` in `main.cpp`), not a bug: every parameter starts
  locked to its current value on boot and every time you return to its
  bank, so turning a knob does nothing until it physically reaches the
  value already in effect. Sweep it across its range once and it will
  always find and catch.

## Files

- `src/maze_types.h` - grid representation, RNG, shared constants
- `src/maze_gen.h` - Prim's, Kruskal's, Wilson's, DFS backtracker, braiding
- `src/maze_solve.h` - Direct, Wall-follower, Tremaux's, Pledge, and Random-walk solvers
- `src/sequencer.h` - path → turn/run events → scale-quantised notes + gates, incl. mapping modes
- `src/scales.h` - the 10 built-in quantization scales
- `src/main.cpp` - ComputerCard subclass, core1 worker (maze gen + USB), switch-bank
  knob handling, internal clock, Path B clock ratio, I/O wiring
- `src/usb_descriptors.cpp` / `src/tusb_config.h` - USB-MIDI device setup (TinyUSB)
- `src/sysex_protocol.h` - message format for the USB visualizer link
- `web/index.html` - standalone live visualizer (Chrome/Edge, Web MIDI): maze/paths/cursors plus a current-vs-pending settings panel and scale selection
- `src/ComputerCard.h` - vendored copy of the library this was checked against (v0.3.0)
- `src/CMakeLists.txt` - build file
