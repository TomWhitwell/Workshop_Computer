# Sources and attribution

Original project code and documentation: Adrian Vos (soveda), 2026, MIT; see LICENSE.
This grant covers original work, not relicensing of dependencies, measurement data
or the copied upstream directive. The packaged firmware is the user-tested alpha14 build; historical alpha
sections below identify when particular sources were introduced.

## Current source-to-file map

| Source | Local use | Preserved notice |
|---|---|---|
| ComputerCard 0.4.0 / web_interface, Chris Johnson | vendor/ComputerCard/ComputerCard.h, src/usb_descriptors.c; build, core split and editor conventions | vendor/ComputerCard/LICENSE; full header notice |
| WaveSeq / EightMU, Chris Johnson; rppicomidi | vendor/EightMU/EightMU.h (adapted), src/mu_host.h, USB role/configuration; pickup conventions in src/modes.h and src/mu_controls.h | Full MIT notices in EightMU.h; pinned source/change record in vendor/EightMU/SOURCE.md |
| Workshop_BlockAudioCard, Adrian Vos and contributors | Adapted scheduling pattern in src/block_audio.h; raw hardware driver not copied | vendor/Workshop_BlockAudioCard/LICENSE |
| You spin me round, Adrian Vos | Reused values in src/dsp/sine_table.h | vendor/YouSpinMeRound/LICENSE |
| Raspberry Pi Pico SDK | External linked SDK and copied pico_sdk_import.cmake | vendor/PicoSDK/LICENSE.TXT (BSD-3-Clause) |
| TinyUSB / contributors | External linked host/device stack; configuration notice in src/tusb_config.h | vendor/TinyUSB/LICENSE and NOTICE.md; full Ha Thach notice in tusb_config.h |
| Gardner/Martin MIT KEMAR data | vendor/KEMAR/diffuse.zip, src/dsp/hrtf_table_32.h and hrtf_table_64.h; HRTF previews | vendor/KEMAR/SOURCE_TERMS.md; data attribution in generated tables |

WaveSeq source and revision are in vendor/EightMU/SOURCE.md. Its web preset UI and
switch-bank pickup were reviewed as examples, rather than copied wholesale.
Voder was reviewed for motion handling; no Voder source or DSP was copied.
Retain this file, LICENSE and the referenced vendor notices when redistributing.


## ComputerCard 0.4.0

ComputerCard by Chris Johnson, copyright 2024–2026, MIT.
The unmodified header is in vendor/ComputerCard/ComputerCard.h and the upstream
license file is preserved beside it. The header also includes the full MIT notice.
The CMake setup follows ComputerCard's example build conventions; the initial
firmware follows its passthrough example and README (Chris Johnson).
pico_sdk_import.cmake is copied from the same upstream directory; the Raspberry Pi
copyright and BSD-3-Clause notice are preserved in vendor/PicoSDK/LICENSE.TXT.

Source: https://github.com/TomWhitwell/Workshop_Computer/tree/7e2b0416093b397b5512de3f111472bd91084c6e/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard

## Workshop Computer documentation

The development directive is an unmodified reference copy from the Music Thing
Modular Workshop Computer repository, revision 7e2b0416093b397b5512de3f111472bd91084c6e.
Its source does not include a separate author or license notice; this project's
MIT license does not relicense that upstream document. Consult upstream for reuse.
Platform and metadata guidance is credited to the Workshop Computer maintainers,
including Tom Whitwell and Chris Johnson.

Source: https://github.com/TomWhitwell/Workshop_Computer/blob/7e2b0416093b397b5512de3f111472bd91084c6e/Demonstrations%2BHelloWorlds/AI/WORKSHOP_COMPUTER_AI_DIRECTIVE.md
Metadata: https://github.com/TomWhitwell/Workshop_Computer/blob/7e2b0416093b397b5512de3f111472bd91084c6e/documentation/info.yaml.md

## Pico SDK

Builds use an externally installed Raspberry Pi Pico SDK. Its components retain
their own licenses. Preserve applicable SDK and dependency notices when distributing
firmware. The SDK itself is not vendored in this repository.

## Musical inspiration

Neuzeit Instruments Quasar is the inspiration for two-source binaural positioning
and orbital movement. Product reference: https://www.neuzeit-instruments.com/Quasar
No Quasar firmware, manual text, graphics, or transfer-function data is included.
This is an independent project, without an affiliation or endorsement claim.

Clock and regulator setup follows Chris Johnson's ComputerCard NOTES.md
(Programming and optimisation / Clock speed) from the same upstream directory
referenced above: 192 MHz with the regulator at 1.15 V.

## Initial alpha1/alpha2 Twin Orbits DSP and editor

