# WS-DOOM firmware

The card firmware is [rp2040-doom](https://github.com/kilograham/rp2040-doom)
(GPL-2.0, vendored in `doom/`, upstream commit in `doom/UPSTREAM_COMMIT`)
plus a Workshop System Computer backend:

| File | Purpose |
| --- | --- |
| `doom/src/pico/ws/ws_hw.c` | MCP4822 SPI DAC fed by a 48 kHz PWM interrupt, mux'd ADC scan of Main/X/Y knobs + switch + CV ins, pulse I/O, CV outs, LED health bar |
| `doom/src/pico/ws/ws_stream.c` | USB CDC device: streams frames/palette/status, receives key events |
| `doom/src/pico/ws/ws_usb_descriptors.c` | CDC-ACM descriptors |
| `doom/src/pico/i_video.c` (`WS_COMPUTER` sections) | headless present + 8-bit frame encoder replacing scanvideo |

The CMake target is `doom_tiny_ws`: super-tiny WHX WAD format, no scanvideo,
no I2C networking (GPIO 18/19 belong to the DAC), sound mixed at 48 kHz, WAD
at flash offset `0x48000`.

## Just flashing

Grab `ws-doom-full.uf2` from the card folder (firmware + shareware WAD in
one file), put
the Computer into USB bootloader mode with a blank/expendable 2 MB program
card inserted, and drop the file onto the `RPI-RP2` drive. Panel mapping and
a macOS copy-truncation workaround (`cp -X` / picotool) are in the top-level
README.

## Building

Requirements: cmake, ninja, [pico-sdk](https://github.com/raspberrypi/pico-sdk)
(`develop`, with the tinyusb submodule), [pico-extras](https://github.com/raspberrypi/pico-extras),
and **arm-none-eabi-gcc 13.2.rel1** (other versions may overflow the very
tight flash/RAM budgets — upstream's warning, not ours).

```bash
cd firmware/doom
mkdir build && cd build
cmake -GNinja -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DPICO_BOARD=ws_computer \
      -DPICO_BOARD_HEADER_DIRS=$PWD/../src/pico/ws/boards \
      -DPICO_SDK_PATH=/path/to/pico-sdk \
      -DPICO_EXTRAS_PATH=/path/to/pico-extras ..
ninja doom_tiny_ws
```

The `ws_computer` board is a stock Pico with `PICO_FLASH_SPI_CLKDIV 4`:
at the 270 MHz overclock the default divider would run the program card's
flash at 135 MHz, which many chips can't do (symptom: completely dead
module, no LEDs).

## Boot diagnostic ladder

- **LED 0 on** — code started (set before the clock/voltage change).
- **LEDs 0+1 on** — survived the 270 MHz clock raise and hardware init.
- **LED chase** — game loop running (D_DoomMain finished, tics flowing).
- **LED bar** — in-game health.

Stuck at nothing → firmware never ran (bad flash, or boot stage 2
incompatible with the card's flash chip). Stuck at LED 0 → the overclock
or flash timing killed it. Stuck at 0+1 → crashed during doom init
(check UART logs on the debug pins, GPIO 0/1, 115200 baud).

Then bundle code + WAD into one UF2:

```bash
python3 firmware/tools/make_uf2.py \
    firmware/doom/build/src/doom_tiny_ws.bin \
    firmware/doom/doom1.whx \
    ws-doom-full.uf2
```

This `firmware/doom` tree is trimmed for size (SDL/desktop ports, the
other-game sources and packaging files are removed) but builds the card
firmware exactly; the untrimmed tree lives in the WS-DOOM repo.

`doom1.whx` is the shareware DOOM1.WAD converted to rp2040-doom's
super-compressed WHX format (ships with the upstream repo; regenerate from a
WAD with upstream's `whd_gen`).

## Design notes

- **Audio**: the stock mixer fills `pico_audio` producer-pool buffers
  (OPL2 music + ADPCM sfx, stereo 16-bit, now at 48 kHz). A PWM-wrap
  interrupt (270 MHz / 5625 = exactly 48 kHz) drains the pool and writes
  both DAC channels through the SPI FIFO. Doom's own stereo separation
  drives the panning you hear when hunting enemies.
- **Video**: rp2040-doom keeps a double-buffered 320×168 8-bit framebuffer
  (status bar + menus composited from "vpatch" overlay lists). The
  `display_frame_freed` spin sites now call `ws_video_present()`, which
  consumes the finished frame, composites overlays in palette-index space
  (`ws_draw_vpatch8`), RLE-encodes per scanline and streams over CDC.
  Standalone (no host attached) the encoder is skipped entirely.
- **vpatchlists** normally live in the USB controller's DPRAM — moved to
  ordinary RAM because the CDC device stack needs the DPRAM back.
- **Inputs** are scanned per game tic (35 Hz) via the ADC mux, converted to
  synthetic keyboard events with hysteresis zones (centre dead-zone, run at
  the extremes), and posted through `D_PostEvent`.
- **Savegames**: the code paths are intact, but with the WAD at `0x48000`
  there is no spare flash behind it, so saving reports failure harmlessly.
  Use a 16 MB card + a rebuilt layout if you want saves.
