# Departures 0.4 — CV chord destinations

**0.4.0-experimental — hardware test build.**

CV In 2 prepares a chord destination; Pulse In 2 commits it. CV In 1 remains
transposition. The sound, expressive bells, shared-note transitions, rests and
tide come from [stable Departures 0.3.0](../../README.md).

## Install and patch

Flash [Departures_0.4.0-experimental.uf2](uf2/Departures_0.4.0-experimental.uf2)
using BOOTSEL. Reset with Z in Middle, listen in stereo and start knobs near noon.
It fits 2MB and 16MB cards. For rollback or comparison, use the
[stable 0.3.0 UF2](../../uf2/Departures_0.3.0.uf2).

1. Patch a slow or stepped voltage into **CV In 2**. Patching it pauses automatic
   chord changes at the current harmony; moving the voltage prepares a new chord.
2. Send a trigger to **Pulse In 2** to commit that prepared chord. The voltage is
   latched at the rising edge, then the shared-note transition begins within
   about 1ms. A voltage change immediately after the edge cannot alter that commit.
3. Shared pad tones continue; changing pad tones fade and retune. Bells follow
   the new harmony on their next strikes, and the bass on its next scheduled entry.
   Existing sound tails continue naturally. Recommitting the current chord does
   not restart the pad transition.
4. Leave Z Up if you also want phrase mutations and new rests/answers held. Pulse
   In 2 still commits destinations in Hold. Breathing, tide and existing tails move.

## Destination voltages

At zero transposition, seven approximately equal bins over **0–5V** choose the
D Dorian triads below. Values are nominal; input calibration is not available.
Use the suggested centre voltages for easy patching. A small boundary hysteresis
helps resist ADC noise.

| Approximate voltage range | Suggested centre | Destination |
| --- | ---: | --- |
| 0–0.71V | 0.36V | D minor |
| 0.71–1.43V | 1.07V | E minor |
| 1.43–2.14V | 1.79V | F major |
| 2.14–2.86V | 2.50V | G major |
| 2.86–3.57V | 3.21V | A minor |
| 3.57–4.29V | 3.93V | B diminished |
| 4.29–5V | 4.64V | C major |

Negative voltage selects D minor; above 5V selects C major. CV In 1 transposes
these destinations together with the ensemble, approximately 1V/oct, limited to
±1 octave. CV In 2 no longer changes Activity or beat rate in this experiment.

**Fallback:** unplug CV In 2 to resume the original automatic progression.
With CV In 2 unpatched, Pulse In 2 advances the ordinary next chord. Tap Z Down
also keeps its original next-chord action, including while CV In 2 is patched.
A patched but stopped Pulse In 1 clock stops new beat-driven notes; destination
commits can still move the pad harmony. Unplug Pulse In 1 to resume internal time.

## Controls and I/O

| Control | Function |
| --- | --- |
| Main | Activity and internal beat rate, approximately 36–82 BPM. |
| X | Colour; bell accents add stored brightness differences. |
| Y | Stereo space blend and tails; fully left stays dry. |
| Z Middle | Phrase evolution; automatic harmony only while CV In 2 is unpatched. |
| Z Up | Hold automatic harmony, mutations and new rests/answers; commits still work. |
| Tap Z Down | Advance one chord in the normal sequence. |
| CV In 1 | Approximate quantized transposition, limited to ±1 octave. |
| CV In 2 | Prepare one of seven chord destinations; never commits by itself. |
| Pulse In 1 | External beat clock. |
| Pulse In 2 | Commit the edge-sampled CV2 destination, or next chord when CV2 is unpatched. |
| Audio In 1/2 | Sum to mono into stereo effects, with the stronger input send; no dry passthrough. |
| Audio Out 1/2 | Stereo ensemble and effects. |
| CV Out 1 / Pulse Out 1 | Latest bell pitch / melody trigger. Simultaneous bell notes merge. |
| CV Out 2 / Pulse Out 2 | Bass target pitch / bass trigger. |

Pitch outputs remain 1V/oct with 0V = C2 (MIDI 36), calibrated where available.
Triggers last 10ms after approximately 1ms of pitch settling. Pulse inputs arm
after 20ms of detected connection and reject events closer than 50ms. Unpatched
audio/CV inputs contribute zero; unpatched CV2 specifically restores normal harmony.

Physical LEDs, viewed from the front:

```text
1 2
3 4
5 6
```

1–3 show pad voices, 4 melody activity, 5 bright Hold/dim external clock, and
6 a latched callback timing warning until reset. No LED is repurposed to display
the prepared chord. Firmware indices remain 0–5.

## Build and validation

From this standalone experiment, with a local Pico SDK and ARM toolchain:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico
cmake --build build -j2
make -C test check preview
python3 tools/package_release.py
```

Host ASan/UBSan checks include all inherited sound/clock/phrasing tests plus
`test/destinations.cpp`: all seven choices, quiet preparation, edge latching,
hysteresis, recommit behaviour, transposition, Hold and unplug fallback.
The default three-minute render is a fixed-seed comparison with CV2 unpatched.

Optional compiled ARM timing estimate (external Unicorn/Capstone):

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DDEPARTURES_BENCH=ON
cmake --build build-bench -j2
python3 test/bench.py build-bench/Departures.elf
```

See [BUILD_REPORT.md](BUILD_REPORT.md), [TEST_PROTOCOL.md](TEST_PROTOCOL.md),
[CHANGELOG.md](CHANGELOG.md) and [NOTICE.txt](NOTICE.txt). Adrian reported the
hardware tests pass on 8 November 2026. This version remains experimental and
separate from the stable catalogue download.

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

