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

## Reproducing the timing check

Install `unicorn` and `capstone` into an isolated Python environment, then:

```sh
cmake -S . -B /tmp/temperance-16mb-bench \
  -DPICO_SDK_PATH=/path/to/pico-sdk -DCMAKE_BUILD_TYPE=Release \
  -DTEMPERANCE_BENCH=ON
cmake --build /tmp/temperance-16mb-bench -j2
python tools/bench/cycles.py \
  /tmp/temperance-16mb-bench/Temperance_16MB_FLASH_EXPERIMENTAL.elf 1 4000
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
