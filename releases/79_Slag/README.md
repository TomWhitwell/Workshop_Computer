# Slag

Metallic percussion, generated.

One harsh metal-percussion voice wired to a random step sequencer whose stream
you can **lock**. Every step re-rolls the whole voice — pitch, decay, noise
blend, corrosion, accent, and whether the step fires at all — and the Steps knob
freezes those rolls into a repeating pattern. Leave it Off for endless fresh
chaos; when you hear something you like, lock it.

Status: **working on hardware.**

## The panel

The switch re-banks the three knobs, so three knobs are nine.

| | **Middle** — the voice | **Up** — the machine | **Down, held** — setup |
|---|---|---|---|
| **Main** | **Pitch** ~55Hz–1.8kHz. Low is gong and anvil, high is hats and shrapnel. | **Steps** Off, 2, 3, 4, 5, 7, 8, 10, 16, 32 | **Tempo** ~0.5–32Hz, or fully anticlockwise for off |
| **X** | **Decay** ~4ms–2.7s. Tick to long ring. | **Chance** density below halfway, ratchets above | **Tone** metal against noise (or Audio In 1) |
| **Y** | **Rust** Pade-tanh drive, then rate reduction past halfway. Warm, fuzzy, aliased garbage. | **Spread** how far each hit strays from "everything at once" | **Metal** pitched bell to atonal scrapyard |

Down is spring-loaded, so it does two jobs. **Tap** it and it plays a hit by
hand, ignoring Chance — and it does *not* advance the sequencer, so reaching
down cannot shift a locked pattern out of phase. **Hold** it and the knobs
re-bank onto Tempo, Tone and Metal: the things you set, rather than the things
you play. (Pressing down always sounds a hit, including on your way to the third
bank. Let it ring.)

A knob does not take over its parameter until you have physically moved it, so a
trip to another bank never yanks the one you came from. At power-up the bank you
boot into adopts its knobs straight away, so the panel never lies.

The LEDs walk the pattern; the brightness is the hit. A skipped step still moves
the dot, but leaves it dark.

## Patching

| Jack | |
|---|---|
| Pulse In 1 | **Clock** — one step per rising edge. Unpatched, the internal clock runs at Tempo. |
| Pulse In 2 | **Reset** — restarts a locked pattern at step 1 |
| CV In 1 | **Pitch**, 1V/oct, adds to the knob — and, with no clock patched, the keyboard |
| CV In 2 | **Chance**, adds to the knob |
| Audio In 1 | **External source** — replaces the internal noise; blend it with Tone |
| Audio In 2 | unused |
| Audio Out 1 | **Metal** — rusted, and with no low end in it at all |
| Audio Out 2 | **Kick** — clean, and never through the drive |
| CV Out 1 | The step's pitch as 1V/oct, sampled and held |
| CV Out 2 | The decay envelope, 0..+6V |
| Pulse Out 1 | **Trigger** — every step that sounds |
| Pulse Out 2 | **Ghost** — every step that was skipped |

## Playing it with 4 Voltages

Unpatch the clock and turn Tempo fully anticlockwise. Slag now says nothing on
its own — and CV In 1 becomes a keyboard.

Patch a **4 Voltages** output to CV In 1. A jump of more than about a semitone
is a new note: the voltage sets the pitch *and* keys the timbre roll, so each of
the four buttons is its own repeatable hit rather than the same clang
transposed. Spread decides how far apart the four sounds are; at zero they are
one sound at four pitches, and wound up they are four different pieces of metal.

The jump has to be a jump, so pressing the same button twice in a row does
nothing — same as any keyboard that only sends pitch. It also means a smooth LFO
into CV In 1 will not trigger anything, and a stepped or sample-and-hold source
will play it like a sequencer.

Patch a clock back into Pulse In 1 and CV In 1 goes back to being only a pitch
input, so a melodic sequencer can't double-trigger against its own clock.

## Things to do with it

**Lock what you hear.** Steps at Off, Chance around halfway, Spread up. When a
phrase goes past that you want, turn Steps to a length — it captures the window
that just played. Moving between lengths after that re-slices the *same* window
rather than rerolling, so 4 → 8 shows you more of the pattern you already have,
not a different one. To reroll, pass back through Off.

**Two interlocking parts from one knob.** Trigger out to a kick, Ghost out to a
hat. Chance now crossfades between them: the steps Slag skips are exactly the
steps the other voice takes.

**Two jacks, two chains.** Audio Out 1 is the metal, Audio Out 2 is the kick,
and there is no crosstalk — Out 1 measures 2% of its energy under 250Hz, Out 2
measures 100%. Reverb, filters and whatever else go on the metal without ever
touching the low end, and the kick reaches the mixer dry and at full scale.
Nothing on the card mixes them, so their relative level is yours to set.

