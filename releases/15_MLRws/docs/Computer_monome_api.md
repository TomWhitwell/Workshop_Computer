# Monome Grid for Workshop System Computer

This document describes the monome API used by the MLRws firmware on the
[Music Thing Modular Workshop System](https://www.musicthing.co.uk/workshopsystem/)
Computer card.

Compatible with modern mext grids, older series/40h protocol grids, and the
FTDI-based monome grids tested with this firmware. DIY grids that speak the same
serial protocols over CDC/FTDI should work too.

## Files

| File | What it does |
|---|---|
| `MonomeGrid.h` | Friendly C++ wrapper — the recommended API |
| `monome_ws.h` | Low-level reusable C API (grid/arc types, events, capability queries) |
| `monome_ws.c` | Protocol engine for mext, series, and 40h; TinyUSB host/device transport; CDC callbacks |
| `tusb_config.h` | TinyUSB configuration (CDC + FTDI host, DTR/RTS, 115200 8N1; also enables CDC device for USB serial) |
| `main.cpp` | MLRws card firmware (grid-driven sample looper) |
| `CMakeLists.txt` | Build configuration |

## Test builds

`MONOME_WS_FORCE_8X8_MONOBRIGHT=ON` forces a modern mext grid to present as an
8x8 binary grid while keeping the mext wire protocol. This is useful for testing
monobright UI behavior on modern hardware. In the local MLRws build, use:

```sh
make monobright
make flash-monobright
```

## API reference — MonomeGrid.h (C++)

MLRws uses the `MonomeGrid` wrapper in host mode, but it does not use the
`grid.begin()` convenience path. Instead, the firmware performs explicit
`monome_ws_init(...)` setup and runs the USB loop itself.

### Lifecycle

| Method | Description |
|---|---|
| `grid.begin()` | Convenience helper for standalone use. MLRws does not call this. |
| `grid.ready()` | `true` once grid dimensions are discovered |
| `grid.connected()` | `true` if a device is physically connected |
| `grid.cols()` | Grid width (0 before discovery) |
| `grid.rows()` | Grid height (0 before discovery) |
| `grid.supportsLevels()` | `true` when the attached grid supports 0-15 LED levels |
| `grid.protocol()` | Current backend protocol (`MEXT`, `SERIES`, `40H`, or `UNKNOWN`) |

### Events

MLRws calls `grid.poll()` during its UI-control pass, not every audio sample.
That happens at a reduced rate controlled by `MAIN_CTRL_DIV`.

| Method | Description |
|---|---|
| `grid.poll()` | Drain event queue. In MLRws, call during the UI-control tick. |
| `grid.keyDown()` | `true` if a key was pressed this sample |
| `grid.keyUp()` | `true` if a key was released this sample |
| `grid.anyHeld()` | `true` if any key is currently held |
| `grid.held(x, y)` | `true` if key (x, y) is currently held |
| `grid.lastX()` | X of the most recent key event |
| `grid.lastY()` | Y of the most recent key event |

### LEDs

LED changes are buffered and flushed by the USB loop on core 1. In MLRws,
redraws are built with frame-buffer helpers and submitted with
`grid.submitFrame()`.

| Method | Description |
|---|---|
| `grid.led(x, y, level)` | Set brightness (0–15) |
| `grid.ledOn(x, y)` | Set to full brightness |
| `grid.ledOff(x, y)` | Turn off |
| `grid.ledToggle(x, y)` | Toggle between off and full |
| `grid.ledSet(x, y, on)` | Set from a bool |
| `grid.ledGet(x, y)` | Read back current level |
| `grid.clear()` | All LEDs off |
| `grid.all(level)` | Set all LEDs to a level |
| `grid.fill(x0, y0, x1, y1, level)` | Fill a rectangle |
| `grid.row(y, level)` | Fill a row |
| `grid.col(x, level)` | Fill a column |
| `grid.intensity(level)` | Set global brightness (0–15) |
| `grid.showHeld(level)` | Light all currently held keys |
| `grid.frameClear()` | Clear the local frame buffer without submitting |
| `grid.frameLed(x, y, level)` | Set a frame-buffer LED with bounds checks |
| `grid.frameLedUnchecked(x, y, level)` | Set a known-good frame-buffer LED during redraw |
| `grid.submitFrame()` | Submit the accumulated frame when core 1 can accept it |

### Advanced / direct buffer access

| Method | Description |
|---|---|
| `grid.ledBuffer()` | Raw `uint8_t*` to the 16×16 LED buffer |
| `grid.markDirty()` | Mark all quadrants for refresh (after writing buffer directly) |

## Low-level C API — monome_ws.h

For developers who want full control. This is the path MLRws follows in its
USB/mode setup:

```cpp
#include "pico/multicore.h"
#include "bsp/board.h"
#include "tusb.h"

extern "C" {
#include "monome_ws.h"
}

// In your constructor:
monome_ws_init(MONOME_WS_TRANSPORT_HOST, 0);   // host mode, CDC interface 0
multicore_launch_core1(my_core1_func);

// Your core 1 loop:
void my_core1_func() {
    board_init();
    tusb_init();
    while (true) {
      monome_ws_task();     // USB host + monome polling (non-blocking)
        // ... your own core 1 work here ...
    }
}

// In ProcessSample or a lower-rate UI tick on core 0:
  monome_ws_event_t ev;
  while (monome_ws_event_pop(&ev)) {
    if (ev.type == MONOME_WS_EVENT_GRID_KEY) {
        // ev.grid.x, ev.grid.y, ev.grid.z (1=down, 0=up)
    }
}

  monome_ws_grid_led_set(x, y, level);  // buffer an LED change
  // monome_ws_grid_refresh() is called automatically by monome_ws_task()
```

### Key C functions

| Function | Description |
|---|---|
| `monome_ws_init(transport, cdc_itf)` | Initialise state (`MONOME_WS_TRANSPORT_HOST` or `_DEVICE`, CDC interface index) |
| `monome_ws_task()` | Poll USB + monome protocols (call from core 1 loop) |
| `monome_ws_connected()` | Device physically connected? |
| `monome_ws_protocol()` | Current backend protocol |
| `monome_ws_device_kind()` | Grid, arc, or unknown |
| `monome_ws_grid_ready()` | Grid ready? |
| `monome_ws_grid_supports_levels()` | Attached grid supports 0-15 LED levels? |
| `monome_ws_event_pop(&ev)` | Pop next event (returns `false` if empty) |
| `monome_ws_grid_led_set(x, y, level)` | Set LED (0–15; older binary grids are thresholded internally) |
| `monome_ws_grid_led_all(level)` | Set all LEDs |
| `monome_ws_grid_led_intensity(level)` | Global brightness |
| `monome_ws_grid_refresh()` | Flush dirty quads (auto-called by `monome_ws_task`) |
| `monome_ws_grid_frame_submit(levels)` | Submit a full 16×16 frame (double-buffered); returns `false` if busy |
| `monome_ws_grid_all_off()` | Send all-off command |
| `monome_ws_send_discovery()` | Re-send system queries |

### Event types

```c
MONOME_WS_EVENT_GRID_KEY   // ev.grid.x, ev.grid.y, ev.grid.z
MONOME_WS_EVENT_ARC_DELTA  // ev.arc.n, ev.arc.delta
MONOME_WS_EVENT_ARC_KEY    // ev.arc_key.n, ev.arc_key.z
```

## Architecture

```
Core 0                                Core 1
──────                                ──────
ComputerCard::Run()                   board_init() + tusb_init()/tud_init()
  └─ ProcessSample() @ 48kHz         └─ while(true) { monome_ws_task(); ... }
       │                                    │
       ├─ grid.poll() on UI tick            ├─ tuh_task()/tud_task() ← USB stack
       ├─ grid.keyDown() ...                ├─ CDC read/write        ← serial RX/TX
       ├─ frameClear/frameLed/submitFrame   ├─ discovery retry       ← if grid_x == 0
      └─ CVOut / PulseOut                  └─ monome_ws_grid_refresh() ← LED TX @ ~60fps
                    │                                 │
                    └── event queue ──────────────────┘
                        (lock-free SPSC ring)
```

## Hardware compatibility

| Grid type | USB chip | TinyUSB driver | Status |
|---|---|---|---|
| Grid (2024+) | RP2040 native | `CFG_TUH_CDC` | ✓ |
| Grid (2021–2023) | FTDI FT232R | `CFG_TUH_CDC_FTDI` | ✓ |
| Grid (older) | FTDI FT230X | `CFG_TUH_CDC_FTDI` | ✓ |
| Series (`m64`/`m128`/`m256`/`mk`) | FTDI | `CFG_TUH_CDC_FTDI` | Binary-grid backend |
| 40h / arduinome | FTDI | `CFG_TUH_CDC_FTDI` | Binary-grid backend |
| Neotrellis DIY | FTDI | `CFG_TUH_CDC_FTDI` | ✓ |
| Arc | Same as above | Same as above | Event API reserved; LED-ring output is not used by MLRws |

## Protocol notes

The driver uses mext when available and can fall back to older series/40h grid
protocols behind the same API. Key implementation details (learned from
[dessertplanet/viii](https://github.com/dessertplanet/viii) and
[monome/ansible](https://github.com/monome/ansible)):

- **LED commands are padded to 64 bytes** with `0xFF` for USB bulk packet alignment.
  This is essential for reliable operation across all hardware versions.
- Full-frame redraws are copied into a 16×16 frame buffer and flushed as dirty
  8×8 quadrants. Modern mext grids receive 0-15 level maps; series/40h grids
  receive binary maps derived from `level > 7`.
- The monobright test build also uses the binary `level > 7` path, but sends
  mext binary LED maps so modern grids can be used to validate 8x8 binary UI.
- **Discovery queries are sent unpadded** — FTDI grid firmware doesn't handle
  63 bytes of `0xFF` padding after a system query well.
- **DTR + RTS must be asserted** during USB enumeration. FTDI chips gate serial
  data through DTR — without it, the chip enumerates but won't pass bytes.
- **Line coding is set to 115200 8N1** automatically via
  `CFG_TUH_CDC_LINE_CODING_ON_ENUM`.
- The RX parser skips `0xFF` bytes between messages.

## Credits

- Protocol implementation modelled on [dessertplanet/viii](https://github.com/dessertplanet/viii)
  and [monome/ansible](https://github.com/monome/ansible)
- [monome](https://monome.org/) for the grid hardware and open mext protocol
- [ComputerCard](https://github.com/TomWhitwell/Workshop_Computer) by Tom Whitwell
