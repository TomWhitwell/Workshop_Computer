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
| **X** knob | Tape speed: −2 to +2 octaves, exponential, centre = normal speed. Range set by `speedRange` |
| **CV in 2** | Tape speed, added to X (same range) |
| **Y** knob | Loop start point, as a fraction of the file |
| **Audio in 1** | Loop start point, added to Y (DC-coupled, so it works like a CV) |
| **Audio in 2** | Loop end point, as a fraction of the file (unpatched = end of file). If the end is before the start, playback runs backwards |
| **Pulse in 1** | External clock for the CV outs (unpatched = internal 120 BPM) |
| **Pulse in 2** | **8mu TRS MIDI remote**, or external retrigger when no 8mu is heard; see below |
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
- **Audio in 2** — loop end point, as a fraction of the file. Unpatched, the end
  is the end of the file. If the end is before the start, the loop runs
  backwards.
- **Pulse in 1** — clock for the two CV outputs. Unpatched, they use the internal
  120 BPM clock.
- **Pulse in 2** — **8mu TRS MIDI remote**, or the external retrigger when no 8mu
  is heard. With `remote = auto` (the default) a gate patched here retriggers as
  before until the card sees a valid 8mu message, then the jack becomes the MIDI
  input. The switch down and the 8mu's top-left button also retrigger.
- **Audio out 1 / 2** — left and right. Mono files appear on both.
- **Pulse out 1** — loop gate: high while the loop plays, low during the wrap gap.
- **Pulse out 2** — loop wrap tick: a short pulse each time the loop wraps.
- **CV out 1** — clocked random stepped CV.
- **CV out 2** — clocked bipolar triangle LFO.

The CV inputs are **bipolar**: a unipolar 0–10 V source reaches only the upper
half of the range. The audio inputs used for loop start/end behave the same way.

## TRS MIDI remote (8mu)

The card can take a Music Thing **8mu** as a wired remote. The 8mu sends
ordinary MIDI CCs out of its 3.5 mm TRS jack; the card receives them on
**Pulse in 2**, so the USB-C socket stays free for the sample stick.