Original fixed-point spatial DSP, orbit logic, configuration contract, editor,
tests and synthetic preview sources: Adrian Vos (soveda), 2026, MIT. The model
combines interaural delay/level, one-pole head-shadow filtering, distance and
feed-forward early reflections. Those initial versions did not incorporate measured HRTF data or third-party
binaural DSP code. Alpha3 and later use the attributed KEMAR data below. The mathematical sine table is reused from this author's
MIT-licensed You spin me round project, introduced in revision
fb56a2144947e8f7e0f47d0094ce1e3c33fa51cc:
https://github.com/soveda/You-spin-me-round/tree/fb56a2144947e8f7e0f47d0094ce1e3c33fa51cc
The project's original MIT notice is retained in vendor/YouSpinMeRound/LICENSE.

USB descriptor code in src/usb_descriptors.c is adapted from Chris Johnson's
ComputerCard web_interface example (MIT; upstream source revision/link above).
Core separation and SysEx transport are based on the same example, with a new
bounded protocol and settings implementation. The full ComputerCard MIT license
is preserved in vendor/ComputerCard/LICENSE. src/tusb_config.h preserves its
original Ha Thach 2019 MIT notice in full. TinyUSB is an MIT-licensed Pico SDK
dependency; the full upstream license is retained in vendor/TinyUSB/LICENSE,
with build-source copyright notices and the pinned revision in vendor/TinyUSB/NOTICE.md.
Firmware also incorporates Pico SDK components under their respective licenses;
the Raspberry Pi BSD-3-Clause notice is in vendor/PicoSDK/LICENSE.TXT.

The alpha operator README, test protocol, protocol description and web editor
include attribution to their hardware/API sources. The copied upstream directive
retains upstream ownership as described above.

Alpha2's three-sample rear spectral filter and stronger rear shadowing are original
analytical DSP by Adrian Vos (soveda), 2026, MIT. The feed-forward pair has transfer
function (1 + z^-3)/2; no measured ear/head responses or external filter coefficients
were copied. Alpha2 comparison noise is generated by the original preview tool.

## Alpha3 measured KEMAR HRTF bank

Derived measurement data: **Bill Gardner and Keith Martin, MIT Media Laboratory**,
copyright 1994. *HRTF Measurements of a KEMAR Dummy-Head Microphone*, MIT Media Lab
Perceptual Computing Technical Report #280, May 1994.
Official source/terms: https://sound.media.mit.edu/resources/KEMAR.html

The dataset permits reuse with author attribution; it is not relicensed under
this project's MIT software license. The unmodified diffuse.zip archive, source
hash, processing details and terms are in vendor/KEMAR. Derived Q14 coefficients
are in src/dsp/hrtf_table.h. Original generator and FIR implementation: Adrian Vos
(soveda), 2026, MIT. Rendered alpha3 previews also use these attributed filters.
No external convolver code is copied. The original analytical DSP statements above
apply to alpha1/alpha2, not to alpha3's measured spectral bank.

Workshop_BlockAudioCard (Adrian Vos, MIT) was reviewed as a possible block transport;
no source was copied into this experiment. Reference:
https://github.com/soveda/Workshop_BlockAudioCard

## Alpha4 block handoff

The fixed-ring, 64-frame/two-block output scheduling pattern in src/block_audio.h
is adapted from Adrian Vos's MIT-licensed Workshop_BlockAudioCard reference,
revision 2802e3692a2e24cb554306ab8c7219ee502650ae (copyright Adrian Vos and
contributors; full notice in vendor/Workshop_BlockAudioCard/LICENSE):
https://github.com/soveda/Workshop_BlockAudioCard/tree/2802e3692a2e24cb554306ab8c7219ee502650ae

This integration keeps Chris Johnson's unmodified ComputerCard 0.4.0 for hardware
service/normalization and uses an original cooperative DSP/USB worker. The
reference's raw ADC/DAC transport is not copied. Original adaptation, TX queue,
diagnostics and tests: Adrian Vos (soveda), 2026, MIT. Earlier "reviewed only"
statements above describe alpha3; alpha4 now adapts the scheduling pattern.

## Alpha5 paired renderer and longer bank

The original optimized paired FIR/shared-distance renderer is by Adrian Vos
(soveda), 2026, MIT. The 32/64-tap generated tables and previews derive from the
same Bill Gardner / Keith Martin, MIT Media Laboratory, copyright 1994 dataset,
with the source terms above retained. No new third-party convolver implementation
or filter dataset is copied. Generator metrics and processing are documented in
vendor/KEMAR; both banks use the same gain scale for comparison.

## Alpha6 room externalization

Original synthetic three-arrival-per-ear, feed-forward room model and distance
balance: Adrian Vos (soveda), 2026, MIT. No room impulse response, third-party
reverb source or additional dataset is copied. Existing Workshop Computer reverb
ring-buffer reads were reviewed as platform examples; this implementation extends
its own delay line. The 32-tap spectral bank and alpha6 audio previews retain
Gardner/Martin KEMAR attribution and data terms above.

## Alpha8 8mu host control

