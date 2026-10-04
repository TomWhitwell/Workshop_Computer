# Bootleg Radio (538)

A lo-fi **bootleg tribute** to Music Thing Modular's **Radio Music** — not a
replacement, a cheaper, grubbier cousin. Load a USB stick with WAV files, plug it
into the Workshop Computer's front USB-C jack, and tune through them like radio
stations. The name comes from **538**, the AM frequency the pirate station Radio
Veronica used off the Dutch coast; **Bootleg** also nods to the Botlek, the
Rotterdam docklands where sub- and pirate culture ran thick.

The DAC is 12-bit and files are resampled with simple linear interpolation, so
expect character rather than fidelity — which is rather the point.

## Quick start

1. Format a USB stick as **FAT32 or exFAT**. The usual macOS/Windows defaults are
   fine — see [Supported USB sticks](#supported-usb-sticks).
2. Copy **uncompressed 16-bit PCM WAV files** (mono or stereo, any sample rate up
   to 192 kHz) into the **root directory**, and/or into **subfolders**. Each
   subfolder that contains WAV files becomes a **bank**.
   - The official Music Thing
     [Radio Music Suggested Audio](https://www.musicthing.co.uk/Radio_Music_Suggested_Audio/)
     page is a great set of loops and samples to load onto the stick first — it
     is the recommended starting content for trying the card.
3. With the Computer powered off, plug the stick into the **front USB-C jack**.
   This needs a **Rev 1.1 or later** board (the Computer acts as USB host), and
   nothing else may share that socket.
4. Insert the card and power up. While a stick is present but not yet readable,
   **all six LEDs blink** together; once the filesystem is readable they stop and
   **LED 4** lights. (Reading the stick can take a moment after it enumerates.)
5. Turn **Main** (and/or patch **CV in 1**) to pick a station. Hold the switch
   **up** to change bank, or flick it **down** to retrigger the loop.

> The root directory is **always bank 0**, even if it holds no WAV files. If it
> is empty the card starts silent — hold the switch up and turn Main to reach
> your folders.

## Controls

| Control | Function |
|---------|----------|
| **Main** knob | Station selection in the current bank; **bank** selection while the switch is Up |
| **CV in 1** | Station/bank selection, added to Main (patch for remote tuning) |
| **X** knob | Tape speed: −2 to +2 octaves, exponential, centre = normal speed |
| **CV in 2** | Tape speed, added to X |
| **Y** knob | Loop start point, as a fraction of the file |
| **Audio in 1** | Loop start point, added to Y (DC-coupled, so it works like a CV) |
| **Audio in 2** | Loop length, as a fraction of the file (unpatched = whole file) |
| **Pulse in 1** | External clock for the CV outs (unpatched = internal 120 BPM) |
| **Pulse in 2** | External retrigger, like a short switch-down tap |
| **Pulse out 1** | Loop gate: high while looping, low during the wrap gap (settable) |
| **Pulse out 2** | Loop wrap tick: a short pulse each wrap (settable) |
| **CV out 1** | Clocked random stepped CV (new value each clock pulse) |
| **CV out 2** | Clocked bipolar triangle LFO (one cycle per 40 clock pulses = 20 s at 120 BPM) |
| Switch **down** | Short tap = retrigger to loop start; hold ~1 s = toggle Loop/Sync mode |
| Switch **up** | Hold = select bank with Main/CV1 (new bank starts at its first station) |
| Switch **middle** | Normal playback |

Tape speed links pitch and time: slowing the card plays lower too. There is no
separate time-stretch or "90s mode".

## Inputs and outputs

- **CV in 1** — station/bank selection, added to Main. This is how you tune by
  voltage.
- **CV in 2** — tape speed, added to X.
- **Audio in 1** — loop start point, added to Y. It is DC-coupled, so a steady
  voltage works just like a CV.
- **Audio in 2** — loop length, as a fraction of the file. Unpatched, the loop
  runs to the end of the file.
- **Pulse in 1** — clock for the two CV outputs. Unpatched, they use the internal
  120 BPM clock.
- **Pulse in 2** — external retrigger, the same as a short switch-down tap.
- **Audio out 1 / 2** — left and right. Mono files appear on both.
- **Pulse out 1** — loop gate: high while the loop plays, low during the wrap gap.
- **Pulse out 2** — loop wrap tick: a short pulse each time the loop wraps.
- **CV out 1** — clocked random stepped CV.
- **CV out 2** — clocked bipolar triangle LFO.

The CV inputs are **bipolar**: a unipolar 0–10 V source reaches only the upper
half of the range. The audio inputs used for loop start behave the same way.

## Banks

Files are organised into **banks**: the **root directory is bank 0**, and each
**subdirectory that contains WAV files is another bank** (up to 32).

- To change bank, **hold the switch up** and turn **Main** (or patch **CV in 1**).
  The new bank starts at its first station, and nothing changes until the knob
  moves. Release the switch to go back to selecting stations.
- The card powers up on the **`bank` setting** from `settings.txt` (default 0 =
  the root), and starts on that bank's **first station**. The Main knob does not
  override the start bank until you actually turn it.
- The root is **always bank 0**, even if it holds no WAV files.
- Banks and the stations inside them are **natural-sorted**: case-insensitive,
  with runs of digits compared as numbers, so `station2` comes before
  `station10` — the same order you see in Finder. Root stays first.
- A bank holds up to **64 stations**; any beyond that are ignored.

## Looping

A station loops over its **window**: it plays from the **loop start** (Y /
Audio in 1) for the **loop length** (Audio in 2), then wraps back to the start.
Unpatched, the length is the whole file, so the loop runs from the start point
to the end and back.

The card never advances on its own. You move it with **Main**/**CV in 1**
(station), **switch up** (bank), or **switch down** (retrigger).

> **Loop seam.** Wrapping re-reads the file from the USB stick, so there is a
> very short gap at the moment of wrap. A click-free crossfade is a future
> improvement. There is also no reverse playback or crossfade between stations
> yet.

## Tune modes (Loop vs Sync)

Both modes loop the station. The mode only decides **where a new station starts**
when you tune (Main/CV1) or change bank (switch up):

- **Loop** — the new station starts at its **loop start**.
- **Sync** — the new station starts at the **same proportional position** as the
  old file (synchronised station jumping), clamped into its window. Sweeping the
  station knob or CV glides through the stick's material in place. **This is the
  default.**

To toggle, **hold switch-down for about a second**. The whole panel blinks
**2 times for Loop** and **3 times for Sync**. A short tap still retriggers.

## The built-in LFO and random CV

The two CV outputs are a clocked modulation pair, so you can self-patch the card
with no external LFO. They share a clock: the internal **120 BPM** by default, or
an external clock patched into **Pulse in 1** (the tempo glides to the new rate,
and falls back to 120 BPM if the clock stops). They run whether or not a file is
playing.

- **CV out 2** is a bipolar **triangle**, one cycle every **40 clock pulses**
  (20 seconds at 120 BPM).
- **CV out 1** is a **random stepped** voltage, a new value on **every clock
  pulse** (2 Hz at 120 BPM).

Patch them back into any input to bring the card to life — see
[Patch ideas](#patch-ideas).

## Settings (`settings.txt`)

The card has no screen or editor (the USB socket is the host port for the stick),
so a few "set and forget" behaviours live in an optional **`settings.txt`** file
in the **root** of the stick. A ready-to-copy example is included as
[`settings.example.txt`](settings.example.txt). It is read once each time the
stick is mounted (only after the filesystem mounts and the root directory can be
listed); all lines are optional:

```ini
# settings.txt - all lines optional
mode = sync        # power-on tune mode: loop | sync
bank = 0           # bank to start on (0-31)
clockBpm = 120     # internal clock tempo (30-240)
triPulses = 40     # clock pulses per triangle cycle (1-256)
pulse1 = gate      # Pulse out 1: gate | tick | clock | off
pulse2 = tick      # Pulse out 2: gate | tick | clock | off
led = verbose      # LED mode: verbose | quiet
```

Rules and defaults:

- `key = value` lines; `#` and `;` start comments. Unknown keys and blank lines
  are ignored.
- A missing file, or an out-of-range/unknown value, keeps the default — so with
  no file the card behaves exactly as described here.
- **Pulse-out modes:** `gate` = high while looping; `tick` = a short pulse each
  wrap; `clock` = the active clock pulse; `off` = always low.
- **LED modes:** `verbose` shows level, playing, station-change and fault;
  `quiet` shows only level, playing and mounted (LEDs 3 and 5 stay dark).

## LEDs

| LED | Meaning |
|-----|---------|
| 0 | Left output level |
| 1 | Right output level |
| 2 | Off when idle; **steady** while playing in **Sync** mode; **blinking** while playing in **Loop** mode |
| 3 | Blinks ~0.5 s when the station changes; steady with LED 4 = no WAV files (or none playable). Dark in `quiet` LED mode |
| 4 | USB stick detected |
| 5 | Underrun (the stick can't keep up); steady with LED 4 = filesystem would not mount. Dark in `quiet` LED mode |

### Reading the LEDs when something is wrong

There is no console on a USB-host card, so the LEDs carry the diagnosis:

- **LED 4 + 3** — mounted, but no `.wav` files were found in the selected bank.
  (If you started on an empty bank 0, hold the switch up and turn Main to reach a
  folder.)
- **LED 4 + 3 + 5** — WAV files found, but none could be decoded (they must be
  uncompressed 16-bit PCM, mono or stereo).
- **LED 4 + 5** — the stick was detected but its filesystem would not mount.
- **LED 4 + 2** — playing normally.

## Supported USB sticks

The card reads an unusually wide range of sticks:

| | Supported |
|---|---|
| **Filesystem** | FAT32, exFAT |
| **Partition scheme** | Master Boot Record (MBR), GUID Partition Map (GPT), or whole-disk "superfloppy" (no partition table) |
| **Sector size** | 512, 1024, 2048 or 4096 bytes |

Notes:

- macOS Disk Utility defaults to a GUID Partition Map, which is fine.
- For a GPT stick, the data volume must be the **Microsoft Basic Data** partition
  type — that is what macOS and Windows use for FAT/exFAT volumes, so a normally
  formatted stick already qualifies.

## Patch ideas

- **Slow station sweep (self-patched).** Patch **CV out 2** (triangle) into
  **CV in 1**. The card sweeps through the stations in the current bank, one pass
  every 40 clock pulses (20 s at 120 BPM). Turn **Main** to choose the centre of
  the sweep.
- **Clock-synced random scanning.** Patch **CV out 1** (random stepped) into
  **CV in 1**, and clock the card via **Pulse in 1**. A new station is picked on
  every clock pulse — a random radio scanner locked to your patch.
- **Tape wobble / loop jumps.** Patch **CV out 1** into **CV in 2** for stepped
  speed changes, or into **Audio in 1** to jump the loop start point around on
  every clock. Slow **CV out 2** into **CV in 2** gives a smooth speed sweep.
- **Rhythmic wraps.** Patch **Pulse out 2** (wrap tick) into a percussion
  trigger, or back into **Pulse in 1** to clock the CV outs from the loop itself.
  Patch **Pulse out 1** (loop gate) into a VCA to mute the seam gap.

## Links

- [Radio Music Suggested Audio](https://www.musicthing.co.uk/Radio_Music_Suggested_Audio/)
  — the recommended set of loops and samples to load onto the stick.
- [Music Thing Radio Music page](https://www.musicthing.co.uk/Radio_Music/)

## Credits

- Radio Music is a Music Thing Modular module by **Tom Whitwell**. Thank you, Tom,
  for Radio Music and for the suggested audio sets. This card is an unofficial,
  lo-fi bootleg tribute to the idea, not a replacement for it.
- This card is adapted from **Chris Johnson's** `usb_msc_host` ComputerCard
  example, added to the Workshop_Computer repository in **PR #437**.
  `ComputerCard.h` is © Chris Johnson, MIT licensed.
- `fatfs/` is FatFs by **ChaN**; see `fatfs/LICENSE.txt`.

## Building from source

This card needs **TinyUSB 0.21 or later** (older versions read sticks too slowly
for audio). From this folder:

```bash
make
```

The built firmware is copied to `UF2/bootleg_radio.uf2`. Flash with the usual
Workshop Computer workflow (debug probe/OpenOCD or UF2 drag-and-drop). If you
build outside the repo dev container, point CMake at a newer TinyUSB with
`-DPICO_TINYUSB_PATH=/path/to/tinyusb`.

## Files of interest

- `main.cpp` — core 1 runs the 48 kHz audio and knob/CV/switch handling; core 0
  runs USB, FatFs and WAV parsing, and fills the ring buffer.
- `diskio.c`, `fatfs/` — FatFs glue for a TinyUSB host mass-storage device
  (built with `FF_LBA64 = 1` and a 4k `FF_MAX_SS` so GPT and 4096-byte-sector
  sticks mount).
- `tusb_config.h` — host-only TinyUSB config (mass storage, single device).
- `ComputerCard.h` — vendor copy of Chris Johnson's header-only card library.
