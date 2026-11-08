# Departures 0.2.2-experimental — build report

Built 7 November 2026 in `126_Departures/experimental/0.2/` on the existing
`codex/departures-prototype` branch. The stable 0.1.0 files remain unchanged.
This is a new hardware listening image; the 0.1.0 hardware pass is not inherited.

## Firmware

- RP2040, `PICO_BOARD=pico`, 2MB flash layout; fits 2MB and 16MB program cards.
- Pico SDK 2.3.0, revision `98a542c1a62fb549ffb5d66a3e5892b06276b670`.
- ARM GNU Toolchain 15.2.Rel1, GCC 15.2.1; C++17, Release, application `-O3`.
- Unchanged ComputerCard 0.4.0 hardware framework.
- 192MHz, 1.15V core; RAM execution; crystal startup multiplier 64.
- Linker allocation: FLASH 101,828 bytes; RAM 107,304 of 262,144 bytes;
  SCRATCH_Y stack 2,048 bytes. No runtime flash writes or USB stdio.
- Packaged UF2: `uf2/Departures_0.2.2-experimental.uf2`, 203,776 bytes,
  398 validated RP2040 blocks. Payload matches `build/Departures.bin`.
- SHA-256: `aafa227625ec5d1b8fe590094a4a5b77da8db241153bb8d06b5c4ea0286b60bb`.
- Checksum and redistribution notices accompany the UF2.

## Functional checks

Apple Clang 21 host tests with AddressSanitizer and UndefinedBehaviorSanitizer
passed. Original clock, switch, transposition, trigger, deterministic playback,
full-scale input and effects-decay checks were retained. New checks verify:

- Shared F/A tones through D minor to F major retain their exact pitches,
  oscillator phases and independent breath cycles.
- Changing tones switch pitch only at zero transition gain, and repeated chord
  requests during a fade settle at the latest destination.
- At most one bell loop or the bass rests at once. A resting loop contributes
  no note events, and bass rests suppress new bass events.
- Hold prevents new rests and automatic chord changes; existing rests finish.
- The tide continues during Hold and clock stop. A patched stopped clock still
  stops beat-driven note/rest progression; Y fully left stays dry.
- A three-minute accelerated-clock run observed 90 shared-tone carries,
  35 bell rests and 36 bass rests; sampled tide ranged from −24,478 to +29,771.
- The two-minute extreme-input stress run produced 2,816 melody strikes and
  479 chord changes, with peak 2,027/2,047 and no sanitizer failure. This is an intentional
  extreme-input check, not a measured hardware headroom specification.

## Audio render

A deterministic 180-second stereo render at default panel settings produced
150 beats, 214 bell strikes, six chord changes, eight shared-tone carries,
four bell-loop rests and four bass rests. File:
`test/out/Departures_0.2.2-experimental.wav` (generated, not tracked).

| Channel | Peak, 16-bit | RMS | Mean sample | Hard-clipped samples |
| --- | ---: | ---: | ---: | ---: |
| Left | 23,664 | -18.43 dBFS | -0.24 | 0 |
| Right | 24,368 | -18.03 dBFS | -0.21 | 0 |

This is synthetic engine output, not a recording of the physical card.

## ARM timing estimate (0.2.1 baseline)

This estimate was run for 0.2.1; it was not rerun for the 0.2.2 mix-only
adjustment. Unicorn 2.1.4 / Capstone 5.0.9 executed 100,000 callback samples, exercising
startup, fast clocks, full effects, full-scale audio, transposition changes,
manual chord changes and automatic chord changes. Approximate framework/IRQ
overhead: 731 cycles. Budget: 4,000 cycles at 192MHz / 48kHz.

| Scenario | Max callback | Max including overhead | Budget used |
| --- | ---: | ---: | ---: |
| Startup | 2,051 | 2,782 | 69.5% |
| Manual chord changes | 2,443 | 3,174 | 79.3% |
| Automatic chord changes | 2,451 | 3,182 | 79.5% |

Removing software division from the bounded scale-degree and pitch-class
calculations reduced the first experimental worst estimate from 90.8% to 79.5%.
Control work remains staggered away from echo/reverb updates. The model excludes
real bus/DMA contention; the on-card timing LED and hardware checks remain needed.

## Build environment

The local build used the installed Command Line Tools' direct `make` and
`clang++` binaries, with the CLT SDK and C++ headers explicitly selected for
host checks. CMake used `-DPICO_TOOLCHAIN_PATH=/opt/homebrew` and
`-DCMAKE_MAKE_PROGRAM=/Library/Developer/CommandLineTools/usr/bin/make`.

Both experimental and stable `info.yaml` files pass the strict repository
validator. The release packager checks block magic, family, size, numbering,
flash addresses and payload identity before copying the image and notices.

## 0.2.1 external-input gain correction

The first hardware listening report found external audio too quiet. The
original x2 external send was small relative to the internal voice/effects
range and final /8 output scaling. The send is now x8, a 12.04dB increase.
No dry path was added, and the ensemble's gain is unchanged.

A paired-engine regression check subtracts a no-input run from an identically
seeded run receiving a mono 220Hz sine at approximately 0.75V peak. It measures
steady-state stereo wet RMS over seconds 6–12, with the beat clock patched and
stopped. This is a host DSP measurement, not a hardware voltage measurement.

| Y setting | 0.2.0 wet/input RMS | 0.2.1 wet/input RMS |
| --- | ---: | ---: |
| 2300 (default) | 0.1118 / −19.03dB | 0.4468 / −7.00dB |
| 4095 (maximum) | 0.2051 / −13.76dB | 0.8200 / −1.72dB |
| 0 (dry endpoint) | 0 | 0 |

Both input jacks produce the same measured level. The test is part of
`make -C test check`. ASan/UBSan host checks and the 100,000-sample ARM timing
estimate were rerun successfully for this build; the timing maxima above are
unchanged. The 180-second no-input render is byte-identical to 0.2.0, confirming
that the autonomous ensemble output has not changed. The original 0.2.0 UF2 is
retained alongside 0.2.1 and both checksums are listed in `uf2/SHA256SUMS.txt`.
The stable 0.1.0 release remains byte-for-byte unchanged.

## 0.2.2 bell balance adjustment

Bell mix scaling changes from one third to one quarter, reducing amplitude
by 25% (−2.50dB) before stereo panning and the shared effects send. Bell synthesis,
note decisions and envelope durations are unchanged. Pad, bass and external-audio
mix gains are unchanged. Existing ASan/UBSan and external-input level checks pass;
the three-minute render has no hard-clipped samples. Previous experimental UF2s
remain alongside the new image for comparison, with all checksums retained.
