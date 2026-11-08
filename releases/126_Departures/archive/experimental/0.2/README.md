# Departures 0.2 experiment

**0.2.2-experimental — hardware listening build.**

An independently buildable experiment in shared-note chord transitions,
occasional layer rests and slow overall movement. The stable 0.1.0 source,
firmware and documentation remain in the parent card folder, unchanged.

## Install and listen

Flash [Departures_0.2.2-experimental.uf2](uf2/Departures_0.2.2-experimental.uf2)
using BOOTSEL. It fits both 2MB and 16MB cards. Reset with Z in Middle, listen
to Audio Outs 1/2 in stereo, and start the knobs around noon. Listen for at
least three to five minutes to hear the slow movement and returning layers.

This is new firmware: the 0.1.0 hardware test pass does not cover 0.2. For
rollback, use [the unchanged 0.1.0 UF2](../../uf2/Departures_0.1.0.uf2).
Physical **LED 6, bottom right**, is the latched callback timing warning.
Reset clears it. It measures the callback, not all surrounding interrupt work.

## Bell balance adjustment (0.2.2)

Bells are 25% quieter (approximately −2.5dB) relative to the bass and pads.
Both their direct sound and contribution to the shared effects are reduced.
Bass, pad and external-audio gains are unchanged. The
[0.2.1 image](uf2/Departures_0.2.1-experimental.uf2) remains available for comparison.

## External audio level correction (0.2.1)

The external effects send is now four times stronger (+12dB) than 0.2.0.
Audio In 1 and 2 still sum to mono before the stereo effects, with equal gain
and no dry passthrough. Y fully left stays dry; raising Y reveals the external
sound. Internal ensemble levels and the tide/transition/rest behaviour are
unchanged. The original input/feedback limiter bounds very hot signals.
The earlier [0.2.0 image](uf2/Departures_0.2.0-experimental.uf2) is retained
for comparison.

## What changes

- **Shared notes carry through a chord change.** If the next triad contains
  a sounding pad note, that voice keeps its exact pitch, oscillator phase and
  breath cycle. Other voices fade down over about 256ms, change pitch at zero
  transition gain, then fade up over about 512ms. Nearby inversions keep the
  movement small. Rapid requests retarget the transition rather than restarting
  all three voices. The fixed D Dorian chord sequence is retained.
- **One layer occasionally rests.** A bell phrase can sit out one complete
  round after several active rounds. The bass can skip eight beats after
  several four-beat phrases. Only one of those five layers rests at once; the
  pad continues, and existing notes and effects decay naturally. Resting layers
  contribute no new pitch CV changes or triggers, then return on their phrase grid.
  Other bell loops still drive the shared melody outputs; a bass rest holds
  CV Out 2 and suppresses Pulse Out 2.
- **A slow tide moves the ensemble.** Three cycles of roughly 71, 127 and
  223 seconds gently vary melody density, brightness, pad level, bell strength
  and the space blend. Low tide is softer and more distant; high tide is fuller
  and clearer. The beat rate stays under Main/CV or the external clock.

## Controls and jacks

| Control | Function |
| --- | --- |
| Main | Activity and internal tempo, approximately 36–82 BPM; the tide gently varies density around it. |
| X | Colour; the tide adds a small amount of brightness movement. |
| Y | Space blend and tails; the tide makes quiet passages slightly more distant. Fully left stays dry. |
| Z Middle | Evolve harmony, phrase mutations and occasional rests. |
| Z Up | Hold harmony, phrase mutations and new rest decisions. A rest already underway finishes at its normal boundary. Breathing and the tide continue. |
| Tap Z Down | Advance one chord. Holding Down advances once, then evolves normally. |
| CV In 1 | Approximate quantized 1V/oct transposition, limited to ±1 octave. |
| CV In 2 | Adds activity and internal clock speed. |
| Pulse In 1 | One rising edge per beat. A stopped patched clock stops new beat-driven notes; unplugging resumes internal timing. |
| Pulse In 2 | Advance harmony, including while held. |
| Audio In 1/2 | External sound into the effects only; raise Y to hear it. |
| Audio Out 1/2 | Left/right ensemble and effects. |
| CV Out 1 / Pulse Out 1 | Latest bell pitch / melody trigger. Simultaneous notes merge. |
| CV Out 2 / Pulse Out 2 | Bass target pitch / bass trigger. The internal bass glides; CV steps. |

Pitch outputs use 1V/octave with **0V = C2 (MIDI 36)**, using output calibration
when available. CVs hold the last note during a rest. Triggers last 10ms and
follow the pitch change by about 1ms. A stopped external clock pauses phrase
and rest progression; pad breaths, chord transitions, the tide and tails
continue. Input pulses closer than 50ms are ignored, and inputs arm after
20ms of detected connection. Unpatched audio and CV inputs contribute zero.

## LEDs

Physical numbering, viewed from the front:

```text
1 2
3 4
5 6
```

| LED | Meaning |
| --- | --- |
| 1–3 | The three pad voices, including their transition fades and tide level. |
| 4 | Melody activity. |
| 5 | Bright = Hold; otherwise dim = external clock connected. |
| 6 | Latched callback timing warning; reset clears it. |

The firmware indices are 0–5. No LED is repurposed for the new features.

## Build and validate

The experiment has its own source, vendor header, licenses, build directories
and release packager. From this folder:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico
cmake --build build -j2
make -C test check preview
python3 tools/package_release.py
```

The normal firmware runs from RAM at 192MHz / 1.15V. The application uses fixed
point while playing. Host checks use ASan/UBSan and cover the original controls,
clock and effects behaviour as well as shared-tone continuity, silent pitch
changes, interrupted transitions, single-layer rests, Hold and tide movement.
The preview target writes a deterministic three-minute stereo WAV into
`test/out/`. It is synthetic engine output, not a hardware recording.

Optional ARM timing estimate (requires external Unicorn and Capstone packages):

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DDEPARTURES_BENCH=ON
cmake --build build-bench -j2
python3 test/bench.py build-bench/Departures.elf
```

See [BUILD_REPORT.md](BUILD_REPORT.md), [TEST_PROTOCOL.md](TEST_PROTOCOL.md) and
[CHANGELOG.md](CHANGELOG.md). This experimental image is not added to the
stable card's curated catalogue downloads. No stable 0.1.0 files are modified.

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