EightMU by Chris Johnson (2026), including rppicomidi's USB MIDI host driver
(2023), MIT. The adapted header preserves both full notices in vendor/EightMU.
Source revision, official protocol references and local changes are documented in
vendor/EightMU/SOURCE.md. USB role selection/configuration follows Chris Johnson's
WaveSeq (Workshop Computer releases/801_WaveSeq, same revision). Voder's paired
motion handling was also reviewed; its DSP/control code was not copied.
Original pickup/mapping, SPSC snapshot, offline LED diagnostics and enumeration
worker integration: Adrian Vos (soveda), 2026, MIT. KEMAR attribution is unchanged.

## Preset editor pass

Original JSON preset format, import/export, validation and lifecycle tests:
Adrian Vos (soveda), 2026, MIT. Workshop Computer Read/Apply/Save conventions and
Chris Johnson's WaveSeq file import/export UI were reviewed as examples; no new
third-party library or preset data was copied. Existing transport and HRTF source
credits above remain unchanged. That editor-only checkpoint used the tested alpha8 build; alpha9 adds the
selector/Mixer firmware described below.

## Alpha9 startup selector and Spatial Mixer

Original mode selector, per-source controls, level/distance integration, telemetry,
editor mode guards, tests and documentation: Adrian Vos (soveda), 2026, MIT.
Chris Johnson's WaveSeq switch-bank soft-takeover pattern was reviewed; the local
near/crossing pickup implementation extends this project's existing 8mu controls.
No new external code or measurements are introduced. Existing ComputerCard 0.4.0,
EightMU/rppicomidi and Gardner/Martin MIT KEMAR notices remain applicable. The
Disorientation selector position is intentionally reserved, not implemented DSP.

## Alpha10 Mixer 8mu mapping

Original selected-source fader mapping, source-aware pickup, controller handback,
button selection, LED feedback, tests and documentation: Adrian Vos (soveda), 2026,
MIT. src/mixer_mu_controls.h reuses this project's original MuControls pickup;
USB host and default CC/note transport continue to use the attributed adapted
EightMU header and rppicomidi driver. No new external source/data is introduced.
No motion data is used for Mixer; Twin Orbits' existing integration is unchanged.

## Alpha11 mode settings and placement presets

Original separate-bank record/migration, coherent snapshot handshake, placement
restore/pickup, mode-tagged SysEx/JSON v2, v1 import conversion and tests/docs:
Adrian Vos (soveda), 2026, MIT. No additional external code or dataset is introduced.
The existing ComputerCard/web_interface, WaveSeq/EightMU/rppicomidi, block scheduling,
Pico SDK/TinyUSB and Gardner/Martin KEMAR notices and retained licenses still apply.

## Alpha12 Fig8 additions

`src/dsp/fig8.h`, `fig8_table.h`, the third-mode integration and its tests/docs are
original Adrian Vos (soveda), 2026, MIT. The lookup tables encode mathematical
atan and radius, with no additional measurement dataset. `fixed.h` relocates our
existing helpers and retains the existing sine-table use credited to You spin me
round above. Fig8 uses the same ComputerCard/block transport and Gardner/Martin
KEMAR/room renderer; their notices/terms are unchanged. No Quasar code or data
has been introduced.

## Alpha13 Fig8 8mu additions

`src/fig8_mu_controls.h` and its tests/documentation are original Adrian Vos
(soveda), 2026, MIT. Reuses this project's `MuControls` pickup engine and the
existing bounded host adaptation. WaveSeq's pickup/buttons/motion implementation
was reviewed again; no additional external code or data was introduced. Existing
Chris Johnson WaveSeq/EightMU, rppicomidi, ComputerCard, Pico SDK/TinyUSB and
Gardner/Martin KEMAR notices and terms remain applicable and preserved.

## Alpha14 movement additions

`src/dsp/movements.h`, its tests, D tap/hold selection, schema migrations and
documentation are original Adrian Vos (soveda), 2026, MIT. Pendulum is mathematical
sine; Wander uses an original deterministic integer hash and cosine easing. No
additional external algorithm implementation or dataset is copied. Existing
sine-table, ComputerCard, WaveSeq/EightMU/rppicomidi, block transport, Pico SDK/
TinyUSB and Gardner/Martin KEMAR notices remain preserved and applicable.

## Card 333 beta packaging

Card 333 is a mnemonic for three spatial axes; it does not imply that elevation
is implemented. This package preserves the tested alpha14 firmware and source
snapshot. Original code/docs remain MIT; no Asterisk freeware terms are adopted.
Licensing layout follows Adrian Vos's card 122 and its runtime notice collection,
which preserves the third-party Newlib/GCC notices from Laboratory 0's card 369.
Those component notices retain their original ownership and license terms.

Keep LICENSE, this file, vendor notices and licenses/ with source distributions.
The full consolidated uf2/NOTICE.txt accompanies firmware and must travel with
standalone UF2 redistribution. This includes the KEMAR measurement attribution,
which is separate from the MIT software grant. Toolchain/build provenance and
linked runtime inventory: licenses/BUILD_COMPONENTS.md. No historical preview
files or obsolete firmware are included in this release folder.
