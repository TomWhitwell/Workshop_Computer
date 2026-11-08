# Departures

**Version 0.3.0 — stable release.**

A self-playing ambient ensemble for the Music Thing Modular Workshop Computer:
three breathing chord voices, four interlocking bell phrases, a gliding bass,
and dark stereo echoes and reverb. It starts playing without a patch and can
lead external voices through melody/bass pitch CVs and triggers.

Promoted to stable on 8 November 2026 at Adrian's request. The release UF2 is
byte-for-byte identical to the original 0.3.0 experimental image; its filename
and release documentation have changed. Earlier versions are preserved in
[archive/](archive/README.md).

## Install

Flash [Departures_0.3.0.uf2](uf2/Departures_0.3.0.uf2) using BOOTSEL, reset with
Z in Middle, and listen to Audio Outs 1/2 in stereo. It fits 2MB and 16MB cards.
Start Main and X around noon and Y low enough to hear the bell attacks. Allow
three to five minutes for phrase returns, occasional octave answers and the tide.
Each reset seeds fresh phrases in D Dorian.

## Musical behaviour

- **Harmony:** the fixed D Dorian progression uses nearby inversions. Shared pad
  notes continue with their oscillator phase and breath intact. Other voices
  fade out, retune at zero transition gain, and fade back in.
- **Phrasing:** four unequal loops carry stored accents, occasional one-round
  octave answers and single-scale-step note mutations. Accents stay within the
  accepted quieter bell ceiling; softer notes are also slightly darker.
- **Arrangement:** a bell loop or the bass occasionally rests, one layer at a
  time, while the pad and existing sound tails continue.
- **Tide:** independent slow cycles move density, brightness, voice strength
  and space. They do not change the underlying beat rate.
- **External sound:** both audio inputs sum to mono into the stereo effects,
  with the stronger input send introduced in 0.2.1. There is no dry passthrough.

The original musical inspiration is Charles Vestal's AMB-1. Departures is an
independent, AI-assisted implementation; its code, sounds and presets are original.

## Controls

| Control | Function |
| --- | --- |
| Main | Activity and internal beat rate, approximately 36–82 BPM. |
| X | Colour; stored accents add small brightness differences to each bell strike. |
| Y | Space blend and tails. Fully left stays dry. |
| Z Middle | Harmony, note mutations, layer rests and occasional octave answers evolve. |
| Z Up | Hold harmony and mutations, and prevent new rests/answers. Existing rests and octave answers finish their round normally. Breaths and tide continue. |
| Tap Z Down | Advance one chord; a held Down advances once, then evolves normally. |

| Jack | Function |
| --- | --- |
| CV In 1 | Approximate 1V/oct quantized transposition, limited to ±1 octave. |
| CV In 2 | Adds Activity and internal beat rate. |
| Pulse In 1 | External beat clock. Stop while patched to stop new beats; unplug to resume internal timing. |
| Pulse In 2 | Advance harmony, including in Hold. |
| Audio In 1/2 | Sum to mono into stereo effects, with the 0.2.1 stronger send; no dry passthrough. |
| Audio Out 1/2 | Stereo ensemble and effects, left/right. |
| CV Out 1 / Pulse Out 1 | Latest bell pitch / melody trigger. Octave answers appear in pitch CV; accent strength is internal audio only. Simultaneous notes merge. |
| CV Out 2 / Pulse Out 2 | Bass target pitch / bass trigger. |

Pitch output remains 1V/oct, 0V = C2 (MIDI 36), using calibration when available.
Triggers are 10ms after approximately 1ms of pitch settling. A resting bass holds
its CV and sends no new triggers; a resting bell loop contributes no events while
other loops continue to drive the shared melody outputs. A stopped patched clock
also pauses phrase/rest/answer progression; breaths, chord fades, tide and effects
continue. Unpatched audio/CV inputs contribute zero.

## LEDs

Physical numbering, viewed from the front, remains:

```text
1 2
3 4
5 6
```

1–3 follow the pad voices; 4 shows melody activity; 5 is bright in Hold or dim
with an external clock connected; 6 latches a callback timing warning until reset.
The code uses indices 0–5.

## Build and checks

This folder has its own source, licenses, vendor header, build outputs and UF2.
From this folder, with a local Pico SDK and ARM toolchain:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico
cmake --build build -j2
make -C test check preview
python3 tools/package_release.py
```

The normal firmware runs from RAM at 192MHz / 1.15V. Host checks use ASan/UBSan;
`test/phrasing.cpp` additionally checks accent persistence, one-step mutations,
single-answer register changes, return-home behaviour, transposition bounds and
Hold. `test/out/` contains the generated, deterministic three-minute preview.

Optional ARM timing estimate, requiring separately installed Unicorn/Capstone:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DDEPARTURES_BENCH=ON
cmake --build build-bench -j2
python3 test/bench.py build-bench/Departures.elf
```

See [BUILD_REPORT.md](BUILD_REPORT.md), [TEST_PROTOCOL.md](TEST_PROTOCOL.md),
[CHANGELOG.md](CHANGELOG.md) and [NOTICE.txt](NOTICE.txt). The catalogue metadata points to this stable firmware only.

## Experiments and plans

The [CV-selected harmony experiment](experimental/0.4/README.md) gives CV In 2 a
new role as a prepared chord destination, committed by Pulse In 2. That behaviour
is exclusive to its experimental UF2; this stable version retains CV2 Activity.
See [FUTURE_PLANS.md](FUTURE_PLANS.md) for the proposed wood-chime ensemble and
startup palette selector, and [archive/README.md](archive/README.md) for older releases.

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
