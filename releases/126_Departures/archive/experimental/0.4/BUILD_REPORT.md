# Departures 0.4.0-experimental — build report

Built 8 November 2026 in `experimental/0.4/`. Stable 0.3.0 is a separate,
byte-identical promotion of the original 0.3 experimental UF2. Previous versions
are preserved in `archive/`. On 8 November 2026, Adrian reported that the hardware
tests passed for this CV-destination image.

## Hardware result

Adrian reported “tests pass” after trying the 0.4 image and its chord-selection
behaviour. No failures were reported. This records the user's test result;
individual durations, exact patch settings and instrumented timing measurements
were not supplied. Firmware remains 0.4.0-experimental; stable remains 0.3.0.

## Build and package

- RP2040 / `PICO_BOARD=pico`, 2MB layout; fits 2MB and 16MB cards.
- Pico SDK 2.3.0, revision `98a542c1a62fb549ffb5d66a3e5892b06276b670`.
- ARM GNU Toolchain 15.2.Rel1 / GCC 15.2.1; Release, C++17, application `-O3`.
- Unmodified ComputerCard 0.4.0; RAM execution at 192MHz / 1.15V.
- Linker: FLASH 103,948 bytes; RAM 109,432 of 262,144 bytes; 2,048-byte core
  stack in SCRATCH_Y. USB stdio disabled; no runtime flash writes.
- UF2: `uf2/Departures_0.4.0-experimental.uf2`, 208,384 bytes, 407 validated
  RP2040 blocks, payload matching `build/Departures.bin`.
- SHA-256: `ee72ee7de4a1f2e0c816d3117ab73e8563298d9861f804e6f6d0ec405d52bc4f`.
- Full redistribution notices and checksum accompany the UF2.

## Functional validation

Apple Clang 21 ASan/UBSan host checks passed. New destination checks cover:

- All seven nominal voltage bins prepare a destination without committing it.
- Pulse In 2 commits the edge-latched selection, including in Hold. A subsequent
  voltage change before the scheduled planning slot does not change that commit.
- Small boundary excursions are rejected by hysteresis. Negative and above-span
  voltages select the lowest/highest chord respectively.
- Recommitting the same harmony does not restart the pad transition.
- CV1 still transposes by the expected positive/negative octave independently.
- Rapid commits through all seven chords work at both transposition extremes,
  while octave answers and beat-driven note generation continue in range.
- CV2 voltage has no effect on Activity/tempo. Low and high prepared voltages
  produce sample-identical music without commit edges.
- Patching CV2 pauses automatic harmony changes; unplug restores progression.
  With CV2 unplugged, Pulse In 2 keeps the normal next-chord action.

Inherited engine, external-input and bell-phrasing checks also passed: shared-tone
continuity, zero-gain retuning, clock stop/unplug, trigger timing, deterministic
playback, rests, tide, accents, octave returns, gentle mutations and effects decay.
The two-minute extreme-input run reached 1,982/2,047 with 2,986 melody events
and 479 chord changes, without sanitizer failures. External-input RMS remains
0.4468 at default Y and 0.8200 at maximum Y for the existing modest mono sine
check. Both jacks match; the dry endpoint contributes no external audio.

## Regression audio

With CV2 unpatched, the fixed-seed 180-second render is byte-for-byte identical
to the original 0.3 render: 150 beats, 214 melody strikes, six chord changes,
eight shared-tone carries, four bell rests, four bass rests, four octave answers,
four returns and three mutations. It has no hard-clipped samples.
Generated file: `test/out/Departures_0.4.0-experimental.wav`.
This is host DSP output, not a hardware recording.

## ARM timing estimate

Unicorn 2.1.4 / Capstone 5.0.9 executed 100,000 compiled callback samples, including
large CV2 jumps with manual commits, CV1 transposition, full effects, hot audio,
and unpatched automatic progression. Estimated framework/IRQ overhead: 731 cycles.
At 192MHz/48kHz the sample budget is 4,000 cycles.

| Scenario | Maximum callback | Maximum including overhead | Budget |
| --- | ---: | ---: | ---: |
| Startup | 1,932 | 2,663 | 66.6% |
| Manual commits | 2,509 | 3,240 | 81.0% |
| Automatic changes | 2,498 | 3,229 | 80.7% |

Commit quantization and chord planning are separated if the trigger lands exactly
in the planning slot. Real bus/DMA contention is not modelled; physical LED 6 is
still the callback timing indicator for hardware validation.

## Release/archive integrity

The previous 0.1.0 and 0.2 files were checked against their saved byte hashes after
archiving. The original 0.3 experiment was likewise preserved byte-for-byte.
Stable 0.3 source matches that archived source and its packaged firmware has the
same hash as the original 0.3 image. Stable and experimental metadata are validated
separately and advertise their respective firmware only.

## Local build environment

The build used direct Command Line Tools `make`/`clang++`, explicit CLT SDK/C++
headers for host checks, and CMake options `-DPICO_TOOLCHAIN_PATH=/opt/homebrew`
and `-DCMAKE_MAKE_PROGRAM=/Library/Developer/CommandLineTools/usr/bin/make`.
