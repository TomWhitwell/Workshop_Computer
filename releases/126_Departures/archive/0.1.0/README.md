# Departures

**Version 0.1.0 — first release, hardware tested.**

A self-playing ambient ensemble for the Music Thing Modular Workshop Computer:
three breathing chord voices, four interlocking bell phrases, a gliding bass,
and dark stereo echoes and reverb. Turn it on and let it find its way.

The musical idea is inspired by Charles Vestal's
[AMB-1](https://github.com/charlesvestal/fm1-amb). Departures is an original,
AI-assisted implementation for Computer; no AMB-1 source, sounds or presets
are used. Its harmony, phrase generation, synthesis and effects are written
for this card.

## Install and play

Flash [Departures_0.1.0.uf2](uf2/Departures_0.1.0.uf2)
onto your program card using BOOTSEL. It fits both 2MB and 16MB cards.
Reset with Z in Middle. Listen to Audio Out 1/2 as left/right, with Main and
X around noon and Y slightly above noon. It fades in gently; give the pad a
few seconds to breathe. Each reset starts new phrases in D Dorian.

This build has been cross-compiled, exercised with host sanitizers and rendered
offline. On 7 November 2026, Adrian reported that all suggested hardware torture
tests passed; see [BUILD_REPORT.md](BUILD_REPORT.md). Physical LED 6 (bottom right,
code index 5) latches on if the callback reaches 18 microseconds; reset clears it.
It measures the callback, excluding the surrounding hardware interrupt work.
If it lights, report the patch and settings.

## Controls

| Control | Function |
| --- | --- |
| Main | Activity: sparse, slow phrases to fuller, faster movement. Internal tempo approximately 36–82 BPM. |
| X | Colour: rounded, soft voices to brighter harmonics and glassier bells. |
| Y | Space: dry ensemble to a more distant blend of echo and reverb, with longer tails. |
| Z Middle | Evolve: harmony moves every 24 beats; phrases occasionally change one note. |
| Z Up | Hold harmony and phrase mutations. Existing phrases, density, timbre and effects keep moving. |
| Tap Z Down | Advance one chord immediately. Holding Down advances once, then continues evolving normally. |

Y fully left is dry (the small bottom deadband accommodates real pots).
Holding harmony doesn't freeze the audio or the reverb. A switch or external
advance can move the harmony while held. Changes spread through the pad as
each voice reaches the quiet part of its breath; existing bells ring out.

## Patch points

| Jack | Function |
| --- | --- |
| Audio In 1 / 2 | Optional external sound into the stereo space; there is no direct dry passthrough. Y must be raised to hear it. |
| CV In 1 | Quantized transposition, approximately 1V/octave, limited to ±1 octave. Positive raises pitch. Input accuracy depends on the ADC; this is not a calibrated precision pitch input. |
| CV In 2 | Adds to Activity. Positive makes the ensemble busier and the internal clock faster. |
| Pulse In 1 | External beat clock: one rising edge = one beat. When patched, replaces the internal clock. A stopped patched clock stops new beats; unplugging resumes the internal clock. |
| Pulse In 2 | Advance one chord on a rising edge, including in Hold. |
| Audio Out 1 / 2 | Stereo ensemble and effects, left/right. |
| CV Out 1 | Most recently struck melody pitch; simultaneous bell notes share one CV/trigger event. |
| CV Out 2 | Bass target pitch, stepped, while the internal bass glides. |
| Pulse Out 1 / 2 | Melody / bass triggers, 10ms, delayed about 1ms after the pitch change for CV settling. |

**Output tuning:** 1V/octave, **0V = C2 (MIDI 36)**. Uses the Computer's output
calibration if present, otherwise the framework's nominal calibration. Tune
an external oscillator to the same reference or offset it by octaves as desired.
Melody and bass CVs hold their last note between events. Transposition reaches
the melody and bass on their next notes, and the pad at its next control update.

Pulse inputs are armed after a connection has been detected for 20ms, and
events closer than 50ms are ignored. Repatching can still depend on the hardware
jack detector's response; connect the clock before starting it when practical.
Unpatched audio and CV inputs contribute zero.

Try CV Out 1 to a SineSquare pitch input and Pulse Out 1 to a Slope trigger,
then use the Slope to open a VCA. The analogue voice will follow the bell part.

## LEDs

Numbered 1–6, left to right from the top, viewed from the front:

```text
1 2
3 4
5 6
```

| LED | Meaning |
| --- | --- |
| 1 / 2 / 3 | The three independent pad breaths. |
| 4 | Melody activity. |
| 5 | Bright: harmony held. Dim: external clock connected. Off: evolving internally. |
| 6 | Latched callback timing warning; reset clears it. |

Firmware indices are 0–5; physical LED 6 is code index 5.

## Musical design

The pad cycles through related D Dorian triads with nearby inversions. Its
voices breathe over approximately 7, 11.3 and 15.6 seconds, taking a new chord
note near silence. The four bell phrases are 11, 19, 29 and 37 beats long.
Their pitches and density thresholds are stored, so recognisable fragments
return while their relationship changes. Higher Activity reveals more notes
and allows more occasional mutations. Up stops those mutations.

The bass renews every four beats, following the chord root with a short glide.
Bell retriggers crossfade the old oscillator for 5ms, and bass attacks are
smoothed. The two-head echo runs at 12kHz with slowly drifting taps near 1.6s
and 1.175s. A damped four-line feedback network runs at 24kHz for the reverb;
synthesis and output run at 48kHz. Feedback is bounded, and output DC is removed.

This release deliberately starts with one scale and one ensemble. Scale
selection, saved settings, MIDI and a web editor are future work.

## Build and checks

Requires a local Pico SDK, CMake and ARM GNU toolchain:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico
cmake --build build -j2
make -C test check preview
python3 tools/package_release.py
```

`build/Departures.uf2` is the build output. `test/out/` holds a deterministic
90-second stereo WAV render; it is not a hardware recording. Host checks cover
clock replacement and stop/resume, harmony hold/advance, transposition, trigger
width, deterministic playback, full-scale inputs and effects decay, using ASan
and UBSan. The engine uses fixed-point arithmetic while playing; floating point
only generates lookup tables before audio starts. It runs from RAM at 192MHz,
with a 1.15V core setting following the existing ambient-card setup.

An optional ARM timing estimate uses Unicorn and Capstone:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DDEPARTURES_BENCH=ON
cmake --build build-bench -j2
python3 test/bench.py build-bench/Departures.elf
```

The estimate does not model all bus contention or substitute for hardware
timing. See [BUILD_REPORT.md](BUILD_REPORT.md) for this build's results and
[NOTICE.txt](NOTICE.txt) for source and firmware redistribution notices.

## Credits and licensing

Departures was developed by **Adrian Vos (soveda)** with AI assistance from Codex.
The original card application is MIT licensed; see [LICENSE](LICENSE).

- **Charles Vestal — AMB-1:** musical inspiration for an ensemble of breathing
  voices, unequal phrase loops and evolving harmony in an ambient space.
  [AMB-1](https://github.com/charlesvestal/fm1-amb) is credited as inspiration;
  its source code, presets, sounds, interface and firmware are not included.
- **Chris Johnson — ComputerCard 0.4.0:** the unchanged hardware framework,
  copyright 2024–2026, MIT. The header and license are in
  [vendor/ComputerCard](vendor/ComputerCard/).
  [Upstream ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard).
- **Matt Allison — Equanimity:** the ARM timing test adapts its cycle model and
  ComputerCard field-offset helper, copyright 2026, MIT. That testing code is
  excluded from the normal firmware. Its full license is preserved in
  [licenses/EQUANIMITY_LICENSE.txt](licenses/EQUANIMITY_LICENSE.txt).
  [Equanimity](https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/112_Equanimity)
  also provided a local reference for ambient-card build and hardware setup.
- **Raspberry Pi and contributors — Pico SDK:** external build dependency and
  source of `pico_sdk_import.cmake`; SDK license preserved in
  [licenses/PICO_SDK_LICENSE.txt](licenses/PICO_SDK_LICENSE.txt).
- **ARM GNU Toolchain, GCC runtime and Newlib contributors:** firmware build and
  runtime support; notices in [licenses/TOOLCHAIN_NOTICES.txt](licenses/TOOLCHAIN_NOTICES.txt).
- **Tom Whitwell, Music Thing Modular and repository contributors:** the Workshop
  Computer platform, examples, contribution conventions and metadata tooling.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the dependency inventory
and [NOTICE.txt](NOTICE.txt) for consolidated redistribution notices. Keep
`uf2/NOTICE.txt` with the firmware when sharing it.

## Release folder

`info.yaml` supplies the catalogue description, controls, all twelve used jacks,
LED meanings and firmware download. `src/`, `vendor/`, `CMakeLists.txt` and the
SDK import helper make the source build self-contained apart from the documented
SDK/toolchain dependencies. `test/` contains host checks, the audio renderer and
ARM timing harness. `uf2/` contains the release firmware, checksum and notices.
`tools/package_release.py` checks the UF2 structure against the build binary,
then refreshes the release firmware, checksum and adjacent notices.

[CHANGELOG.md](CHANGELOG.md) records the release;
[TEST_PROTOCOL.md](TEST_PROTOCOL.md) gives the repeatable hardware checks.
Version 0.1.0 uses the exact binary that passed the prototype tests, under its
release filename. Local build directories and generated test audio are excluded
from the contribution package.
