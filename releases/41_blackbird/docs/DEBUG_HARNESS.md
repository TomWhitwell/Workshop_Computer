# Debug harness

A debug build of blackbird that a host can drive over SWD while the card is
running: read performance counters, run Lua, type into the REPL as druid would,
and fake the hardware inputs. It exists for repeatable stress and coverage
tests (`tests/`), run with `util/bbdbg.py`.

Nothing here is in the release firmware: it is all behind the
`BB_DEBUG_HARNESS` CMake option.

## Quick start

You need a debug probe on the card's SWD pins, and OpenOCD running on the host
with its GDB server on port 3333 (for example `openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg`).
From the dev container, the host is `host.docker.internal`.

```sh
make flash-debug          # build build-debug/blackbird_debug.elf and load it over SWD
make test                 # run tests/ (stress tests take ~10s each)

util/bbdbg.py stats --watch 1
util/bbdbg.py eval "return input[1].volts"
util/bbdbg.py input cv1=2.5 pulse1=square:100hz switch=up
util/bbdbg.py input --clear
util/bbdbg.py repl "^^k"
util/bbdbg.py log --follow           # everything the card writes to USB
```

`make test TEST_ARGS="-k stress --repeat 5 --report results.json"` passes
arguments through. `BB_STRESS_SECONDS=60 make test` runs the stress tests for longer.
Tests that write flash only run with `--allow-flash`.

In VS Code, "Blackbird: Debug harness external OpenOCD" (cortex-debug) loads the
debug build. Its live watch can show `bb_dbg` while the card runs.

## How it works

`main.cpp` defines `bb_dbg`, a struct in RAM. OpenOCD reads and writes RAM
through the debug port without halting the cores (`read_memory` /
`write_memory`), so the card keeps running normally while the host talks to it.

- **Stats.** Core 0 refreshes them every main loop iteration:
  - worst-case and overrun counts for `ProcessSample`
  - main loop worst case and iteration count
  - Lua memory and its peak
  - free C heap (`heap_free_bytes`) and failed Lua allocations
    (`lua_alloc_failures`, each one retried after an emergency GC)
  - drop counts for every event queue, USB output and paced MIDI echoes
  - how many times each output's value changed (`out_changes_1..4`), counted
    at main loop rate so short pulses register
  - the Lua small-object pool: pages, bytes used and capacity, and free arena
    pages
  - Lua allocation sizes: live objects, live bytes and allocations ever made,
    per size bucket, via `Blackbird.alloc_histogram()`

`Blackbird.heap_map()` walks newlib's heap over SWD and gives the real
fragmentation picture: chunks in use, free chunk sizes, and the largest free
block.
- **Commands.** The host writes `script`, `script_len` and `cmd`, then
  increments `cmd_seq`. Core 0 runs the command in the main loop, on the same
  thread as Lua, and sets `ack_seq` when it is done.
  - `EVAL` runs `script` as a Lua chunk. `print()` output and returned values
    go to `out`. Errors go to `out`, with a non-zero `status`.
  - `REPL` feeds `script` into the USB receive queue in 64-byte packets, so the
    REPL handles it exactly as it would bytes from druid. That includes `^^`
    commands, multi-line blocks and script uploads. Send `^^s`/`^^e`/`^^w` in
    their own command, as druid sends them in their own packet.
  - `RESET_STATS` zeroes the worst-case and overrun counters.
- **Audio-rate probes** (debug builds only, read and armed over SWD):
  - `g_slope_starved_samples[4]`: samples each output held because a
    lookahead ASL segment ended before core 0 queued the next (an audible
    stall), with the times of the last 32 stalls in `g_slope_starve_times`
    (`time_us_32()`, output index in the low bits) and their count in
    `g_slope_starve_count`
  - `g_slope_seg_starts`: the sample positions where output 1's next 64
    segments start, armed by zeroing `g_slope_seg_start_count`
  - `g_out_capture`: 512 consecutive samples of one output as the ISR sends
    them (volts in 1/4096ths), for the output in `g_out_capture_channel`
    (0-3), armed by zeroing `g_out_capture_count`. See `capture_output()` in
    `tests/test_audiorate.py`.
- **Log.** Everything written to USB CDC is mirrored into the `log` ring (the
  link wraps `tud_cdc_n_write`). `print()` from callbacks only reaches the log
  when the card's USB is connected to a host, because blackbird skips printing
  otherwise. Errors always reach it.
