# Departures 0.5.1 — build report

Promoted 8 November 2026 after Adrian reported the pad balance sounds better
and hardware tests pass. Stable 0.5.1 preserves the tested experimental UF2 bytes
exactly. Previous stable 0.5.0 and the original 0.5.1 experiment remain preserved
on the fork; see [the archive index](archive/README.md). Only the current stable
UF2 is included in this release.

## Change

The only DSP change is the pad audio mix divisor, from 7 to 9: pad contribution
is 7/9 of its previous level, or −2.18 dB before integer rounding. This applies
to both Bells and Wood Chimes. Melody and bass gains, breathing envelopes,
transitions, tide, seeded musical state, output CV/gates, controls and effects
parameters are unchanged. The effects receive less pad energy; the complete
ensemble's level reduction therefore differs from the pad-only reduction.

## Firmware

- RP2040 / `PICO_BOARD=pico`; fits 2MB and 16MB program cards.
- Pico SDK 2.3.0; ARM GNU GCC 15.2.1; C++17 Release with application `-O3`.
- ComputerCard 0.4.0 unchanged; RAM execution at 192MHz / 1.15V; USB stdio off.
- FLASH 106,788 bytes; RAM 112,264 of 262,144 bytes; core stack 2,048 bytes.
- `uf2/Departures_0.5.1.uf2`: 214,016 bytes, 418 RP2040 blocks.
- SHA-256: `cd740bc07dec8ee79efdab97f7170c431e5f1912f46a328628cd8b5794f9f064`.
- Packaging validates all UF2 headers, family, numbering, addresses and size,
  then compares payload to `build/Departures.bin`. Firmware notices accompany it.

## Host checks and previews

All inherited checks pass with AddressSanitizer and UndefinedBehaviorSanitizer:
engine stress, clocks, shared-note transitions, phrasing, destinations, all 49
scale/chord combinations, scale LED codes, startup palette choice and Wood stress.
Stress peak is 1,988 of 2,047 output units. The external-input wet/output reference
ratios are 0.4465 at Y=2300 and 0.8198 at Y=4095 for either jack; Y=0 is silent.
The audio routing and send gain are unchanged; the shared nonlinear mix accounts
for the small difference from previous reference measurements.

Both 180-second previews preserve the stable render's arrangement counts. Bells:
150 beats, 214 melody notes, six chord changes; Wood: 149 beats, 213 notes and
six changes because its preview includes the half-second startup-selection hold.
Both have eight shared-tone carries, four melody rests, four bass rests, four
octave answers, four returns and three mutations.

| Palette | Channel | Peak, 16-bit | RMS | Hard-clipped samples |
| --- | --- | ---: | ---: | ---: |
| Bells | Left | 20,384 | −19.85 dBFS | 0 |
| Bells | Right | 22,208 | −19.58 dBFS | 0 |
| Wood | Left | 16,960 | −21.27 dBFS | 0 |
| Wood | Right | 19,024 | −20.75 dBFS | 0 |

These are fixed-seed host renders, not hardware recordings. WAVs and generated
build/test outputs are ignored. Strict metadata validation passes with zero
errors and warnings for the stable entry.

## Reproduction

See README for ordinary build commands. Local builds use
`-DPICO_TOOLCHAIN_PATH=/opt/homebrew` and
`-DCMAKE_MAKE_PROGRAM=/Library/Developer/CommandLineTools/usr/bin/make`.
Host checks use the direct CLT clang++ executable with explicit macOS SDK/C++
headers. Optional timing builds add `-DDEPARTURES_BENCH=ON`, then run
`test/bench.py build-bench/Departures.elf` with and without `--wood`.

## Compiled ARM timing estimates

Unicorn 2.1.4 / Capstone 5.0.9 ran 100,000 compiled callback samples per palette.
The palette helper confirmed the actual startup choice. Estimated framework/IRQ
overhead is 734 cycles; sample budget is 4,000 cycles at 192MHz/48kHz.

| Palette / scenario | Max callback | Including overhead | Budget used |
| --- | ---: | ---: | ---: |
| Bells / startup | 2,127 | 2,861 | 71.5% |
| Bells / manual changes | 2,620 | 3,354 | 83.9% |
| Bells / automatic changes | 2,571 | 3,305 | 82.6% |
| Wood / selector startup | 430 | 1,164 | 29.1% |
| Wood / manual changes | 2,734 | 3,468 | 86.7% |
| Wood / automatic changes | 2,793 | 3,527 | 88.2% |

The maxima match stable 0.5.0. This is a timing model and excludes real DMA/bus
contention. Physical LED6 remains the latched timing indicator. Adrian reported
the hardware tests passed and preferred the new balance before stable promotion.

## Stable promotion verification

The tested experiment is retained at fork commit `24edd4b7`. Stable uses the
filename `Departures_0.5.1.uf2` with the same SHA-256. The release includes exactly
one UF2 and one current `info.yaml`; historical firmware and experimental bundles
remain on the fork rather than being copied into the upstream submission.

A fresh build from the promoted stable source reproduces the tested UF2
byte-for-byte. All ASan/UBSan host checks passed again from the stable source;
strict metadata validation returned zero errors and warnings. README links,
firmware notices and the single-UF2 release inventory were verified.
