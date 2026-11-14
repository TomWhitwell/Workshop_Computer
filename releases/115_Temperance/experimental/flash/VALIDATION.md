# Validation — flash gate mode 0.2.0-flash-exp.1

Date: 2026-11-13. Release build and software checks pass. **Physical retesting
of this gate-mode binary is pending on both 2MB and 16MB cards.** The hardware
pass dated 2026-11-11 below applies to the preceding 0.1.1 universal build.

## Integration and mode-boundary fix

Matt Allison's MIT-licensed 0.2.0 gate feature is adapted from `e9ffbc3f`
(upstream PR #558, `6d47f07b`). Only this experimental flash directory is
changed by the integration. The RAM source, documentation and firmware remain
exactly as imported from upstream, including its original gate implementation.

Only the selected input detector runs. Switching mode resets the trigger timers
and gate ownership; the gate delay warms for 961 samples while rejecting rises
from the inactive/entry history. A gate high at entry cannot start recording
when that old sample emerges. New rises after entry still receive the matching
20ms delay. Switch taps retain independent ownership of their recordings.
Both gate levels share one packed 240-byte delay ring.

To retain timing margin, reader/cache links are initialised once at boot.
Starting a new session deactivates each playback configuration without changing
its length/direction/generation tuple, clears the audio reader pointer, and
changes its generation. An in-flight old decode may finish but cannot be read
by the new session. A barrier publishes deactivation before new pages.
The first sample uses an exact specialization of the ADPCM encoder's reset
state; the vendored upstream codec is unchanged. On a speed crossfade's first
tick, coincident heads reuse the same decoded sample and seam blend.

## Completed checks

- Release cross-build: Pico SDK 2.3.0 / ARM GNU 15.2.1, board `temperance_flash`,
  minimum 2MB, 192MHz, `TEMPERANCE_BENCH=OFF`. Flash image 43,624 bytes;
  main RAM 118,616 bytes (45.25%), plus the existing 2KiB scratch stacks.
- ASan/UBSan gate suites pass separately on simulated 2MB and 16MB flash:
  default/toggled modes, LEDs, exact lengths, short gates, unplug/probe,
  switch/gate takeover, independent overlapping gates, rapid re-recording,
  2kHz minimum lengths, already-high inputs and mode boundaries at
  1, 5, 19, 20 and 21ms. A new rise immediately after entry is accepted.
- Removing the warm-up guard in an isolated copy makes the already-high-gate
  regression fail, confirming the test exercises the fix.
- Existing flash suites pass on both sizes, including complete simultaneous
  capacity recording through the new Record/Release entry points. Capacity
  stops playback normally and a later gate release has no effect. The existing
  Main-max/X/Y, independent takes, reverse/long seeks, queue/cache/fault and
  invalid geometry tests pass; corrupt region cases run in separate processes.
- For every one of the 4,096 possible signed 12-bit first samples, the reset
  fast path and the following 31 samples produce exactly the upstream codec's
  encoded bytes. This checks both initial output and subsequent codec state.
- A 48,000-tick sequence of forward/reverse speed changes produces audio hash
  `11462512352256120198` with and without coincident-head reuse, on both sizes.
  The reference build uses `TEMPERANCE_DISABLE_SHARED_SAMPLE`.
- All **38** compiled Cortex-M0+ scenarios pass the unchanged 15% modeled
  headroom target: both modes/sizes, held-high gates, full wash, speeds,
  crossfades/seams, simultaneous gate starts, A recording while B plays,
  and entering gate mode during direction crossfades. Worker health and
  recording/playhead progress are asserted. Worst tested total is
  **3,372 cycles / 17.56us (84.3%)** on each size, including 1,146 cycles of
  modeled ComputerCard/normalisation/PWM/exception overhead.

Gate-start timing fixtures run for 200 ticks after each prepare: they execute
real session startup and ADPCM encoding, without publishing a complete flash
page to the emulator-only worker. Host suites cover complete pages and flash
operations. Physical SSI timing, RAM contention, interrupt phasing and erase
latency are not simulated; these figures are not a hardware worst-case bound.

## Reproduce

Use a fresh build folder, then `make -C test` (ASan/UBSan). Configure a separate
bench build with `TEMPERANCE_BENCH=ON` and run:

```sh
python tools/bench/cycles.py /path/to/bench/temperance0.2.0.flash.elf 1 4000
```

An optional fourth argument selects comma-separated scenarios for diagnosis;
omitting it runs all 38. Unicorn and Capstone are required. Do not flash the
bench binary. Hardware retest steps are at the top of [TEST_PROTOCOL.md](TEST_PROTOCOL.md).

---

# Validation — universal flash 0.1.1-flash-exp.1

Date: 2026-11-11. Status: release build and software regressions pass;
**user-reported functional hardware tests pass**.
On 2026-11-11 the user confirmed that `temperance0.1.1.flash.uf2` passes
functional hardware tests on **both 2MB and 16MB cards**. Individual test
measurements were not supplied. Direct audio-interrupt timing has not been
measured on hardware.
The user-reported 2026-11-07 hardware pass below belongs to the earlier
16MB-only 0.1.0-flash-exp.2 build, not this new binary.

## Automatic-size change

JEDEC is read before core 1/audio starts. Winbond manufacturer EF, memory
family 40/70, and capacities 15/18 select 2MiB/16MiB respectively. Other IDs
are refused without a capacity fallback. The first 512KiB is reserved, and
the remainder is split into sector-aligned equal loop regions. The looper's
automatic stop follows the detected sample limit.

| Detected size | Bytes per loop region | Samples per loop | Maximum at 48kHz |
| --- | ---: | ---: | ---: |
| 2MB | 786,432 | 1,548,288 | 32.256s |
| 16MB | 8,126,464 | 15,998,976 | 333.312s |

Before erasing/programming a queued page, the worker validates its block index
against its own loop region, then validates the physical address against the
detected chip. Decoder requests are also bounded by the detected capacity.
Fault code 5 identifies invalid storage geometry/range. The minimum-size
board config links against 2MB; raw flash recording uses the detected geometry.
The original RAM release and previous 16MB UF2s are preserved.

## Completed software checks

- Release cross-build with Pico SDK 2.3.0 / ARM GNU 15.2.1, at 192MHz, using
  `temperance_flash` (minimum 2MB) and `TEMPERANCE_BENCH=OFF`.
  Flash image: 42,560 bytes; main RAM: 117,264 bytes (44.73%).
- ASan/UBSan suite passes against separately allocated 2MB and 16MB NOR models.
  Both loopers fill simultaneously, stop automatically at their runtime limits,
  and read their final pages correctly; the firmware reserve remains intact.
- The existing independent odd-length recordings, reverse/seeks beyond 65,536
  samples, queued re-recording, complete instrument, full wash, rapid X/Y
  changes at 48kHz, cache guards, concurrent producer and overflow tests pass
  for both sizes. Storage faults leave dry audio and controls running.
- Missing/unknown manufacturer, memory type or capacity IDs are rejected before
  writes. Both supported memory-type identifiers are exercised through the
  same detector as the firmware.
- Injected overflowing block indices and the first block beyond A or B are
  rejected before any erase/program operation, separately on both card sizes.
- Sixteen compiled Cortex-M0+ timing scenarios exercise both geometries with
  full wash, forward/reverse/half/double speeds, direction crossfades and seams.
  The fixture resets when switching simulated cards and asserts healthy storage
  and playhead progress, preventing a faulted/dry-only run being counted as a
  timing pass. Complete calls must return within the emulator instruction limit.
- Including 1,146 modeled cycles of ComputerCard/normalisation/PWM/exception
  overhead: worst tested 16MB case **3,337 cycles / 17.38us**; worst tested 2MB
  case **3,352 cycles / 17.46us**. Both meet the bench's 15% modeled margin.

The model excludes physical SSI/erase timing and RAM bus contention. These
checks do not establish a hardware worst-case execution bound.

## Build and retest

Use the same `UF2/temperance0.1.1.flash.uf2` for both physical card sizes.
Run T12 in [TEST_PROTOCOL.md](TEST_PROTOCOL.md), including full-capacity stop,
repeat recordings into used flash, independent playback while the other loop
records, and the previous Main-max/X/Y lock-up case on each size.

```sh
cmake -S . -B /tmp/temperance-universal-build \
  -DPICO_SDK_PATH=/path/to/pico-sdk -DCMAKE_BUILD_TYPE=Release \
  -DTEMPERANCE_BENCH=OFF
cmake --build /tmp/temperance-universal-build -j2
make -C test
```

The resulting normal binary is `temperance0.1.1.flash.uf2`. For the timing
fixture, use a separate `/tmp/temperance-universal-bench` directory configured
with `TEMPERANCE_BENCH=ON`, then run:

```sh
python tools/bench/cycles.py \
  /tmp/temperance-universal-bench/temperance0.1.1.flash.elf 1 4000
```

Unicorn and Capstone are required. The bench binary is emulator-only: do not
flash it. Author attribution and GPL/MIT/BSD notices remain preserved.

---

# Validation — 0.1.0-flash-exp.2

Date: 2026-11-07. Status: software checks pass; user-reported functional hardware tests pass.

## User-reported hardware results — 2026-11-07

The tester first confirmed the exp.2 48kHz parameter-change retest passes,
then reported **“all tests pass”** after the recommended hardware checklist.
This records a functional pass for the discussed recording/playback stress,
full-capacity automatic stop, independent-loop operation, clock/effect changes
and longer-run checks. The previously reported Main-max/X/Y freeze was not
reproduced in the reported exp.2 retest.

These results are reported by the user, not independently observed by Codex.
No audio captures, exact soak duration, measured latency, debugger counters or
oscilloscope/cycle measurements were supplied. Do not interpret this report
as verification of optional multi-hour/overnight tests, every flash-chip
variant, flash endurance or a measured worst-case interrupt execution time.
The direct hardware audio-interrupt timing measurement remains outstanding.

## Hardware report and correction

Exp.1 froze the LEDs and stopped audio when moving X or Y with Main fully
clockwise (48kHz), including a long recording and Y fully clockwise. A compiled
Cortex-M0+ cycle estimate confirmed that the exp.1 playback/crossfade path
could exceed 4,000 cycles per sample at 192MHz. Its earlier four-scenario model
reported up to 4,602 cycles (24.0us), even without normalisation-probe overhead.
This supports an audio-deadline diagnosis; it does not prove there are no
other hardware faults.

Exp.2 uses cached range readers with sequence/session checks, publishes aligned
play positions separately from infrequent configuration changes, caches the
completed flush state and checks storage health once per audio-clock tick.
It retains loop capacities, page format, six speed zones, seam tails,
speed-change crossfades, dry mix and the plate. Exp.1's UF2 is preserved.

## New verification

- Release build: 42,184-byte flash image, 116,880 bytes main RAM (44.59%).
  UF2 headers/version/credits and firmware storage boundary verified; no bench
  hooks in the packaged binary. Original release and exp.1 UF2 remain unchanged.
- ASan/UBSan host suite passes, including the existing full-capacity test.
- New regression: two 250,001-sample recordings, 48kHz, middle switch, full wash,
  Y maximum while X changes every 2ms; then Y and both controls change across
  all six zones. Both playheads keep moving and output remains bounded.
- New reader checks reject in-progress writes, stale session tags, reused slots
  and out-of-range positions. Fault handling keeps the dry path/controls live.
- Eight compiled-firmware timing scenarios cover a 10,000,001-sample recording,
  forward/reverse/half/double speeds, simultaneous crossfades, mid-loop and seam
  positions. The model includes 1,146 cycles of worst measured ComputerCard,
  enabled normalisation-probe, PWM and estimated exception overhead.
- Worst tested total: approximately 3,309 cycles (17.2us, 82.7% of 4,000 cycles),
  meeting the bench's 15% modeled margin requirement. The fixture's instruction
  limit also rejects emulator calls that do not return.

The cycle model does not simulate physical bus contention, exact interrupt
phasing or SSI/flash timing. The user-reported functional hardware pass is
recorded above. A direct measurement of complete audio-interrupt duration
remains outstanding; the 17.2us result is an emulator estimate.

## Output naming — 2026-11-08

CMake now sets the output basename to `temperance0.1.0.16mb`, producing matching
UF2, ELF, BIN and HEX filenames. The packaged UF2 was renamed without changing
its bytes. A release rebuild using the new basename passed; its only byte
change from the tested package was the embedded Nov 7 to Nov 8 build date.
The executable code, embedded experimental version and licence credits remain
unchanged. Both packaged firmware checksums were verified.

## Reproducing the timing check

Install `unicorn` and `capstone` into an isolated Python environment, then:

```sh
cmake -S . -B /tmp/temperance-16mb-bench \
  -DPICO_SDK_PATH=/path/to/pico-sdk -DCMAKE_BUILD_TYPE=Release \
  -DTEMPERANCE_BENCH=ON
cmake --build /tmp/temperance-16mb-bench -j2
python tools/bench/cycles.py \
  /tmp/temperance-16mb-bench/temperance0.1.0.16mb.elf 1 4000
```

The bench fixture enables playback using synthetic ADPCM flash. Its binary is
**emulator-only; do not flash it**. Build the distributed UF2 with
`TEMPERANCE_BENCH=OFF`. The timing driver adapts Matt Allison's original MIT
bench; its credit and licence remain retained.

The host-test command and original validation history follow below.

---

## Historical record — 0.1.0-flash-exp.1

Date: 2026-11-04. Status: experimental; physical hardware validation pending.

### Checks completed for exp.1

- Release firmware built successfully with Pico SDK 2.3.0, ARM GNU 15.2.1,
  `copy_to_ram`, a 16MiB board configuration and a 192MHz processor clock.
- Linker report: 115,976 bytes used in the 256KiB main RAM region (44.24%);
  2KiB stack reservations in each 4KiB scratch bank. Flash image: 41,464 bytes,
  comfortably below the 512KiB audio-storage boundary.
- ELF section inspection places runtime `.text` and `.data` in SRAM. The
  flash-resident boot/initialisation sections run before flash recording.
- Picotool recognises the RP2040 UF2 and its experimental name/version.
  Following the attribution audit, the rebuilt UF2 also embeds upstream credits;
  source-file attribution headers and bundled license texts were added. These
  changes do not alter the audio or storage implementation. The overall GPL-3.0
  licence is also embedded following the upstream author recommendation reported
  by the maintainer; Matt's original MIT notice remains bundled separately.
- Host storage/looper/instrument tests passed with AddressSanitizer and
  UndefinedBehaviorSanitizer, warnings treated as errors.
- Both channels' ADPCM playback matched a reference decoder for independent
  odd-length recordings and reverse/seek positions on either side of the
  original 65,536-sample address limit.
- Tested queued rapid re-recording, independent lengths, all six speeds,
  wrapping, double-tap clearing, independent delayed pulse recording, live
  clock changes, full plate wash/stereo output and region isolation.
- Simulated the entire 15,998,976-sample capacity of one region and its last
  page; writes stayed outside the firmware reserve. Simulated producer activity
  while programming. A stalled worker correctly latched a queue-overflow fault.
- The original Temperance host suite passed under UndefinedBehaviorSanitizer.
  No tracked original source, metadata or firmware files changed.

On this Mac, the Xcode command shims reported an unaccepted license. Validation
used the installed Command Line Tools binaries directly; no system settings
were changed. The host invocation was:

```sh
/Library/Developer/CommandLineTools/usr/bin/make -C test \
  CXX=/Library/Developer/CommandLineTools/usr/bin/clang++ \
  'CPPFLAGS=-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -isystem /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1'
```

The firmware CMake configuration additionally used
`-DCMAKE_MAKE_PROGRAM=/Library/Developer/CommandLineTools/usr/bin/make`.

### Physical checks outstanding at the exp.1 stage

1. Confirm JEDEC acceptance on the intended 16MB Winbond card, normal startup
   and repeatable startup after a reset. Check that a 2MB card is refused.
2. Record A and B simultaneously at 48kHz. Stop one while the other continues;
   listen for gaps, clicks or noise during sector erases. Inspect diagnostics:
   zero queue overruns and hardware faults; read misses should settle once
   the initial playback caches are filled.
3. Exercise reverse/double-speed playback, rapid direction changes, independent
   pulse starts/stops, clock modulation and full wash while the other loop
   records. Measure the worst audio interrupt duration against 20.83 microseconds.
4. Measure stop-to-play latency, check seam/crossfade quality and listen for
   flash bus noise. Confirm erase-suspend timing on the particular flash chip.
5. Run to full capacity on both channels and check automatic stop, final-page
   playback, repeated takes and long-loop random CV movement. Reset should
   return to empty loops; this build has no save/recall.

The host model tests the NOR erase/program rules and storage geometry, not
the SSI controller, physical suspend/resume timing, flash endurance or
RP2040 bus contention. These checks are required before treating the build
as a stable release.
