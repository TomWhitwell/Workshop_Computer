# Diagnostics

Instrumentation for the GBA link. These are shipped with the release deliberately: the link runs
over jacks that were never meant to carry SPI, through a cable whose internal wiring is not
standardised, and **the failures are silent** — a transposed pair, a missing conductor and a
console that is simply not listening all present as "nothing happens". Guessing between them is
expensive. Measuring is not.

They are also how any of the numbers in [`POSTMORTEM.md`](POSTMORTEM.md) would be re-taken after
a regression.

## Building

**These are sources, not binaries.** From this directory:

```sh
cmake -G Ninja -B build -S .
cmake --build build --target cablecheck    # -> build/cablecheck.uf2
cmake --build build                        # or all of them at once
```

They reuse the parent applet's real transport (`../gba_multiboot.cpp`, `../gba_spi.pio`), so a
pass here exercises the same code path the instrument uses rather than a simulation of it.

## Which one

**If you are wiring this up for the first time, you want `cablecheck`.** Everything else is for
when something is wrong.

| Tool | The question it answers |
|---|---|
| **`cablecheck`** | **Which wire actually carries the GBA's SO.** Listens on Pulse In 1 and Pulse In 2 at once, so it settles the cable crossover by measurement. **Start here.** |
| `pin_toggle` | Do SC and SI physically reach the connector? A slow square wave you can meet with a multimeter, a scope or an LED. |
| `bringup` | The electrical layer in stages, picked with the 3-position switch: output swing, edges, levels. |
| `gba_probe` | The raw received word at selectable slow clocks — separates "input too slow" from "bits misaligned". |
| `handshake` | How far the multiboot handshake gets before it stops, and where. |
| `loopback` | The transport on its own, two stages on the toggle switch, with no console involved. |
| `linkcheck` | LED-only link check — no scope, no held switch. For when the bench is not to hand. |
| `mbrate` | Which multiboot clock rate transfers *repeatably*. One speed at a time, chosen by hand. |
| `linkrate` | How many words per second the post-boot link sustains cleanly, reported on the GBA's own screen. |
| `bandwidth` | **Superseded — kept as a record of what not to do.** Its automatic sweep judged "clean or not" itself, so a fault in the harness returned a bare zero indistinguishable from a real ceiling of zero. |

## The documents

| | |
|---|---|
| [`TESTPLAN.md`](TESTPLAN.md) | **The bench procedure. Work it from STEP 1.** |
| [`CABLES.md`](CABLES.md) | Per-cable-type continuity tables and the socket pinout |
| [`BRINGUP.md`](BRINGUP.md) | Accumulated hardware background |
| [`POSTMORTEM.md`](POSTMORTEM.md) | What went wrong during bring-up and how each fault was caught |

## The habit worth keeping

**When a measurement contradicts something already known to work, doubt the measurement first.**
Several days of this project went into faults that turned out to be in the diagnostics rather
than the hardware: a scope probe with both leads on the same net, a loopback with a structural
blind spot, a diagnostic that blocked the 48 kHz callback, and an instrument that corrupted the
very words it was counting. `POSTMORTEM.md` records each one, and `bandwidth` is kept in the
table above as the standing example.
