# Beta3 verification — 2026-10-15

©2026 Adrian Vos(soveda),MIT. [Source/dependency attribution](../THIRD_PARTY_NOTICES.md).

**0.4.0-beta3** promotes user-tested0.4.0-alpha2-roll-polarity, commit67228dd,
with no control/DSP changes. Distributed UF2 is byte-identical to the accepted alpha:
`ff641228bbca88632c47de0419f0194238a98026c79164e5411022e0fdc92149`.
Root Release rebuild also matches. PicoSDK2.3.0/ArmGNU15.2.Rel1, warning-free build;
flash147876B, RAM189792B, scratch2048B each.192MHz/1.15V,48kHz,two sources/64taps,
64-frame worker/128-frame transport. ComputerCard0.4.0 local ADC/DMA flash-pause
adaptation remains byte-identical to beta2.

23C++host programs pass ASan/UBSan against promoted root: physical pitch/roll
cardinals through Orbits scene placement, yaw rejection/neutral hold, non-flat
recapture, Disorientation roll sign,180000 changing-rotation captured frames,
existing pickup/page/reconnect, save parser/init/Apply/Read and1000changed+
100unchanged saves/failure injection, HRTF/room/slope/vertical/smoothing/queues.
Editor script regressions pass for lifecycle, presets/migration and staged elevation
tools. User reports tests pass and asks promotion; no new peak/duration numbers
were supplied. Earlier timing history is context, not beta3 profiling.

Metadata validates against the canonical Workshop Computer schema with0errors/
0warnings. Beta2 source/editor/docs/firmware and original tester ZIP are archived
under archive/beta2; older archives retained. Firmware checksums verified. Settings
formats remain SysEx7/JSON7/flash8, compatible with beta2. Existing beta1 migration/
downgrade instructions remain applicable. Root beta includes current editor, notices,
source and tester guide. No Workshop release changes or remote publishing implied.


## Archive cleanup verification — 2026-10-15

Five superseded alpha experiments moved into archive/experimental/ with rebased
build, Markdown and comparison-test paths. All five CMake configurations pass;
the moved pitch/roll baseline rebuild is byte-identical to current beta3. Twenty
archived comparison programs (four per moved experiment) pass ASan/UBSan; all five
matching editor script checks pass. Current beta build/editor checks pass; source,
editor, firmware and active regression tests remain unchanged by housekeeping.
All35 archived UF2 and14 historical WAV checksums verify. Preview WAVs now live
in archive/previews/ with their attribution/checksum list. Old build caches are
retained locally under ignored archive/local-builds/. Tester ZIP refreshed.


## Workshop Computer card333 update — 2026-10-22

Copied current beta3 from standalone source75d740e into the Binaural-card branch.
Firmware remains byte-identical to user-tested pitch/roll alpha2 and beta3:
ff641228bbca88632c47de0419f0194238a98026c79164e5411022e0fdc92149.
No DSP/control/save changes are introduced by this release packaging. Regression
comparison headers are now local under tests/reference/; tests/run.sh runs all23
C++programs and editor checks. Full historical archives remain external.
Release-folder build, host/editor regressions, metadata and UF2 checksums pass.
No new hardware timing or duration is claimed for the packaging update.


Reproducibility: the normal2026-10-22 rebuild differs from the accepted2026-10-15
binary only in the SDK's embedded build-date text (two bytes). Distributed UF2
remains the original tested file. To reproduce its date and exact firmware bytes,
use `SOURCE_DATE_EPOCH=1792022400 cmake --build build --clean-first -j2` after
configuring with the recorded SDK/toolchain. This timestamps the reproduction at
2026-10-15UTC; it does not change DSP or firmware settings.