**Feed it something.** Audio In 1 replaces the noise source. A drum machine, a
field recording, a feedback loop — it gets gated by the percussion envelope and
rusted, so the sequencer chops whatever you give it.

**Melody from the same stream.** CV Out 1 is the step's pitch, held. Patch it to
another oscillator and it tracks the metal's random pitch, so a locked pattern
locks the melody too.

## How it works

**The voice** is six square oscillators **XORed together**, blended against
white noise (or Audio In 1) by Tone, through a decay envelope with a short pitch
blip on the attack, then into rust: digital overdrive followed by downsampling.

The XOR is the whole trick. *Summing* six squares gives you six discrete
pitches — a chord — which sounds like a videogame no matter how you tune the
ratios. XOR is a square-wave ring modulator, so every oscillator beats against
every other and the sum-and-difference products smear the spectrum into
something dense and inharmonic. Measured as spectral flatness (0 is a pure tone,
1 is white noise) the summed version scored 0.02; the XOR scores 0.28–0.49.
`sim/render.cpp` asserts those floors so it cannot drift back.

Pure cloud has no pitch at all — which is the point of the top of the Metal
knob, but leaves nothing to tune. So Metal crossfades a clean fundamental
against that cloud, and opens the partial ratios out as it goes. That puts the
pitch back the way a bell has one: a strong fundamental sitting in a cloud of
inharmonic partials, rather than the chord of six that started all this. The
tonal end still measures 0.17 flatness — clearly pitched, but nowhere near the
0.02 of a bare square. There is deliberately no whole-number-harmonic setting;
harmonic ratios ring-modulate back into a periodic signal, which is a beep.

**Chance runs past "every step fires" into ratchets.** A sequencer that only
ever asks "does this step fire?" is a coin flip on a grid, and no amount of
external clocking fixes that — the grid is the problem. Past halfway, every step
fires and the knob instead buys an increasing chance that a step subdivides into
2–4 strikes of the same sound. The step length comes from the measured gap
between clocks, so it follows whatever tempo you feed it, and the roll comes
from the same hash as everything else — so a locked pattern locks its ratchets
too.

**Rust** is a waveshaper then a sample-rate reducer. A makeup gain of
`1/shaper(drive)` follows it, so full scale comes out at full scale however hard
you drive it — Rust changes the character, not the level (measured: 1.26× across
the whole knob). The rate reducer is a phase accumulator rather than a sample
counter, because a counter can only divide by whole numbers and left the top
half of the knob with just eight reachable rates.

**The attack pitch blip** is a modulation destination, not a constant. On the
real unit the envelope is routed to pitch rather than amplitude, and the depth is
one of the things the randomiser moves — it is the difference between a tick, a
thud and a zap — so Spread gets it too, from nothing to three octaves. It runs
over ~25ms; at the ~4ms it started as, a pitch drop is a click rather than a
thump.

**The thump** is a triangle three octaves under the *knob* pitch, on its own
~75ms envelope, mixed in **after** the rust stage. Three things matter about
that, and all three were mistakes first:

- It runs off the knob, not the per-step pitch, so it ignores the random spread
  and the big attack blip. A bass drum that jumps two octaves a hit is not a
  bass drum. It keeps a modest sweep of its own, which is what a kick does.
- It goes in *after* the drive. Run through it, a near-full-scale 50Hz triangle
  just gets its peaks flattened — at Rust three-quarters up the output was
  clipping 96% of the time, which is exactly what "distorted low end" sounds
  like. Body clean, metal dirty, the way a drum synth is wired.
- It is **summed**, not crossfaded. A crossfade makes "more kick" mean "less
  metal", and at a kick level worth having that wiped the metal out entirely.
  They live in different octaves and barely fight, so they sum into a master
  saturator that catches the peaks — which is the right answer anyway for
  anything calling itself industrial. That saturator carries no makeup gain, so
  it *is* the ceiling: the sum can never reach the clamp. It costs ~2dB at
  nominal level, which is the price of never hard-clipping the kick.

It is a **sine**, not a triangle — a triangle at 50Hz puts real energy at 150
and 250Hz, which you hear as buzz rather than feel as weight. And the sweep
rides the *fast* envelope while the level rides the slow one, which is the whole
trick of a 909-style kick: pitch falls two octaves in ~25ms while the amplitude
rings on for ten times that. At Pitch noon it starts around 210Hz and lands on
53Hz. That drop is the thump; a body without it is just a low hum.

