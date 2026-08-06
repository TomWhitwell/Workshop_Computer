# 82_Computer_Grids — engineering context

Dense reference for USB / Web MIDI / dual-core bring-up. User-facing docs stay in `README.md`.

## Web GUI

- **Path:** `web/` — serve over **http://localhost** or HTTPS (not `file://`) so Chromium exposes Web MIDI. Example:  
  `python3 -m http.server 8080 --directory web`
- **API:** `navigator.requestMIDIAccess({ sysex: true })`; SysEx `0xF0 0x7D 0x63 … 0xF7` (manufacturer `0x7D`, device `0x63`). See `sysex_spec.json`, `web/app.js`, `GridsCard.cpp`.
- **Official Workshop pattern:** [web_interface](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard/examples/web_interface) — same Web MIDI + SysEx idea; their HTML filters on product string **`MTMComputer`**; this card uses **`Workshop Grids`** in `usb_descriptors.c` (list all ports or match that substring).

## USB MIDI — match `20_reverb`, not bare Pico examples

**Reference firmware:** `Workshop_Computer/releases/20_reverb` (`reverb.c` `usb_worker`, `tusb_config.h`, `CMakeLists.txt`).

| Topic | What we do |
|--------|------------|
| **Core layout** | **Core 0:** `tud_task` / `tuh_task` + `Housekeeping()` (SysEx, deferred flash). **Core 1:** `GridsCard::Run()` (48 kHz). Same as reverb: USB on core 0, `audio_worker` on core 1. |
| **Clock** | `set_sys_clock_khz(200000, true)` at start of `main()`. |
| **TinyUSB config** | `tusb_config.h`: `CFG_TUSB_RHPORT0_MODE` = **device \| host**; host hub + MSC like reverb. **CMake:** `CFG_TUH_ENABLED=1`, link **`tinyusb_host`** + **`tinyusb_device`** + **`tinyusb_board`**. |
| **Init order** | ~**100 ms** delay → **`tud_init(0)`** *or* **`tuh_init(0)`** (see below) → **`board_init()`**. Do **not** use `stdio_usb_init()` with MIDI-only descriptors (CDC conflicts). |
| **Runtime role** | Same gpio/board rules as reverb: `USB_HOST_STATUS` (GPIO 20) + `HardwareRevision()` (`Proto1` / `Proto2_Rev1` always device path; Rev1.1 uses pin to choose **UFP→PC = device** vs **DFP = host**). Wrong physical port ⇒ no MIDI device to the computer. |
| **Flash / RAM** | **`pico_set_binary_type(copy_to_ram)` removed** — run from flash like reverb. |
| **Multicore lockout** | **`multicore_lockout_victim_init()`** only on **audio core** (core 1). Flash writes from `ConfigStore` use `multicore_lockout_start_blocking` / `end` on core 0. |

## Pitfalls already hit

1. **`multicore_lockout_victim_init()` before `multicore_fifo_pop_blocking()` on core 1** — the lockout handler becomes the SIO FIFO IRQ handler and **discards** FIFO words that are not lockout handshakes, so the `GridsCard*` pushed from core 0 is consumed and lost; core 1 hangs in `pop_blocking()` forever while core 0 USB/MIDI still runs. **Fix:** `pop_blocking()` first, then `multicore_lockout_victim_init()`, then `Run()`.
2. **`stdio_usb_init()` + custom MIDI `usb_descriptors.c`** — breaks enumeration; removed.
3. **`pico_stdio_usb` linked** without CDC — useless `#warning`; removed from link.
4. **`tud_task()` ~1 kHz only** — too slow for reliable FS enumeration; spin `tud_task` in a tight inner loop (still used inside the 1 ms slice pattern on core 0).
5. **USB on wrong core vs reverb** — Workshop shipping layout is **USB core 0**, audio core 1; `midi_device` example is the opposite.
6. **Device-only TinyUSB** on Workshop — insufficient vs **reverb’s device+host build + runtime `tud` vs `tuh`** for correct PHY/port behavior.

## Swing timing

- **`cfg.swing`** is stored as **50–75** (percent, sequencer-style: 50 straight, 75 strong shuffle).
- Applied in **`GridsCard::ProcessSample`** for the **internal** clock only: `InternalClockSpacingSamples()` uses `amount = swing − 50` (0…25) so skew runs **0 … nominal/4** on alternating steps; average spacing stays nominal. **`GridsEngine` step order is unchanged**. External `PulseIn1` ignores swing.

## Key files

- `main.cpp` — boot 200 MHz, core split, USB init branch, USB loop.
- `tusb_config.h` — rhport0 device+host, class enables (mirror reverb).
- `usb_descriptors.c` — VID/PID `0x2E8A` / `0x10C1`, MIDI class, strings.
- `GridsCard.cpp` — `HandleIncomingSysEx`, `tud_midi_*` (device mode only).
- `GridsCard.h` — **`HardwareRevision()`** for USB role logic from `main.cpp`.
- `CMakeLists.txt` — TinyUSB defs, `tinyusb_host`, no `copy_to_ram`.

## `00_Simple_MIDI`

Arduino-based in this repo — **not** comparable to Pico TinyUSB bring-up; use **`20_reverb`** as the Workshop Pico reference.
