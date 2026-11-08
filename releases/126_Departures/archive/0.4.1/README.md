# Departures

**Version 0.4.1 — stable release. Hardware and torture tests reported passed.**

A self-playing ambient ensemble for the Music Thing Modular Workshop Computer:
three breathing chord voices, four expressive bell phrases, a gliding bass,
drifting echoes and stereo reverb. Shared tones carry through harmonic changes;
independent loops, occasional rests and a slow tide keep the piece moving.

CV In 1 now changes the scale instead of transposing. CV In 2 still prepares a
chord destination, and Pulse In 2 commits that chord. All scales share a fixed
**D tonic**. Promoted on 8 November 2026 after Adrian reported both normal and
additional torture tests pass. This stable image has exactly the same firmware
bytes as the tested experiment. Earlier releases and experiments are preserved
in the [archive](archive/README.md).

## Install and quick test

Flash [Departures_0.4.1.uf2](uf2/Departures_0.4.1.uf2)
using BOOTSEL, reset in Middle and listen in stereo. It fits 2MB and 16MB cards.
The [previous stable 0.3.0 UF2](archive/0.3.0/uf2/Departures_0.3.0.uf2) and
[tested 0.4 UF2](archive/experimental/0.4/uf2/Departures_0.4.0-experimental.uf2)
remain available for comparison or rollback.

For a clear first listen, set Y low, patch 0V to CV In 2 and trigger Pulse In 2
for chord I. Patch CV In 1 and compare about 0.36V (D Dorian) with 1.79V (D major).
The pad's F should fade into F-sharp while shared D/A continue. **No Pulse In 2
is needed for a scale change.** New bell/bass entries use the new scale; existing
tails finish naturally.

## Upgrading from earlier versions

CV roles have changed. Compared with 0.3.0, **CV1 selects a scale instead of
transposing**, and **CV2 prepares a chord instead of modulating Activity**.
Compared with 0.4, CV2/Pulse In 2 keep their prepare/commit roles, while CV1 gains
scale selection and the first three LEDs gain the connected-scale display.
Pitch CV outputs, audio routing, knob roles and timing LED6 are retained.

## CV voltage map and scale LEDs

Both CV selectors use seven nominal bins over **0–5V**, with small independent
hysteresis at boundaries. Input calibration is unavailable, so use the centre
voltages for predictable patching. Negative voltages choose the first option;
above 5V chooses the last.

While **CV In 1 is patched**, physical LEDs **1, 2 and 3** have weights **1, 2
and 4**, displaying the active scale's code 1–7. The table names exactly which
LEDs should be on. These are the top pair and middle-left LED, viewed from the front.

| Approx. range | Centre | CV1 scale (root D) | Lit LEDs | CV2 chord degree |
| --- | ---: | --- | --- | --- |
| 0–0.71V | 0.36V | Dorian | 1 | I |
| 0.71–1.43V | 1.07V | Aeolian / natural minor | 2 | II |
| 1.43–2.14V | 1.79V | Ionian / major | 1 + 2 | III |
| 2.14–2.86V | 2.50V | Mixolydian | 3 | IV |
| 2.86–3.57V | 3.21V | Lydian | 1 + 3 | V |
| 3.57–4.29V | 3.93V | Phrygian | 2 + 3 | VI |
| 4.29–5V | 4.64V | Locrian | 1 + 2 + 3 | VII |

The two inputs are independent: the row selected by CV1 need not match the row
selected by CV2. A chord's actual notes/quality come from the active scale. For
example, CV2 at 0V selects chord I: D minor in Dorian, D major in Ionian, or
D diminished in Locrian. CV2 at 2.5V selects degree IV of whichever scale CV1 chose.

Unplugging CV1 returns to **Dorian** and restores the three pad-breath LEDs.
Unplugging CV2 resumes automatic chord progression; Pulse In 2 then advances the
normal next chord. CV1 no longer provides semitone/key transposition in this build.

## How the two changes behave

- **CV1 acts directly:** the control loop selects the scale, then shared pad
  pitches carry through and changed pitches fade down, retune at zero transition
  gain and fade up. A scale change does not restart the beat clock or 24-beat
  chord timer. It works in Hold and with a stopped external clock.