At any useful Pitch setting the metal itself sits well above 250Hz — mid-pitch
measured 2.4% of its energy down there before any of this — so without a
dedicated sub there is nothing to feel. Now 46%, with the output clipping 0.0%.
The body envelope never outlasts the main decay, so a short Decay setting still
gives you a tick rather than trailing a thump behind it.

The fundamental inside the metal bank is a triangle too, for a related reason: a
square's odd harmonics land squarely in the mid band and rattle.

Keeping it off the drive is now structural rather than a routing choice: the
kick has its own jack and never enters the rust stage at all. It also means the
kick can run to full scale — sharing one output with the metal meant both had to
give up headroom, and a master saturator had to sit across the sum to catch the
peaks. Neither is needed, so both jacks are louder and cleaner than the mix was.

**Every blend is constant-power.** Tone, Metal and the body are all crossfades,
and a *linear* crossfade of two uncorrelated signals loses 3dB in the middle —
the weights sum to one but the powers do not. Tone measured a 5dB hole at
three-quarters before this. The ends of Tone still differ by ~4dB, but
that is honest: a full-scale square carries more RMS than noise at the same peak.

**The random stream** is a hash of `(seed, step index)`, not a stored buffer. So
"locking" is just choosing a window into that stream: instant, and re-slicing
the window at a different length keeps the pattern's character. One hash per
step is sliced into seven fields — skip, pitch, decay, noise, rust, accent, pitch-blip
depth — which is why the rhythm and the timbres lock *together*. The seed is stirred from the
knob positions and the timing of the first step, so it isn't the same sequence
every power-up.

**Each step draws a kind of hit, not just a set of numbers.** Nudging every
parameter independently around a common centre makes one instrument wobble
rather than a kit play — every step ends up with a bit of everything in it. So
each step instead draws one of eight archetypes — kick alone, clang alone, tick,
anvil, hat, ghost kick — which set the kick and metal levels, tone, accent and
decay *together*. Spread blends from "everything at once" toward the archetype,
so at zero you still get the uniform voice, and wound up the pattern reads as
several instruments. The continuous dice are still there underneath, but small:
the archetype decides what a hit *is* and the dice only colour it in.

**Each voice rolls its own dice.** This is the part that actually separates
them, and levels could never do it. With one skip roll gating both voices, every
step is offered to both — so whichever fires less often is a strict *subset* of
the other, and no amount of level variation changes that they share one rhythm.
Each archetype instead carries a density for the kick and a density for the
metal, each a share of Chance, and each voice rolls against its own. Measured
over 96 steps: the two coincide 27% of the time where two independent rolls
predict 28% and one shared roll would have forced 51%.

A voice that loses its roll is left alone rather than silenced, so a long kick
rings on underneath the metal-only steps that follow it.

**Phrases.** One archetype is held across a run of steps, so a passage leans
kick-heavy or metal-heavy before moving on, and about a third of steps break out
of the run to keep it from turning into a machine-gun of one sound. The phrase
index comes off the same stream as everything else, so locking a pattern locks
its shape too.

The archetypes are multipliers on the knobs, so the panel still sets the
palette — Tone at zero means no step is airy, however the dice fall.

Spread scales the voice fields but not the skip field: at Spread zero the
voice stops moving and only the rhythm stays random. That is the same split the
original makes between its two randomness generators.

**No floating point in the audio interrupt.** The one `powf` table is built at
boot. The per-sample path is integer only; `updateVoice` and `doStep` are the
only things that call `__aeabi_lmul`, and both run at control rate or on
trigger. (See `92_Vox` for what happens when this rule is broken.)

## Building

Firmware (needs the Pico SDK and `PICO_SDK_PATH`):

```sh
cd src && mkdir -p build && cd build && cmake .. && make -j8
```

Produces `slag.uf2`. Hold BOOTSEL, plug in, copy it over.

Simulator — runs the *real* card source against a mock hardware layer, asserts
the sequencer behaves, and renders demo WAVs. Needs only `g++`:

```sh
./sim/build.sh
```

Left channel is Audio Out 1 (metal), right is Audio Out 2 (kick), so you
can hear what the rust stage is doing to the voice.

| | |
|---|---|
| `out_free.wav` | Steps Off — the stream never repeating |
| `out_locked.wav` | The same settings locked to 8 steps |
| `out_rust.wav` | A locked pattern with Rust swept clean to destroyed |
| `out_metal.wav` | Metal swept from a tight bell to a wide scrapyard |
| `out_keyboard.wav` | Four voltages played as a keyboard, no clock |
| `out_external.wav` | A tone sweep into Audio In 1, chopped and rusted |
