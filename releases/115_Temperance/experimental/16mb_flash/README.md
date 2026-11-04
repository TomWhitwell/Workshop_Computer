# Temperance — experimental 16MB flash looper

**Version 0.1.0-flash-exp.1. Built and host-tested; not yet tested on physical hardware.**

An isolated derivative of Temperance 0.1.0. The original source, metadata,
manual and `../../UF2/temperance.0.1.0.uf2` are unchanged. This experiment
uses Goldfish 2.0's buffered ADPCM recording and flash erase-suspend approach
to give Temperance two much longer, independently recordable loops.

## Firmware and supported card

Flash `UF2/temperance.0.1.0-flash-exp.1.16mb.uf2` onto a **16MB Winbond
W25Q128 program card**. The processor runs at **192MHz / 1.15V**, as in
the original Temperance. The binary checks the JEDEC manufacturer and
capacity before allowing any recording writes; a 2MB card or an unrecognised
chip is refused. This is 16 megabytes, not 16 megabits.

Recording erases and replaces audio in this card's flash storage regions.
Use a card whose existing sample contents you can replace.

All six LEDs flashing together means the hardware was refused or a storage
fault has latched. The clean input path remains available. Restart after a
fault; do not interpret a fault as a successful recording.

## Maximum lengths

Each loop has **15,998,976 sample ticks**, with both capacities available
simultaneously. These are calculated storage limits, not measured hardware
recording times.

| Sample clock | Recording capacity per loop |
| --- | ---: |
| 48kHz | 5m 33.3s |
| 32kHz | 8m 20.0s |
| 24kHz | 11m 6.6s |
| 16kHz | 16m 39.9s |
| 12kHz | 22m 13.2s |
| 8kHz | 33m 19.9s |
| 6kHz | 44m 26.5s |
| 4kHz | 1h 6m 39.7s |
| 3kHz | 1h 28m 53.0s |
| 2kHz | 2h 13m 19.5s |

At the same clock setting, half-speed playback doubles these durations and
double speed halves them. Changing the clock during recording changes the
rate at which capacity is consumed. Playback pitch and duration continue
to follow the shared clock.

## Controls

Use the [original manual](../../README.md) for the full panel and patch guide.

| Control | Switch middle | Switch up |
| --- | --- | --- |
| Main | Shared sample clock, 48kHz to 2kHz | Wash: plate send, decay, damping |
| X | Loop A speed/direction | Clean dry level |
| Y | Loop B speed/direction | Crush, 12 down to 4 bits |

- Tap down to record both loops; tap again to stop and play.
- Pulse In 1 and 2 independently start/stop A and B.
- Two taps less than 300ms apart clear the affected loop(s).
- Forward/reverse at half, normal and double speed, layer pickup, input
  normalling, calibrated random CV outputs and pulse outputs are retained.
- The dry signal bypasses storage and effects. The plate remains at 24kHz.

## Differences and experimental limits

- **Lossy IMA-ADPCM storage** replaces the original lossless packed 12-bit
  sample arrays. Its colour is present even with Crush at its cleanest setting.
- Each flash page holds four bytes of decoder state and 504 samples. Decoder
  state is carried across pages, but each page can be decoded independently
  from its stored state, allowing reverse playback without a large RAM index.
- After stopping, playback waits for the last queued page and its first
  decoded block. This adds a short storage-dependent delay beyond the original
  matched 20ms event/input delay. It needs measuring on the actual card.
- If the current playback block is unavailable, the looper holds its last
  output and position until decoding catches up. Diagnostic read misses count
  these requests, including normal initial cache loading.
- The original short post-stop seam tail and speed-change crossfade remain.
  Starting a new recording fades the previous **last output value** over
  approximately 20ms; it cannot keep reading the old loop while reusing its
  flash region.
- Loops start empty after reset. Audio bytes remain in flash, but **save/recall
  and recovery metadata are not implemented**. Clear is a logical clear, not
  a full-chip erase. There is no continuous wrapping delay mode.
- The original cycle-bench figures do not validate this derivative. Live
  timing, audio noise, erase-suspend compatibility and worst-case simultaneous
  recording/playback still need hardware testing.

## Implementation

The first 512KiB is reserved for firmware. Loop A occupies
`0x080000..0x83ffff`; Loop B occupies `0x840000..0xffffff`, 7.75MiB each.
Audio core 0 encodes samples directly into independent 32-page queues. Their
indices stay monotonic across takes, so pending writes complete before the
consumer erases/reuses a region for a new take. Queue overflow latches a fault
instead of overwriting queued samples.

Core 1 owns flash access, erases ahead and uses Goldfish's raw QSPI
`0x75`/`0x7a` suspend/resume sequence to program already-erased pages and refill
decoded playback caches during a sector erase. It checks the suspend flag
before programming, bounds program/erase waits, and services controls during
the suspended interval. Core 0 reads only RAM caches. Main and crossfade heads
each have three independently tagged blocks, protected by sequence counters.

Playback positions use half-sample units, enough for all six speed zones
without the original 16.16 position limit. LED position multiplication and
smooth-random phase accumulation are adjusted for long loops.

`temflash::diagnostics` exposes queue overruns, read misses, erase/program
counts, maximum queue occupancy and hardware faults to a debugger. Hardware
fault values: 1 = program timeout; 2 = erase timeout; 3 = suspend not confirmed;
4 = invalid decoder-state header. Hardware refusal is reported by
`temflash::Healthy() == false` without a fault code.

## Build and verification

From this directory, with the Pico SDK and ARM GNU toolchain installed:

```sh
cmake -S . -B /tmp/temperance-16mb-build \
  -DPICO_SDK_PATH=/path/to/pico-sdk -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/temperance-16mb-build -j2
```

The output is `/tmp/temperance-16mb-build/Temperance_16MB_FLASH_EXPERIMENTAL.uf2`.
The entire runtime image is copied to RAM. `PICO_FLASH_SIZE_BYTES` is 16MiB.
The initial build used Pico SDK 2.3.0 and ARM GNU 15.2.1; see
[VALIDATION.md](VALIDATION.md) for the recorded build result and outstanding
hardware checks.

```sh
make -C test
```

The host tests run the real storage, codec, looper and instrument code against
simulated NOR flash under AddressSanitizer and UndefinedBehaviorSanitizer.
They check independent odd-length recordings, reference decoder agreement,
reverse/seeks beyond 65,536 samples, rapid re-recording with pending pages,
partition isolation, all six speeds, pulse recording, changing sample clocks,
full wash, the complete 16-million-sample capacity and queue-overflow handling.
They do not emulate the QSPI controller or prove real-time timing.

## Credits

Temperance and its design: Matt Allison; original MIT license retained here.
Goldfish 2.0: Dune Desormeaux. QSPI helpers and the erase-suspend streaming
approach are adapted from `releases/11_goldfish/goldfish_stream.c`.
`vendor/adpcm.h` is copied unchanged from Goldfish, where it is credited to
MLRws. The per-page format, independent queues and cache integration are
specific to this experiment. ComputerCard by Chris Johnson is used from the
unchanged original `../../lib/` directory; its license remains there.