- **CV2 prepares only:** changing it never selects a new chord by itself. Its
  degree is sampled at the Pulse In 2 rising edge, then committed with the same
  shared-note transitions. A later CV2 change affects the next commitment.
- **CV2 patched:** automatic chord changes pause, leaving the current degree
  under commit control. Scale changes from CV1 still revoice that degree.
- **Same chord recommit:** does not restart a pad fade. Tap Z Down retains its
  normal sequential next-chord action, including with CV2 patched.
- **Bass and bells:** follow the active scale on subsequent entries; existing
  notes and effects tails continue. The 0.2.2 quieter bell mix and stronger
  external-audio send are preserved.

## Controls and I/O

| Control | Function |
| --- | --- |
| Main | Activity and internal beat rate, approximately 36–82 BPM. |
| X | Colour, including bell brightness. |
| Y | Stereo space blend and tails; fully left stays dry. |
| Z Middle | Phrase evolution; automatic chord progression only when CV2 is unpatched. |
| Z Up | Hold automatic harmony, mutations and new rests/answers. External scale changes and commits still work. |
| Tap Z Down | Advance one chord in the normal sequence. |
| CV In 1 | Select one of seven fixed-D scales directly, with scale LED feedback. |
| CV In 2 | Prepare a chord degree I–VII in the active scale. |
| Pulse In 1 | External beat clock; unplug to resume internal timing. |
| Pulse In 2 | Commit the CV2 degree, or next chord if CV2 is unpatched. |
| Audio In 1/2 | Summed mono into stereo effects, wet only. |
| Audio Out 1/2 | Stereo ensemble and effects. |
| CV Out 1 / Pulse Out 1 | Latest bell pitch / melody trigger; simultaneous notes merge. |
| CV Out 2 / Pulse Out 2 | Bass target pitch / bass trigger. |

Pitch outputs remain 1V/oct with 0V = C2 (MIDI 36), calibrated where available.
Triggers are 10ms after approximately 1ms of pitch settling. A stopped patched
beat clock pauses new notes/rest/answer progression; scale/chord transitions,
breaths, tide and effects continue. Pulse inputs arm after 20ms of connection and
ignore events closer than 50ms. Unpatched CV1 means Dorian; unpatched CV2 restores
normal automatic harmony. Unpatched audio inputs contribute zero.

## LED positions

```text
1 2
3 4
5 6
```

With CV1 patched, 1–3 show the scale code above. Otherwise they show pad breaths.
LED 4 stays melody activity; LED 5 stays bright Hold / dim external clock; LED 6
stays the latched callback timing warning until reset. Scale selection never
uses or masks LED 6. The firmware indices remain 0–5.

## Build and checks

The release contains source, licenses, tests, build files, firmware and checksums:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPICO_BOARD=pico
cmake --build build -j2
make -C test check preview
python3 tools/package_release.py
```

Host ASan/UBSan checks cover all 49 scale/chord combinations, sounding pitch
membership, shared-tone fades, selector hysteresis, Hold, CV unplug fallbacks,
LED codes, unchanged chord timing and inherited bell/effects/clock behaviour.
The default fixed-seed render has both CVs unpatched for comparison with 0.4.

Optional ARM timing estimate, with separately installed Unicorn and Capstone:

```sh
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DDEPARTURES_BENCH=ON
cmake --build build-bench -j2
python3 test/bench.py build-bench/Departures.elf
```

See [BUILD_REPORT.md](BUILD_REPORT.md), [TEST_PROTOCOL.md](TEST_PROTOCOL.md),
[CHANGELOG.md](CHANGELOG.md) and [NOTICE.txt](NOTICE.txt). The host checks and
fresh stable source build pass. Adrian reported all normal and additional
hardware torture tests pass on 8 November 2026. Exact durations and instrumented
hardware timing measurements were not supplied. The catalogue metadata lists
only the current stable UF2.

## Versions and future work

Earlier firmware, source and original test records are in
[archive/README.md](archive/README.md). [experimental/README.md](experimental/README.md)
tracks experimental status; the completed 0.4 and 0.4.1 experiments are archived.
[FUTURE_PLANS.md](FUTURE_PLANS.md) records the proposed wood-chime ensemble and
Z-down startup palette selector. Those sound-selection features are not implemented.

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