> **This is not a MIDI input.** It reuses the Pulse in 2 voltage input; there is
> no optocoupler and no current loop. See [It is not a MIDI input](#it-is-not-a-midi-input).

### Mapping

| 8mu control | MIDI | Function |
|---|---|---|
| Fader 1 | CC 34 | Tape speed |
| Fader 2 | CC 35 | Speed range |
| Fader 3 | CC 36 | Loop start |
| Fader 4 | CC 37 | Loop end (before the start = reverse) |
| Fader 5 | CC 38 | Station |
| Fader 6 | CC 39 | Loop mode: loop / one-shot / ping-pong |
| Fader 7 | CC 40 | Performance mode: off / scan / beat-repeat |
| Fader 8 | CC 41 | Pulse out 1 mode |
| Button 1 | Note 36 | Retrigger (on press) |
| Button 2 | Note 48 | Bank down (tap); tuning static off (hold) |
| Button 3 | Note 60 | Bank up (tap); tuning static on (hold) |
| Button 4 | Note 72 | Loop/Sync (tap); release the remote (hold) |

The 8mu's accelerometer (CC 42–49) is ignored, and its two side buttons only
change its own internal banks; they do not send anything.

### Taking over and releasing

F1–F5 are performance controls: speed, speed range, loop start, loop end and
station. F6–F8 configure the loop mode, the performance mode and pulse out 1.
The bottom row is therefore **STATION · LOOP MODE · PERFORM · PULSE 1**, while
the top two faders are the speed pair. Clock BPM and pulse out 2 live in
`settings.txt`; Pulse in 1 still overrides the tempo.

- Moving fader 1–5 takes over that parameter. The panel knob and CV for it are
  ignored from then on; **CV still adds** (station = fader + CV in 1, speed =
  fader + CV in 2, start = fader + Audio in 1).
- **Fader 2 is the speed range:** 0–7 uses `settings.txt` (`speedRange`), then
  off (8–37), fine (38–67), ±1 octave (68–97) and ±2 octaves (98–127). It has no
  panel knob; releasing the remote returns it to the `speedRange` setting.
- **Fader 3 is START, fader 4 is END.** If END is left of START the loop plays
  backwards; if END is right of START it plays forwards. The loop length is the
  distance between them.
- **Fader 5 is the station.** It has no dead zone: fully down is station 0.
  A bank change keeps its relative position, so 80 % through the old bank lands
  around 80 % through the new one. While F7 is in **Scan**, F5 sets the starting
  station and is then ignored until Scan is turned off.
- Loop end is fader-only while the remote owns it.
- Moving fader 3 or 4 changes the loop window. The card waits until you stop
  moving for about 250 ms, then retriggers once, so the new start/end (and the
  direction) is heard immediately instead of waiting for the old loop to end.
- Faders 2 and 6–8 have a **leftmost dead zone (0–7)** that means "use the panel /
  `settings.txt`". Above that:
  - F2: speed range — off (8–37), fine (38–67), ±1 octave (68–97), ±2 octaves
    (98–127).
  - F6: loop mode — one-shot (8–47), loop (48–87), ping-pong (88–127).
  - F7: performance mode — scan (8–27), beat-repeat (28–47), off (48–127).
  - F8: pulse out 1 mode — gate (8–37), tick (38–67), clock (68–97), off
    (98–127). Pulse out 2 mode and clock BPM stay in `settings.txt`.
- **F7 Scan** advances the station on the clock; **F7 Beat-repeat** retriggers
  the current loop on the clock. `perfDiv` divides the clock (1, 2, 4, 8, 16).
  The 8mu’s F7 0–7 zone uses the `performanceMode` setting.
- Clock BPM is no longer an 8mu fader: set `clockBpm` in `settings.txt`, or patch
  Pulse in 1. The 8mu’s F8 is now pulse out 1.
- **B2/B3 tap** changes bank on release. **Hold either for about 1 second** to
  turn the tuning static off (B2) or on (B3); the bank change is suppressed.
  This is a runtime setting change, so it persists after you release the remote
  and is not written to the stick. A settings reload that contains a `static`
  value overrides it; `static = off` in `settings.txt` remains the power-on
  value.
- **B4 tap** toggles Loop/Sync; the panel blinks 2 times for Loop and 3 for
  Sync. This becomes the new `panelMode`, so it persists after the remote is
  released. `mode = ...` in `settings.txt` remains the power-on value.
- **B4 hold for about 1.5 seconds**, or holding the panel switch **up for about
  2 seconds without changing bank**, releases the remote. The performance
  controls do **not** jump: the card keeps the sound where it is, and a panel
  knob takes over only once you actually move it past a small threshold. The
  performance faders go back to the panel and `settings.txt`; a mode or static
  change made from B4/B2/B3 is kept.
- Loop end has no panel knob. On release it goes back to Audio in 2 **only if
  something is patched there**; otherwise the value the fader set is kept.
- B2/B3 bank changes happen on release so that a hold can mean static instead.
  The only cost is the duration of the press.
- Static on/off feedback is one all-LED flash for off and four for on, distinct
  from the 2/3 mode flashes.

### Power-up and auto-detection

The 8mu sends every non-zero fader when it powers up. The card waits ~10 ms and
treats four or more different fader CCs in that window as a power-up snapshot,
not a performance move. The first deliberate single-fader move takes over.

`settings.txt` has a `remote` key:

- `remote = auto` (default): Pulse in 2 retriggers until a valid 8mu message has
  been heard, then becomes the MIDI input. Releasing the remote puts it back
  into retrigger service until the 8mu speaks again.
- `remote = on`: always the 8mu input; the retrigger jack is disabled.
- `remote = off`: always the external retrigger; MIDI is ignored.

A gate is very unlikely to produce a valid MIDI status byte, so patched clocks
and gates still work in `auto`. If you want to be certain the jack never changes
function, use `on` or `off`.

The LEDs give a short flash of LED 3 when a fader first takes over, and the
usual all-LED blink when the remote is released. There is no persistent "remote
active" light.

### Setting it up

1. In the 8mu editor, set the TRS mode to **Type A**. Type B puts the data on
   the ring, which is not connected by a mono jack.
2. Use a **stereo TRS-to-TRS cable** from the 8mu's MIDI out to **Pulse in 2**.
   Do not use a mono TS cable: it shorts the 8mu's 3.3 V source to ground.
3. Power the 8mu from its own USB charger. The Computer's USB socket is busy
   with the sample stick.
4. Move a fader. The first message from a performance fader takes that control
   over; faders 5–8 are live as soon as they are above their dead zone.

The 8mu's TRS port is **output-only**: the card cannot send anything back, so
there is no LED feedback, no handshake and no SysEx. This is a deliberate
one-way remote.

### It is not a MIDI input

This is a deliberate hack, not a MIDI input. The 8mu's TRS output is a **3.3 V
push-pull UART** — tip carries the data, the ring is held high — not the 5 V
current loop a MIDI receiver expects. Pulse in 2 is the card's **100 k/NPN
pulse input**, not an opto-isolated MIDI receiver. The card is simply reading a
3.3 V UART between tip and sleeve.

Two consequences:

- Use a **stereo TRS cable**. The ring has no contact in the Computer's mono
  jack, so it floats. A mono TS cable would short the 8mu's ring source to
  ground; its series resistors probably save it, but do not rely on that.
- There is **no galvanic isolation**. A phone charger or power bank for the 8mu
  avoids the usual ground loop; a grounded laptop can hum. A standard 5 V
  current-loop MIDI output will not work directly into this jack. Proper
  isolation would need a powered opto receiver between the 8mu and the card,
  which is not part of this build.

It works because the 8mu and the pulse input happen to meet in the middle: the
8mu drives a 3.3 V push-pull signal, and the pulse input's transistor threshold
is about 0.6 V. Do not generalise it to other MIDI gear. Bending the MIDI
circuit is rather in the spirit of the card.

The default 8mu CC numbers are 34–41 and the top buttons send notes 36, 48, 60
and 72. If the 8mu has been used as a Radio Remote, check its fader CCs in the
editor; if they differ, change the constants in `main.cpp` (`kCcFirst`, the
`kNote...` values) to match.

### Faceplate legend

A strip over the 8mu is enough. Mark the two halves clearly:

`SPEED · SPEED RANGE · START · END` — the top two are the speed pair

`STATION · LOOP MODE · PERFORM · PULSE 1` — F5 is performance; F6–F8 setup
(pulse out 2 and the clock tempo live in `settings.txt`)

and over the four buttons:

`RETRIG · BANK− / static off · BANK+ / static on · MODE / release`

The B2/B3/B4 secondary actions need a hold; the slash marks the long press.

### If nothing responds

- With the 8mu powered and idle, the tip should sit at about **3.3 V** relative
  to the sleeve. If it reads 0 V, the 8mu is in Type B, or its output is a
  current loop rather than the push-pull 3.3 V UART this input expects.
- Confirm the cable is stereo TRS at both ends, not mono TS.
- Confirm the 8mu's TRS CCs really are 34–41 and the button notes 36/48/60/72.
- Confirm the 8mu is powered; the Computer cannot power it while hosting the
  stick.
- If `remote = off` is in `settings.txt`, MIDI is ignored by design.

## Banks

Files are organised into **banks**: the **root directory is bank 0**, and each
**subdirectory that contains WAV files is another bank** (up to 32).

- To change bank, **hold the switch up** and turn **Main** (or patch **CV in 1**).
  The new bank starts at its first station, and nothing changes until the knob
  moves. Release the switch to go back to selecting stations.
- With the **8mu station fader (F5)** active, a bank change keeps the fader's
  relative position instead of starting at station 0; see the remote section.
- While the switch is up, **LEDs 0–4 show the selected bank in binary** (bank 0
  is all off, LED 5 stays dark). Release the switch to go back to the normal LED
  display.
- The card powers up on the **`bank` setting** from `settings.txt` (default 0 =
  the root), and starts on that bank's **first station**. The Main knob does not
  override the start bank until you actually turn it.
- The root is **always bank 0**, even if it holds no WAV files.
- Banks and the stations inside them are **natural-sorted**: case-insensitive,
  with runs of digits compared as numbers, so `station2` comes before
  `station10` — the same order you see in Finder. Root stays first.
- A bank holds up to **64 stations**; any beyond that are ignored.

## Looping

A station plays over its **window** between the **loop start** (Y / Audio in 1)
and the **loop end** (Audio in 2). If the end is after the start, it plays
forwards; if the end is before the start, it plays **backwards**. Unpatched, the
end is the end of the file, so the loop runs from the start point to the end and
back.

With `loopMode = one-shot` in `settings.txt`, the station plays its window once
and stops. A retrigger (switch down, the 8mu's top-left button, or Pulse in 2
when it is still in retrigger service) or a station change starts it again.

With `loopMode = ping-pong`, the station alternates: it plays the window in the
direction the start/end faders indicate, then the other way, and so on. The
first pass after a station change keeps the direction of the faders; the
Loop/Sync landing only applies to a forward first pass.

The card never advances on its own. You move it with **Main**/**CV in 1**
(station), **switch up** (bank), or **switch down** (retrigger).

Station changes and retriggers fade the output down over about 5 ms before the
new file is requested, then fade back in. That removes the hard cut at the
change; it adds about 5 ms of latency.

> **Loop seam.** Wrapping re-reads the file from the USB stick, so there is a
> short gap at the moment of wrap. The card fades out over the last few
> milliseconds and back in over the first few, so the seam is click-free, but it
> is a fade rather than a true overlapping crossfade. Reverse and ping-pong use
> the same fade at the pass boundaries, so a ping-pong turn has a small dip
> rather than a click.

## Tune modes (Loop vs Sync)

The Loop/Sync mode only decides **where a new station starts** when you tune
(Main/CV1) or change bank (switch up). `loopMode` separately decides whether the
station repeats.

- **Loop** — the new station starts at its **loop start** (START).
- **Sync** — the new station starts at the **same proportional position** as the
  old file (synchronised station jumping), clamped into its window. Sweeping the
  station knob or CV glides through the stick's material in place. **This is the
  default.**
- If the loop is **reversed** (END before START), a new station starts at its
  START, which is the window end; the Sync landing is not used in reverse.

To toggle, **hold switch-down for about a second**, or **tap B4** on the 8mu.
The whole panel blinks **2 times for Loop** and **3 times for Sync**. A short
switch tap still retriggers. The B4 choice becomes the new panel mode and
survives releasing the remote; `mode` in `settings.txt` is the power-on value.

## Tuning static

With `static = on`, changing station or bank adds a short filtered noise burst —
a radio-tuning crackle. It starts as the old station fades and fills the gap
while the new file loads; the new station rises out of it. `staticLevel` sets the
level from 0 to 100.

It fires on station and bank changes only, not on retrigger or the loop seam. It
is off by default.

On the 8mu, **hold B2 to turn it off and B3 to turn it on**. That is a runtime
change: it persists after the remote is released and is not written to the
stick. A settings reload that contains a `static` value overrides it. The panel
flashes all LEDs once for off and four times for on. `static` and `staticLevel`
in `settings.txt` still set the power-on state and level.

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
loopMode = loop    # station end: loop | one-shot | ping-pong
performanceMode = off # F7 performance: off | scan | repeat
perfDiv = 1        # clock pulses per scan/repeat step (1-16)
bank = 0           # bank to start on (0-31)
speedRange = 2oct  # speed range: 2oct | 1oct | fine | off
clockBpm = 120     # internal clock tempo (30-240)
triPulses = 40     # clock pulses per triangle cycle (1-256)
pulse1 = gate      # Pulse out 1: gate | tick | clock | off
pulse2 = tick      # Pulse out 2: gate | tick | clock | off
led = verbose      # LED mode: verbose | quiet
remote = auto      # Pulse in 2: auto | on | off (8mu remote)
static = off       # tuning static: off | on
staticLevel = 50   # static level (0-100)
```

Rules and defaults:

- `key = value` lines; `#` and `;` start comments. Unknown keys and blank lines
  are ignored.
- A missing file, or an out-of-range/unknown value, keeps the default — so with
  no file the card behaves exactly as described here.
- **Loop mode:** `loop` wraps at the end of the window; `one-shot` plays the
  window once and stops until retriggered; `ping-pong` alternates forward and
  reverse passes.
- **Loop direction:** there is no direction setting — the order of the loop
  start and loop end sets it. End after start is forward; end before start is
  reverse.
- **Speed range:** `2oct` is the original −2..+2 octaves; `1oct` is ±1 octave;
  `fine` is about ±1 semitone; `off` fixes playback at 1× and the speed controls
  do nothing. The 8mu's F2 overrides this while it is above its dead zone.
- **Performance mode:** `off` is normal playback; `scan` advances the station on
  the clock; `repeat` retriggers the current loop on the clock. `perfDiv` divides
  the clock by 1–16. The 8mu's F7 overrides this while it is above its dead zone.
- **Clock:** `clockBpm` sets the internal tempo; Pulse in 1 overrides it. The 8mu
  no longer has a clock fader; F8 is pulse out 1.
- **Pulse-out modes:** `gate` = high while looping; `tick` = a short pulse each
  wrap; `clock` = the active clock pulse; `off` = always low.
- **LED modes:** `verbose` shows level, playing, station-change and fault;
  `quiet` shows only level, playing and mounted (LEDs 3 and 5 stay dark).
- **Remote mode:** `auto` keeps Pulse in 2 as a retrigger until an 8mu is heard,
  then switches it to MIDI; `on` always uses the 8mu input; `off` always uses the
  external retrigger. See [TRS MIDI remote](#trs-midi-remote-8mu).
- **Tuning static:** `on` adds a short noise burst on station and bank changes;
  `off` is silent. `staticLevel` sets the level from 0 to 100.
- **Power-on vs runtime:** `mode` and `static` are the power-on values. A B4 tap
  changes the running Loop/Sync mode; a stick remount re-applies `mode`. A B2/B3
  hold changes the running static; it is not written back, and a settings reload
  that sets `static` overrides it. Neither remote change is written to the file.

## LEDs

| LED | Meaning |
|-----|---------|
| 0 | Left output level |
| 1 | Right output level |
| 2 | Off when idle (including after a one-shot finishes); **steady** while playing in **Sync** mode; **blinking** while playing in **Loop** mode |
| 3 | Blinks ~0.5 s when the station changes; steady with LED 4 = no WAV files (or none playable). Dark in `quiet` LED mode |
| 4 | USB stick detected |
| 5 | Underrun (the stick can't keep up); steady with LED 4 = filesystem would not mount. Dark in `quiet` LED mode |

While the switch is **up**, LEDs 0–4 instead show the selected bank in binary;
LED 5 goes dark. Bank 0 is all off.

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
- `midi_rx.pio` — PIO UART receiver for the optional 8mu TRS MIDI remote.
- `tusb_config.h` — host-only TinyUSB config (mass storage, single device).
- `ComputerCard.h` — vendor copy of Chris Johnson's header-only card library.
