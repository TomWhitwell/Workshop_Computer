# Otherworld vol. 1

Thirty-two custom effects for the Happy Nerding FX AID XL, written in
SpinASM for its Spin FV-1 chip. They are slicers, reverbs, distortions and voices, and every one
follows what you play: its notes, onsets and levels, or the clock on its `_clk` twin.

**Listen in your browser:** https://andreasjansson.github.io/otherworld/. Every effect runs live
on an FV-1 emulator compiled to WebAssembly. You can play it with six loops recorded from Mutable
Instruments Rings and Plaits, and turn its knobs.

**Firmware:** [`site/otherworld-vol1-fxaid-xl.wav`](site/otherworld-vol1-fxaid-xl.wav) holds all
32 effects, in the slot order below. It is a finished release file: this repository has the
effect sources but not the tools that build firmware.

## Flashing

This was made and tested on the FX AID XL. It is the FX AID's firmware format, but it is **not for
the FX AID Pro**, whose firmware is different.

1. Hold both buttons for 10 seconds to enter update mode. The middle LEDs flash.
2. Patch your computer's headphone output into the **SRR** input, and play the WAV at about 80 %
   volume. Use a wired cable; Bluetooth won't do. At full volume a laptop's headphone output can
   overdrive the input and the update fails a few seconds in.
3. While the module receives, the bottom-left LED is lit. If the top-left and top-right LEDs
   flash, the update failed: enter update mode and play it again.

