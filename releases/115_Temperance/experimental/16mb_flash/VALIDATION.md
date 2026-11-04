# Validation — 0.1.0-flash-exp.1

Date: 2026-11-04. Status: experimental; physical hardware validation pending.

## Completed

- Release firmware built successfully with Pico SDK 2.3.0, ARM GNU 15.2.1,
  `copy_to_ram`, a 16MiB board configuration and a 192MHz processor clock.
- Linker report: 115,736 bytes used in the 256KiB main RAM region (44.15%);
  2KiB stack reservations in each 4KiB scratch bank. Flash image: 41,220 bytes,
  comfortably below the 512KiB audio-storage boundary.
- ELF section inspection places runtime `.text` and `.data` in SRAM. The
  flash-resident boot/initialisation sections run before flash recording.
- Picotool recognises the RP2040 UF2 and its experimental name/version.
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

## Physical checks still required

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
