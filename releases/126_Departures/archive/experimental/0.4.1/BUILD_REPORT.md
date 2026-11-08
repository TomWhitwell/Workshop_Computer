# Departures 0.4.1-experimental — build report

Built 8 November 2026 in `experimental/0.4.1/`. The tested 0.4 source/firmware
folder is preserved unchanged, and 0.3.0 remains the stable release. This new
scale-selection image passed the user's reported hardware tests on 8 November 2026.

## Reported hardware result

Adrian reported “tests pass” for 0.4.1 before requesting an additional torture
test. No failures were reported. Individual durations, exact patches and
instrumented measurements were not supplied. Adrian subsequently reported
“tests all pass” after the additional torture-test protocol, and requested stable
promotion. The protocol covered concurrent scale/destination changes, beat/commit
clocks, effects load, Hold, clock stop/repatch and reset. Exact run durations and
instrumented timing measurements were not supplied. The identical firmware is
promoted to stable 0.4.1; this folder preserves its original experimental image.

## Firmware

- RP2040 / `PICO_BOARD=pico`, 2MB layout; fits 2MB and 16MB cards.
- Pico SDK 2.3.0, revision `98a542c1a62fb549ffb5d66a3e5892b06276b670`.
- ARM GNU Toolchain 15.2.Rel1 / GCC 15.2.1; C++17, Release, application `-O3`.
- Unmodified ComputerCard 0.4.0; RAM execution at 192MHz / 1.15V.
- Linker: FLASH 105,592 bytes; RAM 111,080 of 262,144 bytes; 2,048-byte stack
  in SCRATCH_Y. USB stdio disabled; no runtime flash writes.
- UF2: `uf2/Departures_0.4.1-experimental.uf2`, 211,456 bytes, 413 RP2040 blocks.
- SHA-256: `d7b043b61e0f1543d4fc7b0636114d5a60174ed802f5f70bb60420806dfc7a7c`.
- The packager validates every block and matches the payload to
  `build/Departures.bin`. Notices and checksum accompany the firmware.

## Functional checks

Apple Clang 21 ASan/UBSan checks passed, including all inherited audio, clock,
phrasing and destination tests adjusted for CV1's intentional role change.
New `test/scales.cpp` checks:

- All 49 scale/chord combinations produce the requested three-note pad triad
  after settling, not merely arbitrary notes within the scale.
- Subsequent melody events belong to the active scale; new bass entries match
  the chosen scale degree. The tonic stays D rather than transposing the key.
- CV1 changes scales without Pulse In 2, including during Hold; CV2 degree
  selection/commit remains independent.
- CV1 and CV2 use independent selector state and hysteresis.
- All seven scale codes appear on physical LEDs 1/2/3 with weights 1/2/4;
  Hold/clock feedback stays on LED5 and timing LED6 remains reserved.
- Unplugging CV1 restores Dorian and graded pad-breath LED feedback.
- Scale changes preserve the 24-beat automatic chord timer.
- A scale request deferred by a simultaneous commit can be cancelled by the
  next control sample; a stale queued choice is not applied afterward.

Inherited tests retain shared-tone phase/breath continuity, silent retuning,
interrupted transitions, trigger timing, rests, tide, stored accents, octave
answers/returns, gentle mutations, deterministic audio and effects decay.
The two-minute extreme-input test reached 2,002/2,047 with 2,986 melody events
and 486 harmonic changes, including scale retunes, without sanitizer failures.
External-audio test ratios remain 0.4468 at default Y and 0.8200 at maximum Y;
both jacks match and Y fully left contributes no external audio.

## Default audio regression

With CV1 and CV2 unpatched, the fixed-seed 180-second render is byte-identical
to 0.4: 150 beats, 214 melody strikes, six chord changes, eight shared-tone
carries, four bell rests, four bass rests, four octave answers, four returns and
three mutations. This preserves default Dorian behaviour and the accepted mix.
Generated file: `test/out/Departures_0.4.1-experimental.wav`.
It is host DSP output, not a recording of the physical card.

## ARM timing estimate

Unicorn 2.1.4 / Capstone 5.0.9 executed 100,000 compiled callback samples with
rapid jumps across all seven scales, large destination CV changes, manual
commits, automatic changes, full effects and hot audio. It also exercises CV
unplug fallbacks. Estimated framework/IRQ overhead: 731 cycles. Budget:
4,000 cycles at 192MHz / 48kHz.

| Scenario | Maximum callback | Maximum including overhead | Budget used |
| --- | ---: | ---: | ---: |
| Startup | 2,155 | 2,886 | 72.2% |
| Manual changes | 2,640 | 3,371 | 84.3% |
| Automatic changes | 2,599 | 3,330 | 83.2% |

Scale planning has a separate sample slot from the CV quantizers, note strikes
and chord-commit planner. It is deferred if a fresh commit edge would combine
quantization with planning in the same interrupt. Physical DMA/bus contention
is not modelled; LED6 remains the hardware callback timing indicator.

## Reproduction

The local build used direct Command Line Tools `make`/`clang++` and explicit CLT
SDK/C++ headers for host checks, with CMake options
`-DPICO_TOOLCHAIN_PATH=/opt/homebrew` and
`-DCMAKE_MAKE_PROGRAM=/Library/Developer/CommandLineTools/usr/bin/make`.
The experimental metadata passes the strict repository validator. See README
for the full voltage map, physical LED codes and build commands.
