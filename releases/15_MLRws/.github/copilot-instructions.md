# Copilot instructions for MLRws

## Build and validation commands

- These instructions are scoped to `releases/15_MLRws`; commands below assume this directory is the current working directory.
- The Raspberry Pi Pico SDK must be available via `PICO_SDK_PATH`; the local `GNUmakefile` defaults to `/opt/pico-sdk`.
- `make firmware` builds `MLRws.uf2` into `UF2/`.
- `make all` builds the firmware.
- `make clean` removes `build/`.
- `make flash` builds and flashes through GDB/OpenOCD. Override `OPENOCD_HOST`, `OPENOCD_GDB_PORT`, or `GDB` when needed.
- There is no local automated test or lint target. For firmware changes, validate by building with `make firmware`.

## High-level architecture

- MLRws is a Pico SDK C/C++ Workshop Computer card built as a single dual-mono firmware. `CMakeLists.txt` sets `CARD_NAME` and flash layout macros; the post-build step copies UF2 files into `UF2/` and fails if the binary exceeds the reserved firmware flash area.
- `ComputerCard.h` is vendored locally and provides the hardware abstraction. `MLRCard` derives from `ComputerCard`, does setup in its constructor, overrides `ProcessSample()`, then calls `Run()`. `ProcessSample()` runs at 48 kHz and must complete in roughly 20 microseconds.
- ComputerCard handles ADC/mux sampling, smoothed knobs/CV, switch and jack detection, DAC audio outputs, PWM CV outputs, LEDs, EEPROM calibration, and optional normalisation probing. Jack indexes are zero-based internally even though panel labels are one-based.
- Real-time audio/control is split from slower I/O across RP2040 cores. Core 0 runs `ComputerCard::ProcessSample()`, handles grid gestures/control state, records ADPCM samples, plays/mixes RAM ring buffers, and outputs audio. Core 1 runs USB tasks plus `mlr_io_task()`, refilling playback rings from flash and erasing/writing recording pages.
- Runtime mode is selected at startup from USB power/protocol detection:
  - `HostMLR`: USB host with a directly connected monome grid.
  - `DeviceMLR`: USB device speaking mext through a host/grid proxy.
  - `DeviceGridless`: no grid protocol detected; panel-only interaction.
  - `DeviceSampleMgr`: CDC sample manager protocol owns flash/sample transfer.
- Persistent data lives on the program card flash after the firmware reserve. Each of six tracks has a header sector plus ADPCM data; scene/pattern/recall state is stored after the track area. Keep CMake flash constants, `mlr.h` layout macros, and sample-manager protocol expectations in sync.

## MLRws-specific conventions

- Prefer fixed-point integer DSP in per-sample paths. Avoid introducing floating-point work inside `ProcessSample()` unless the surrounding card already accepts that cost.
- MLRws uses `pico_set_binary_type(... copy_to_ram)` because it writes flash during operation. Use `__not_in_flash_func(...)` for additional timing-critical code that may run while flash is busy.
- Keep USB/TinyUSB polling and other potentially long work off the audio core. Core 1 owns calls such as `tud_task()`, `tuh_task()`, `mext_task()`, `device_mode_task()`, and flash I/O so audio stays uninterrupted.
- Preserve the `COMPUTERCARD_NOIMPL` pattern when including `ComputerCard.h` from multiple translation units: define it before including the header in all but one source file.
- Shared structs in `mlr.h` include static size checks because flash headers must fit in one 4 KB sector. Keep the mono flash format compatible with existing `MLR4` track data.
- Any grid layout change must deliberately account for 8x8 grids as well as 16-wide grids. 8x8 does not need functional parity with larger grids, but it must continue to work and have a sensible, intentional layout.
- Treat cross-core shared state deliberately: existing code uses `volatile` fields, single-producer/single-consumer rings, and memory barriers such as `__dmb()` around mode handoffs. Do not replace these with blocking locks in audio paths.
- The mext grid transport pads USB writes to 64 bytes with `0xFF` for compatibility with both modern CDC/RP2040 and older FTDI grids; preserve this behavior when changing `monome_mext.c`.
- Keep `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64` in CMake compile definitions; this card relies on the longer oscillator startup delay.