- **Inputs.** `bb_dbg.inputs` is passed to `ComputerCard::SetInputOverrides`.
  At the end of input processing each sample, `BufferFull` replaces overridden
  audio, CV, pulse, knob and switch values. Edge detection, `Connected()`,
  input detection and `ProcessSample` all then see the fake values. Each input
  can be held constant, or driven by a square or triangle generator that runs
  on the card, so pulse trains and sweeps are sample-accurate. A jack with an
  override reports as connected unless `set_connected` says otherwise.

The struct layout is duplicated in `util/bbdbg.py`. `static_assert`s in
`main.cpp` pin the offsets, and the tool checks `version` and `struct_size` when
it connects. If you add fields, append stats into `reserved` and bump
`BB_DBG_VERSION`.

## Writing tests

Tests are `tests/test_*.py` functions that take a `bb` argument (see
`Blackbird` in `util/bbdbg.py`). No pytest is needed. Before each test the runner
clears input overrides, sends `^^k` and resets the counters. After each test it
fails the test if:

- `ProcessSample` overran (set `bb.allow_overruns` to allow some)
- the main loop heartbeat stopped
- the USB log contains Lua errors or a crash (set `bb.allow_log_errors`)

```python
def test_metro_survives_input_storm(bb):
    bb.eval("n = 0; metro[1].event = function() n = n + 1 end; metro[1].time = 0.01; metro[1]:start()")
    bb.square("cv1", 300, low=0, high=3)
    bb.wait(5)
    assert bb.query("n") >= 450
```

Mark tests that write flash with `@writes_flash` (from `bbdbg`). Mark known
issues with `@expected_failure("why")`; they report as XFAIL instead of failing
the run, and as XPASS once fixed. To skip a test, raise `bbdbg.Skip("why")`
from inside it.

## Script compatibility: bbbowery and bowery

`make test-bowery` runs every script in `bbbowery/` and
[monome/bowery](https://github.com/monome/bowery) on the card. That covers
bowery's top-level scripts and `snippets/`; set `BB_BOWERY_LEGACY=1` to add
`legacy/`. Bowery is fetched at a pinned commit (`BOWERY_REF` in
`tests/test_bowery.py`) into `tests/.cache/`. Set `BB_BOWERY_DIR` to use your
own checkout.

Each script is uploaded the way druid's `r` does (`^^s`, the script, `^^e`).
It then runs for `BB_BOWERY_SECONDS` (default 5) with generic fake inputs:

- clocks on CV 1 and both pulse inputs
- a sweep on CV 2
- knobs part-way

It passes if it loads and runs without Lua errors, overruns, a stalled main
loop or dropped events. Which outputs moved is reported, not asserted.

Known problems go in `EXPECTED` (XFAIL) or `SKIP` at the top of
`tests/test_bowery.py`, with the reason. If a script needs particular input to
do anything, add it to `STIMULUS_FOR`. `--report results.json` saves the
whole compatibility table.

## ii

`tests/test_ii.py` (leader) and `tests/test_ii_follow.py` (follower) need a
crow on the ii bus: the Computer's TX pad to crow's SDA, RX to SCL, sharing
ground and with crow's pull-ups on (its default). Patch crow output 1 to CV
In 1 and CV Out 1 to crow input 1, with nothing else in crow input 1. Without
a crow answering, the crow tests skip.

- `test_ii` wants crow idle (`^^k` in druid).
- `test_ii_follow` needs crow running `tests/crow/ii_follow_driver.lua`. Run
  it with druid's `r`, not `u`: a driver saved with `u` starts at boot and
  answers every rising edge on crow input 1, so later tests that put an
  audio-rate LFO on CV Out 1 flood blackbird with ii commands and fail.

## nb_drumcrow

`tests/test_drumcrow.py` plays and stresses nb_drumcrow's crow script, one of
the heaviest around. nb_drumcrow has no licence, so it isn't in this repo: the
test fetches it at a pinned commit into `tests/.cache/nb_drumcrow` (ignored by
git), or uses your checkout from `BB_DRUMCROW_DIR`, and skips if neither is
available. `BB_DRUMCROW_SECONDS` (default 15) and `BB_DRUMCROW_SOAK`
(default 30) set the run lengths.

## norns

`make test-norns NORNS_HOST=norns.local` tests blackbird against a real norns
(`norns.local` resolves inside the dev container; an IP address works too). Plug blackbird's USB into norns and keep the SWD probe on blackbird.
The tests run Lua on norns through matron's REPL websocket (port 5555, the one
maiden uses; client in `util/nornsws.py`) and check the other end over SWD:

