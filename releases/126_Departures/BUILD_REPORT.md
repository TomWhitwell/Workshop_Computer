# Departures 0.5.0 — build report

Promoted 8 November 2026 after Adrian reported the experimental hardware tests
passed. Stable 0.5.0 preserves the tested 0.5 experimental UF2 bytes exactly.
Previous stable 0.4.1 and the original experiment are preserved on the development
branch; see [the archive index](archive/README.md).
Host results and ARM timing measurements below are from the tested source;
these estimates are distinct from the user-reported hardware pass.

## Firmware and dependencies

- RP2040 / `PICO_BOARD=pico`, default 2MB layout; fits 2MB and 16MB cards.
- Pico SDK 2.3.0, revision `98a542c1a62fb549ffb5d66a3e5892b06276b670`.
- ARM GNU Toolchain 15.2.Rel1 / GCC 15.2.1; Release, C++17, application `-O3`.
- ComputerCard 0.4.0 unchanged; RAM execution at 192MHz / 1.15V.
- Linker: FLASH 106,788 bytes; RAM 112,264 of 262,144 bytes; 2,048-byte core
  stack in SCRATCH_Y. USB stdio disabled. Palette selection uses RAM only.
- UF2: `uf2/Departures_0.5.0.uf2`, 214,016 bytes, 418 RP2040 blocks.
- SHA-256: `85fc0ef6315ee52285dcd6ea886d3a6f81a67fcac2529141f4056bbbbb0a6f28`.
- Packaging checks all block headers, family, numbering, size and addresses,
  matching the payload to `build/Departures.bin`. Full notices accompany it.

## Palette design

Bells retains stable 0.4.1's synthesis and accepted one-quarter melody mix.
Wood Chimes uses the same four phrase voices, with two sine resonances: a rounded
fundamental and a faster-decaying upper resonance. Ratios across the four loops
are 4, 3, approximately 4.03 and 2. Body decay has an approximately 0.34-second
time constant; the upper strike resonance about 21ms. X varies its initial
strength. Wood pad colour has less upper-harmonic emphasis; bass and effects
parameters are unchanged. No extra voices or sample data are added.

Both palettes retain the note-level ceiling and output limiting. The palette
cannot change after startup, so the audio loop dispatches its synthesis type once
per sample rather than branching inside every oscillator and retrigger tail.

## Functional checks

Apple Clang 21 host checks with AddressSanitizer and UndefinedBehaviorSanitizer
passed. Existing scale, destination, shared-note, clock, phrasing, external-input,
rest/tide and effects-decay checks remain. New `test/palettes.cpp` verifies:

- Normal Middle boot selects Bells even with Main fully right.
- Down at startup opens a silent selector. Left selects Bells, right Wood;
  the correct 1+3 or 2+4 physical LED pair is displayed.
- The centre deadband preserves a selection and a short release bounce does
  not confirm it; a stable release confirms playback.
- Menu state freezes musical events and ignores incoming clock/commit edges.
  Audio and output gates remain silent. LED6 is not used for the choice.
- The boot Down gesture does not advance harmony. A later runtime Down tap
  advances exactly one chord and leaves the palette unchanged.
- A fresh normal-reset object defaults to Bells after a previous Wood choice.
- Over 60 seconds of aligned, identical seeded input, both palettes have identical
  CV pitches and gate timing on every sample, matching scales, chords, note/rest
  counts, while their audio differs.
- The isolated wood voice stays within the existing note ceiling, with a shorter
  measured tail than Bells, plus a 60-second Wood stress run using correlated
  full-scale audio, extreme CVs, full controls and fast beat/commit clocks.

All inherited checks also passed. External-audio reference measurements remain
0.4468 wet/input RMS at default Y and 0.8200 at maximum Y; both jacks match and
the dry endpoint contributes no external audio.

## Audio previews

The 180-second normal Bells preview is byte-for-byte identical to stable 0.4.1's
original fixed-seed render: 150 beats, 214 melody notes, six chord changes,
eight shared-tone carries, four bell rests, four bass rests, four octave answers,
four returns and three mutations.

The Wood preview includes a half-second selection hold and debounced release,
so slightly less music fits in the same wall-clock file: 149 beats and 213 notes.
Its aligned score/CV identity is tested separately above; the startup silence is
not a dropped note. It retains the same other arrangement counts.

| Palette | Channel | Peak, 16-bit | RMS | Mean sample | Hard-clipped samples |
| --- | --- | ---: | ---: | ---: | ---: |
| Bells | Left | 23,136 | −18.74 dBFS | −0.24 | 0 |
| Bells | Right | 24,064 | −18.33 dBFS | −0.26 | 0 |
| Wood | Left | 19,632 | −19.80 dBFS | −0.21 | 0 |
| Wood | Right | 20,832 | −19.19 dBFS | −0.19 | 0 |

Files are generated in `test/out/` and are not tracked. These are host DSP
renders, not physical card recordings. The dispatch optimization preserved both
render files byte-for-byte.

## Compiled ARM timing estimates

Unicorn 2.1.4 / Capstone 5.0.9 executed 100,000 compiled callback samples per
palette, including scales/destinations, manual and automatic changes, full
effects and hot audio. Wood is selected using the actual Down-at-boot path;
a test-only helper verifies which palette the emulated firmware entered.
Estimated framework/IRQ overhead is 734 cycles; budget is 4,000 at 192MHz/48kHz.

| Palette / scenario | Max callback | Max including overhead | Budget used |
| --- | ---: | ---: | ---: |
| Bells / startup | 2,127 | 2,861 | 71.5% |
| Bells / manual changes | 2,620 | 3,354 | 83.9% |
| Bells / automatic changes | 2,571 | 3,305 | 82.6% |
| Wood / selector startup | 430 | 1,164 | 29.1% |
| Wood / manual changes | 2,734 | 3,468 | 86.7% |
| Wood / automatic changes | 2,793 | 3,527 | 88.2% |

The first Wood estimate reached 90.8%; specializing the audio path by fixed
palette reduced it to 88.2% with unchanged audio. The model excludes real DMA/bus
contention. Physical LED6 remains the latched callback timing indicator; verify
both palettes when repeating hardware tests. Adrian reported the hardware tests
passed before this stable promotion.

## Build reproduction

The local build used direct Command Line Tools `make`/`clang++`, explicit CLT
SDK/C++ headers for host checks, and CMake options `-DPICO_TOOLCHAIN_PATH=/opt/homebrew`
and `-DCMAKE_MAKE_PROGRAM=/Library/Developer/CommandLineTools/usr/bin/make`.
Strict metadata validation passes for this stable entry. Source and
firmware redistribution notices are preserved in the bundle.

## Stable promotion verification

A fresh root release build reproduces the tested experimental UF2 byte-for-byte.
The stable filename is `Departures_0.5.0.uf2`; its SHA-256 is unchanged. All
ASan/UBSan host checks, including the palette suite, passed again from the
promoted source. Strict catalogue metadata validation passed with zero errors
and warnings. The 33 tracked files of previous stable 0.4.1 were archived with
every file hash unchanged. The original 0.5 experimental snapshot is also retained.

The upstream submission includes only `uf2/Departures_0.5.0.uf2`. Historical
source and firmware bundles are retained on the fork rather than shipped in the
release; their preservation checks above describe the original promotion.