To go back to the factory effects, flash any firmware from [fxaid.app](https://fxaid.app) the same way.

## Effects

The three knobs are CV1, CV2 and CV3. The `_clk` effects take mono audio on input L and a square
clock on input R; their outputs are still stereo.

| Slot | Effect | Category | CV1 · CV2 · CV3 | What it does |
|---:|---|---|---|---|
| 1 | [Prism Scatter](effects/01_prism_scatter.spn) | Slicer | Grain · Prism · Regen | Chaotic re-sequencer that shatters the last second into octave shards |
| 2 | [Prism Scatter (clocked)](effects/01_prism_scatter_clk.spn) | Slicer | Scatter · Prism · Regen | Clock-locked slicer that shatters the last second into octave shards on your grid |
| 3 | [Grain Bloom](effects/202_grain_bloom.spn) | Slicer | Time · Hits · Length | Once every Euclidean cycle one step of your playing is caught as a grain; every hit strikes it, slowed to fill the gap to the next hit, in alternate ears, blooming into reverb as it plays, on a step you set with Time |
| 4 | [Grain Bloom (clocked)](effects/202_grain_bloom_clk.spn) | Slicer | Hits · Length · Bloom | Once every Euclidean cycle one clock step of your playing is caught as a grain; every hit strikes it, slowed to fill the gap to the next hit, in alternate ears, blooming into reverb as it plays |
| 5 | [Zeno's Arrow](effects/95_zenos_arrow.spn) | Slicer | Time · Climb · Edge | Every phrase fires arrows that halve the distance to the downbeat, climbing an octave with every halving |
| 6 | [Zeno's Arrow (clocked)](effects/95_zenos_arrow_clk.spn) | Slicer | Climb · Edge · Spread | Every phrase fires arrows that halve the distance to the downbeat, climbing an octave with every halving |
| 7 | [Quantum Tunneling](effects/121_quantum_tunneling.spn) | Slicer | Time · Erode · Rattle | Every beat throws your last beat at a wall: it bounces back reversed, or tunnels through to the far side an octave down, until your notes wear the barrier away |
| 8 | [Quantum Tunneling (clocked)](effects/121_quantum_tunneling_clk.spn) | Slicer | Barrier · Erode · Rattle | Every beat throws your last beat at a wall: it bounces back reversed, or tunnels through to the far side an octave down, until your notes wear the barrier away |
| 9 | [Turtle Grammar](effects/18_turtle_grammar.spn) | Slicer | Turn · Step · Sprout | Two mirrored tape turtles scratch out a golden-angle grammar |
| 10 | [Turtle Grammar (clocked)](effects/18_turtle_grammar_clk.spn) | Slicer | Sprout · Turn · Ink | Two mirrored tape turtles scratch a golden-angle grammar on the beat |
| 11 | [Step Mutator](effects/73_step_mutator.spn) | Slicer | Time · Mutate · Regen | An 11-step pattern of octave ratchets that every note rewrites on the spot |
| 12 | [Step Mutator (clocked)](effects/73_step_mutator_clk.spn) | Slicer | Mutate · Reach · Regen | An 11-step pattern of octave ratchets that every note rewrites on the spot |
| 13 | [Tesseract](effects/93_tesseract.spn) | Slicer | Time · Turn · Spin | Your playing spins a hypercube: each step one corner plays on the left and its opposite corner on the right |
| 14 | [Tesseract (clocked)](effects/93_tesseract_clk.spn) | Slicer | Turn · Spin · Gate | Your playing spins a hypercube: each beat one corner plays on the left and its opposite corner on the right |
| 15 | [Mitosis](effects/85_mitosis.spn) | Slicer | Cycle · Food · Peak | One note becomes a cell that splits into octave twins, doubles and doubles across the field, then starves |
| 16 | [Noocytes](effects/113_noocytes.spn) | Slicer | Body · Food · Rise | A note becomes a living cell that buzzes, eats the music around it until it is a whole mirrored loop, then rises out of hearing |
| 17 | [Brownian Dust](effects/122_brownian_dust.spn) | Slicer | Cool · Heat · Scatter | Every note heats a cloud of glittering dust: it flies two octaves up and all over the field, then cools, slows and hangs in place |
| 18 | [Strata](effects/165_strata.spn) | Reverb | Abyss · Drift · Glass | Three worlds stacked by pitch: a glass box over a stone hall over a vast dark abyss, and your playing slowly sinks everything until the floor drops away |
| 19 | [Fog Bank](effects/172_fog_bank.spn) | Reverb | Range · Weather · Fog | Your notes bloom into a fog bank and echo back from deep inside it, each return more blurred than the last; the fog rolls in and lifts as you play |
| 20 | [Comet Harp](effects/216_comet_harp.spn) | Reverb | Space · Sweep · Ring | Every note flings two tuned combs out through the reverb, one diving and one soaring in octave leaps, and they swing back to land on your note and ring |
| 21 | [Ventifact](effects/215_ventifact.spn) | Reverb | Decay · Wind · Glass | A stone hall in a sandstorm that your playing whips up: each pass through its four corridors wears a different part of the tail away, to breath, bare air, glass and silt, until it crumbles into singing sand |
| 22 | [Avalanche](effects/71_avalanche.spn) | Distortion | Pack · Snow · Fall | Every note piles onto a glittering snowpack until it breaks, plunges down in octaves and opens into a vast hall |
| 23 | [Numbers Station](effects/179_numbers_station.spn) | Distortion | Time · Chaos · Dial | Your playing keyed out at the beat you dial as chaotic Morse slices, short and long with hard silent rests, every slice crushed, filtered and thrown hard left or right |
| 24 | [Numbers Station (clocked)](effects/179_numbers_station_clk.spn) | Distortion | Chaos · Rests · Dial | Your playing keyed out on the clock by an 8-step loop of whole-pulse slices, half-pulse slices and silent steps that mutates as you turn up Chaos, every slice crushed, filtered and thrown hard left or right |
| 25 | [Pitch-Locked Decimator](effects/103_pitch_locked_decimator.spn) | Distortion | Crush · Speed · Throw | Every phrase crushes down an in-tune staircase into a raw chip buzz and climbs back to clean, again and again, while reversed shards of the crush swing across the stereo field |
| 26 | [Transmutation](effects/247_transmutation.spn) | Voice | Spread · Decay · Shape | Four resonators tuned to the note you play, like the modes of Mutable Rings or the four bands of XAOC Oradea: the first on your note, the other three spread from the harmonic series out to the partials of a metal bar, like the Spread of Noise Engineering's Basimilus Iteritas, morphing from pure sines to soft triangle waves at any level, in still stereo |
| 27 | [Wavetable Thief](effects/102_wavetable_thief.spn) | Voice | Chunk · Greed · Rot | Steals a cycle of every note and plays it on as a frozen digital oscillator with an octave-down shadow, the two trading sides at every theft, rotting into squares as you play on |
| 28 | [Two Whales](effects/258_two_whales.spn) | Voice | Chance · Depth · Ocean | Two whales on either side of you learn the notes you play and sing them back in a whale's voice, one at a time and only on your note changes, sliding into each new note, never on the same one, into a warm, dense sea |
| 29 | [Bellfounder](effects/112_bellfounder.spn) | Voice | Tower · Ring · Peal | Every note strikes a ring-modulated bell cast from the pitch it heard, and the bells grow until they crack |
| 30 | [Phase Ghost](effects/132_phase_ghost.spn) | Voice | Reso · Sweep · Snarl | A phase-distortion synth ghost hears the note under your playing and zaps it back on every note, its cycles ping-ponging between the speakers, and Snarl folds it from a hollow saw into a buzzing growl |
| 31 | [Talking Chip](effects/186_talking_chip.spn) | Voice | Vowel · Species · Vibrato | The chip sings your melody on a vowel you set with CV, oo oh ah eh ee, sliding between legato notes, its vibrato swelling as you hold them |
| 32 | [Moonquake (clocked)](effects/211_moonquake_clk.spn) | Voice | Busy · Mutate · Ratio | A drum machine whose kick is tuned to the newest note you play; Busy turns a kick-and-offbeat grid into a dense random rhythm that repeats or mutates every 16 triggers, Ratio sets kicks against hits, and every hit zaps down onto a pitch that is in tune, a tritone off, or a hiss |

## Repository

- `effects/` the SpinASM sources. Each header carries the effect's name, knobs and description.
- `bank.json` the slot order, categories and loops.
- `build.py` builds the page (`site/index.html`) from `site/page.html`, `bank.json` and the
  sources, using only the Python standard library. To run the page locally, serve `site/` with any
  static server:

  ```bash
  python3 build.py && python3 -m http.server -d site 8000
  ```

- `site/` the page, the loops, the firmware, and the in-browser FV-1 engine (`fv1.wasm`, `fv1-worklet.js`).
- `tools/header4d/` renders the header video.

The sources assemble with any SpinASM-compatible assembler, for example
[spinfv1](https://github.com/ngriaznov/spinfv1-rs). The programs in the firmware are byte for
byte the assembled sources.

## Credits

- The in-browser engine is [spinfv1](https://github.com/ngriaznov/spinfv1-rs) by ngriaznov.
  `site/fv1.wasm` is built with `wasm/build.sh` from the fork
  [andreasjansson/spinfv1-rs](https://github.com/andreasjansson/spinfv1-rs) at commit
  [`5777eca`](https://github.com/andreasjansson/spinfv1-rs/commit/5777eca11f08c33646314eb09c8fdcbf6ce69cfc). The fork adds a model of the FV-1 as measured in the FX AID XL. The engine is
  licensed under the spinfv1 Source-Available License ([`LICENSE-spinfv1`](LICENSE-spinfv1)).
- The header video plays through mpeg4-glitch, a deliberately glitchable MPEG-4 decoder (MIT).
- The FX AID XL is made by Happy Nerding. This project is not associated with Happy Nerding.

The effects, the page and the tools are MIT licensed ([`LICENSE`](LICENSE)).
