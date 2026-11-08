# Departures 0.5 — Bells / Wood Chimes

**0.5.0-experimental — hardware listening build.**

A separate sound-palette experiment based on stable Departures 0.4.1. Choose the
original Bells ensemble or warmer Wood Chimes at startup. The scale/chord CVs,
phrasing, tide, bass, effects and pitch/trigger outputs keep their established roles.
[Stable 0.4.1](../../README.md) and its firmware remain unchanged.

## Install and choose a sound

Flash [Departures_0.5.0-experimental.uf2](uf2/Departures_0.5.0-experimental.uf2)
using BOOTSEL. It fits both 2MB and 16MB program cards.

1. **Hold Z Down while powering on or pressing reset**, and keep holding it.
   After the panel settles, the sound-choice LEDs appear. Audio is muted while
   selecting; incoming musical triggers are ignored.
2. Turn **Main clearly left of noon for Bells**, or **right of noon for Wood
   Chimes**. A small centre deadband prevents jitter once a choice is made.
3. **Release Z to confirm and start.** The normal fade-in follows. Main returns
   to Activity, so adjust it to your preferred playing position after release.

| Startup choice | LEDs lit while holding Z Down |
| --- | --- |
| Bells | Physical 1 + 3: left column, top two rows |
| Wood Chimes | Physical 2 + 4: right column, top two rows |

Physical LED5 is off in the selector. **LED6 remains the timing warning** and
is never used as a sound-choice indicator. After release, normal LED roles return,
including CV1 scale codes on LEDs 1–3 when patched.

**Normal boot without holding Down always selects Bells**, regardless of Main's
position. The choice lasts until reset and is not saved to flash. During normal
play, tapping Down still advances a chord; it does not reopen sound selection.
The startup gesture is consumed and does not advance the first chord.

## What Wood Chimes changes

- The four phrase voices use a rounded fundamental with a rapidly damped upper
  resonance, rather than the original sustained FM bell spectrum. Their body
  rings for less time, with a more percussive, woody attack.
- Each loop has a slightly different resonance relationship. X controls strike
  brightness; its strongest effect is heard on new strikes.
- The pad has less upper-harmonic emphasis for a warmer accompaniment. X still
  changes its colour. The bass voice and its level are unchanged.
- The quieter one-quarter melody mix, stored accents, octave answers, note
  mutations, rests, harmony, tide and external-input send are retained.

Bells retains the accepted 0.4.1 sound. At the same seed/settings, the normal
Bells host render is byte-identical to stable 0.4.1. Both palettes use the same
seeded score, CV pitches and trigger timing when their playback starts are aligned.

## Compare fairly

Use the same Main/X/Y positions **after confirming**, the same CV patch and a
few minutes of listening for each palette. Start X around noon and Y low enough
to hear the attacks, then add space. A shorter dry wood ring does not mean notes
or triggers have been removed. Each hardware reset still chooses a new seed;
the fixed-seed host previews provide a controlled musical comparison.

Rollback: [stable Departures_0.4.1.uf2](../../uf2/Departures_0.4.1.uf2).
This new image still needs hardware testing; watch physical LED6 during stress tests.

## Playing controls and jacks

| Control | Function during playback |
| --- | --- |
| Main | Activity and internal beat rate, approximately 36–82 BPM. |
| X | Palette colour: pad harmonics and new note/strike brightness. |
| Y | Stereo space blend and tails; fully left stays dry. |
| Z Middle | Phrase evolution; automatic harmony only when CV2 is unpatched. |
| Z Up | Hold automatic harmony, mutations and new rests/answers; external scale/chord actions still work. |
| Tap Z Down | Advance one chord in the normal sequence. |
| CV In 1 | Direct scale selection, seven fixed-D scales over nominal 0–5V. |
| CV In 2 | Prepare chord degree I–VII within the current scale. |
| Pulse In 1 | External beat clock; unplug to resume internal timing. |
| Pulse In 2 | Commit CV2's edge-sampled degree, or next chord when CV2 is unpatched. |
| Audio In 1/2 | Summed mono into stereo effects, wet only. |
| Audio Out 1/2 | Stereo ensemble and effects. |
| CV Out 1 / Pulse Out 1 | Latest melody pitch / trigger. Simultaneous notes merge. |
| CV Out 2 / Pulse Out 2 | Bass target pitch / trigger. |

Output tuning remains 1V/oct, 0V = C2 (MIDI36), calibrated where available.
Triggers last 10ms after approximately 1ms of pitch settling. The selected palette
changes internal audio only, not the pitch/gate amplitudes or the CV roles.

## CV1 scale codes and physical LEDs

```text
1 2
3 4
5 6
```

During playback, CV1 patched gives LEDs 1/2/3 weights 1/2/4. With CV1 unpatched,
Dorian returns and those LEDs show pad breaths. Input ranges are nominal and have
small independent hysteresis; use centre voltages for predictable choices.

| Approx. range | Centre | CV1 scale (D tonic) | Scale LEDs | CV2 degree |
| --- | ---: | --- | --- | --- |
| 0–0.71V | 0.36V | Dorian | 1 | I |
| 0.71–1.43V | 1.07V | Natural minor | 2 | II |
| 1.43–2.14V | 1.79V | Major | 1 + 2 | III |
| 2.14–2.86V | 2.50V | Mixolydian | 3 | IV |
| 2.86–3.57V | 3.21V | Lydian | 1 + 3 | V |
| 3.57–4.29V | 3.93V | Phrygian | 2 + 3 | VI |
| 4.29–5V | 4.64V | Locrian | 1 + 2 + 3 | VII |

The CV inputs are independent. CV1 changes scale directly, including in Hold;
CV2 prepares a degree and Pulse In 2 commits it. CV2 patched pauses automatic
progression; unplugging restores it. LED4 stays melody activity, LED5 stays
Hold/external-clock status, and LED6 stays the latched timing warning. In the
startup selector only, LEDs 1–4 temporarily show the sound choice as above.

## Build and checks

This folder has independent source, licenses, build outputs, tests and firmware:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico
cmake --build build -j2
make -C test check preview
python3 tools/package_release.py
```

Host checks use ASan/UBSan. They retain the 0.4.1 checks and add startup default,
selector mute/debounce, choice hysteresis, boot-gesture isolation, two-palette
score/CV matching, damped wood-voice behaviour and a Wood Chimes stress run.
The preview target generates both three-minute WAVs under `test/out/` (not tracked).
The Wood preview includes a half-second boot-selection hold before playback.

Optional compiled ARM timing estimates require separate Unicorn/Capstone packages:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DDEPARTURES_BENCH=ON
cmake --build build-bench -j2
python3 test/bench.py build-bench/Departures.elf
python3 test/bench.py build-bench/Departures.elf --wood
```

See [BUILD_REPORT.md](BUILD_REPORT.md), [TEST_PROTOCOL.md](TEST_PROTOCOL.md),
[CHANGELOG.md](CHANGELOG.md) and [NOTICE.txt](NOTICE.txt). This experiment is separate
from the stable catalogue download. It uses no samples or external sound banks.

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
