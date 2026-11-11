# Temperance Flash — automatic 2MB / 16MB detection

**Version 0.1.1-flash-exp.1. One UF2 for supported 2MB and 16MB Winbond cards.**
Build and software tests pass. On 2026-11-11 the user confirmed that this
auto-detecting version passes functional hardware tests on **both 2MB and
16MB cards**. The earlier 16MB-only exp.2 build passed user-reported
functional tests on 2026-11-07.

Flash **`UF2/temperance0.1.1.flash.uf2`**. Capacity is identified at startup;
there is no menu, jumper or separate card-size build. This is the flash-recording
variant on both sizes, not the original RAM micro looper. Matt Allison's
original source, metadata, manual, MIT licence and UF2 remain unchanged.

Overall licence: **GNU GPL version 3 (GPL-3.0)**; see [LICENSE](LICENSE),
[ATTRIBUTION.md](ATTRIBUTION.md) and [LICENSES/](LICENSES/).

## Detection and storage

Before starting the second core/audio, the firmware reads JEDEC ID once.
Supported identifiers are Winbond manufacturer `EF`, memory type `40` or `70`,
with capacity `15` (2MiB) or `18` (16MiB). Unknown manufacturer/type, missing
identification and other capacities are refused without a size fallback.
The supported Winbond W25Q16/W25Q128 families provide the erase-suspend commands
used by the storage engine; other flash-chip variants are not verified.

The firmware reserves the first **512KiB** and splits the rest equally:

| Detected card | Loop A flash offsets | Loop B flash offsets | Samples per loop |
| --- | --- | --- | ---: |
| 2MB | `0x080000..0x13ffff` | `0x140000..0x1fffff` | 1,548,288 |
| 16MB | `0x080000..0x83ffff` | `0x840000..0xffffff` | 15,998,976 |

Both loops have their full capacity simultaneously. Writes and erases are
checked against the detected geometry, including each loop's own region.
Invalid queued block indices are refused before calculating/erasing their
addresses. Recording stops automatically at the detected sample limit.

Recording replaces sample data in those flash regions. Use a card whose
existing audio contents you can replace. All six LEDs flashing together means
unsupported identification or a latched storage fault. The clean dry path and
controls remain available; power-cycle after a fault.

## Recording duration per loop

These are calculated storage limits with a constant clock, not newly measured
physical recording times for the universal build.

| Sample clock | 2MB card | 16MB card |
| --- | ---: | ---: |
| 48kHz | 32.3s | 5m 33.3s |
| 32kHz | 48.4s | 8m 20.0s |
| 24kHz | 1m 4.5s | 11m 6.6s |
| 16kHz | 1m 36.8s | 16m 39.9s |
| 12kHz | 2m 9.0s | 22m 13.2s |
| 8kHz | 3m 13.5s | 33m 19.9s |
| 6kHz | 4m 18.0s | 44m 26.5s |
| 4kHz | 6m 27.1s | 1h 6m 39.7s |
| 3kHz | 8m 36.1s | 1h 28m 53.0s |
| 2kHz | 12m 54.1s | 2h 13m 19.5s |

At the same clock, half-speed playback doubles these times; double speed halves
them. Changing the clock during recording changes how quickly storage fills.

## Controls and sound

Use the [original manual](../../README.md) for the full panel/patch guide.

| Control | Switch middle | Switch up |
| --- | --- | --- |
| Main | Shared sample clock, 48kHz to 2kHz | Wash: plate send, decay and damping |
| X | Loop A speed/direction | Clean dry level |
| Y | Loop B speed/direction | Crush, 12 down to 4 bits |

- Tap down to record both loops; tap again to stop/play. Two taps less than
  300ms apart clear. Pulse In 1 and 2 independently start/stop A and B.
- Forward/reverse at half, normal and double speed, pickup, input normalling,
  calibrated random CV and pulse outputs are retained.
- Both sizes use **lossy IMA-ADPCM**, with four bytes of decoder state and 504
  samples per 256-byte page. Compression is audible even at the cleanest Crush.
- The plate stays at 24kHz; the live dry input bypasses storage/effects.
- Playback waits for the final queued write and first decode after stopping.
  Exact added latency needs measuring. A missing playback block holds the
  last output/position until data arrives.
- Original seam tails and speed-change crossfades remain. Re-recording fades
  the old last output value, since its flash region is being reused.
- Loops start empty after reset. **Save/recall and continuous wrapping delay
  are not implemented**, even though audio bytes remain in flash.
- CPU clock remains **192MHz / 1.15V**. The exp.2 audio-deadline optimisations
  are retained: constant-time cached reads, minimal audio-thread divisions,
  and aligned positions published separately from configuration changes.

## Build and tests

The flash version lives in `experimental/flash/`, alongside the unchanged
original RAM release. The board configuration links for the **minimum 2MB**
card; detected capacity governs
all recording addresses, including on 16MB. Runtime code/constants run in RAM.

```sh
cmake -S . -B /tmp/temperance-universal-build \
  -DPICO_SDK_PATH=/path/to/pico-sdk -DCMAKE_BUILD_TYPE=Release \
  -DTEMPERANCE_BENCH=OFF
cmake --build /tmp/temperance-universal-build -j2
```

The output is `/tmp/temperance-universal-build/temperance0.1.1.flash.uf2`.
Use a fresh build directory so old 16MB-specific CMake cache settings are not
reused. See [VALIDATION.md](VALIDATION.md) for software results and required
physical checks, and [TEST_PROTOCOL.md](TEST_PROTOCOL.md) for the test procedure.

```sh
make -C test
```

The ASan/UBSan suite runs against actual-size simulated NOR flash for both
capacities. It checks both loops filling/stopping at their limits, final-page
reads, firmware-region preservation, independent recordings, reverse/seeks,
queued re-recording, 48kHz parameter changes, invalid chip IDs, and malformed
block addresses (overflow and the first block beyond either loop).
The emulator timing fixture exercises both geometries; it does not simulate
physical bus contention or erase/program timing.

`temflash::geometry` and `temflash::diagnostics` are available to a debugger.
Fault codes: 1 program timeout, 2 erase timeout, 3 suspend not confirmed,
4 invalid decoder header, **5 invalid storage range/block**. A refused chip
makes `Healthy()` false with no fault code. No physical pass is claimed for
the new universal build merely because the earlier 16MB build passed.

## Previous builds and attribution

`UF2/temperance0.1.0.16mb.uf2` is the previously tested **16MB-only** exp.2
build. The older exp.1 binary is retained as history and has a known 48kHz
parameter-change freeze. `SHA256SUMS.txt` covers all packaged firmware.

This is **Matt Allison's Temperance**, adapted with flash-recording techniques
and QSPI code from **Dune Desormeaux's Goldfish**, and the ADPCM implementation
from **MLRws** via Goldfish. ComputerCard is by **Chris Johnson**; the plate
design is by **Jon Dattorro**; the SDK is by **Raspberry Pi**. The original
README acknowledges Claude (Anthropic) development support. Experimental
integration, automatic size detection and tests were developed with OpenAI
Codex at the user's direction. Upstream notices remain intact; overall
GPL-3.0 follows the Goldfish/MLRws author's recommendation as reported by the
maintainer. See [ATTRIBUTION.md](ATTRIBUTION.md) for file origins and notices.