- norns sees blackbird as a crow, and `crow.identity()` and `crow.version()` answer
- `crow.output[n].volts` and actions set from norns arrive on the card
- faked CV on the card reaches norns' `crow.input[n].stream`, `.change` and `query()` callbacks
- a function assigned from norns, called on crow, brings its arguments back
  intact (`_c.tell` and `quote`)
- norns flooding output volts (the wobblewobble case) causes no overruns,
  dropped USB input, or lost final value
- latency, timed on blackbird's microsecond clock (`bb_dbg_us()`, debug
  builds only). blackbird plays the keyboard as well as the endpoint: its USB
  port to norns carries MIDI too. It sends a note-on, norns' MIDI handler sets
  `crow.output[1].volts`, and blackbird times how long until output 1 lands.
  Three variants:
  - the MIDI path
  - a crow-serial baseline (`_c.tell` → norns → back), to show which leg
    costs what
  - the MIDI path in a busy session (LFOs and 100 Hz input streaming)

  Each reports min, median, p95 and max. A run fails if any note is lost, if
  p95 is over 10 ms (`BB_LATENCY_P95_MS`), or if the worst note is over 25 ms
  (`BB_LATENCY_MAX_MS`). `BB_LATENCY_NOTES` sets the count (default 50).
- a script corpus (`NORNS_SCRIPTS` in `tests/test_norns.py`: acid_test, awake,
  buoys, dunes, less_concepts, loom and wobblewobble, each set up to use crow)
  loads and runs for `BB_NORNS_SECONDS` with the
  generic fake inputs. It must produce no errors on norns, keep blackbird
  connected, and drop no events. Scripts not installed on norns are skipped.
  Add more with `BB_NORNS_SCRIPTS=dir/script.lua,...`, or in the table with
  setup Lua that points the script at crow.

The tests unload whatever script is running on norns. If norns can't be
reached, every norns test skips within about 2 seconds, and the rest of a
`make test` run carries on. `util/bbdbg.py norns "<lua>"` runs Lua on norns by
hand.

## Caveats

- **Don't press the card's reset button during a run.** `^^w` lights every
  LED and asks for a reset, but the tests clean up after themselves (`^^c`). A
  reset with OpenOCD attached leaves both cores halted. The harness says so
  when it happens; recover with a reflash.
- **HardFaults** on either core are captured by a RAM-resident handler
  (`g_fault_info`), and the runner fails the test with the faulting PC and LR,
  symbolized. This is how a vtable read from flash during flash writes was
  found. `test_flash.py` writes flash while core 1 is fully loaded, as the
  dynamic check for flash reads that `util/core1_flash_audit.py` can't see.
- **Memory headroom.** Lua objects of 64 bytes or less live in a small-object
  pool (`lib/lua_pool.c`), in a 64 KB arena reserved at boot. That keeps
  their churn out of the main heap: after the whole bowery corpus, the largest
  free block is 15 KB, where it used to be about 4 KB. The debug build has
  about 11 KB less heap than release, though, because of the harness buffers.
  Each `^^k` (and upload) unloads the libraries that load on first use
  (sequins, timeline, hotswap, ii: ~31 KB), so every test starts from the
  boot-time heap. Before that, a long run left them loaded and a big script
  after them (nb_drumcrow's) failed to compile. Check with
  `Blackbird.heap_map()`.

- **Reflashing while norns is connected.** After `make flash-debug`, norns
  may not notice blackbird re-enumerate. It keeps reporting
  `norns.crow.connected()` as true, but nothing gets through. Replug the
  norns ↔ Workshop Computer USB cable after each flash.
- **Input change detection** delivers every edge up to about 500 edges/s per
  input (a 250 Hz gate). Faster than that, edges are merged (one pending
  change per input), by design.

- **Halting.** OpenOCD may halt the target when a GDB client attaches.
  `bbdbg.py` resumes it at once, but while it is halted the ADC keeps
  sampling. The FIFO can overflow, which leaves the input mux misaligned until
  reset. To avoid the halt, either:
  - use the Tcl server: `--transport tcl` (port 6667: `scripts/start_openocd_host.sh`
    moves it there, leaving 6666 for druid's websocket)
  - start OpenOCD with `-c "rp2040.core0 configure -event gdb-attach {}"`

  Don't set breakpoints during a timing test.
- **RAM.** The harness takes about 16 KB of RAM (script, output and log
  buffers), so the debug build has less Lua heap than release. Memory-limit
  tests are slightly pessimistic.
- **Timing.** The debug build uses the same optimisation as release, so timings
  are representative. Polling the mailbox and refreshing stats adds a few
  microseconds per main loop iteration on core 0. It adds nothing to core 1:
  when nothing is overridden, the input override hook is a single pointer
  check.
