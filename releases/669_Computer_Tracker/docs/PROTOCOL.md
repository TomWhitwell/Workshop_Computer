# Workshop Tracker USB protocol (v8)

The card appears as a **USB-MIDI device** called **"Workshop Tracker"**
(VID `0x2E8A`, PID `0x10C1`). All editor traffic is **SysEx**; no notes or CCs
are used, so the port can sit next to other MIDI gear without side effects.

```
F0 7D 54 <cmd> <payload…> F7
   │  │
   │  └─ 0x54 'T' = Workshop Tracker
   └──── 0x7D = MIDI "non-commercial / educational" manufacturer ID
```

Every payload byte is 7-bit (0–127). All song values are designed to fit in
7 bits, so no nibble packing is needed. The only value that is bigger than 127
(tempo, 30–300 BPM) is split into `hi = bpm >> 7`, `lo = bpm & 127`.

Reference implementations: `src/engine/protocol.h` (firmware, unit-tested in
`tests/test_engine.cpp`) and `web/js/protocol.js` (browser). The two are
cross-checked byte for byte by `web/test/xcheck.test.mjs`. That test feeds the JS
upload stream through the real C++ parser and reads the song back. It also
decodes a Panel report built by the C++ code and compares the effective values
with the JS implementation.

## Song data model

| Item | Size | Notes |
| --- | --- | --- |
| Patterns | 16 | each 1–64 rows (`pat_len`) |
| Tracks | 8 | 0–3 drum voices, 4–5 CV/gate, 6–7 IN1/IN2 (VCAs of the audio inputs) (v3; v2: 6) |
| Cell | 4 bytes | `note, vel, fx, fxv`: one effect per step (v3 / fw 0.3.0 had 6 bytes with a second effect) |
| Song (arrangement) | 32 entries | `{pattern, repeats 1–16}`, `song_len` 1–32 |
| Header | 212 bytes | see the table below |
| Whole song | 32,980 bytes | header + 16×8×64×4 cells (v3: 49,364; v2: 24,788) |

**Cell**

* `note`: 0 = empty, 1–126 = MIDI note, 127 = OFF (choke drum / gate off / close the IN VCA).
  On IN tracks any note is just a trigger (the editor shows `TRG`).
* `vel`: 0 = default, 1–127. Drums: hit strength (default 100). IN tracks: VCA
  level (default = full). **CV tracks: always 0** (v5 / fw 0.3.2): a CV gate is only
  on or off. The firmware stores 0 for any CV `vel` it receives or loads, and an
  IN VCA that follows the CV gate opens fully (the mixer level sets the volume).
* CV `note` range (v5): **C0–B5 = MIDI 12–83 = 0.000–5.917 V** at 1 V/octave,
  C0 = 0 V, so the octave number in the note name is the voltage
  (`C-3` = 3.000 V, `A-1` = 1.750 V). Only positive voltages: notes outside are
  folded by whole octaves into that range (`F-6` → `F-5`, `C-(-1)` → `C-0`), and
  so is the output after the track's transpose and CV in 1, so it never goes
  below 0 V or above 5.917 V (the pitch class is kept).
* `fx`: 0 = none or an ASCII letter (below); `fxv` 0–127.

| fx | Tracks | Meaning |
| --- | --- | --- |
| `A` | drum | lock param A for this hit |
| `B` | drum | lock param B |
| `D` | drum, IN | lock decay (IN: VCA release for this trigger) |
| `G` | CV, IN | gate length in 1/16 row (0 = hold until the next note/OFF) |
| `S` | CV | slide: glide amount, gate stays high (legato) |
| `R` | all | ratchet: 2–8 evenly spaced hits in the row |
| `P` | all | probability in % (0–99) |
| `K` | all | delay by n/16 row (1–15) |
| `W` | drum, IN | (fw 0.4.1) reverb send for this hit, 0–127 with the send-knob law; replaces the channel's `fx_send` until the next hit on that track (a hit without `W` returns to `fx_send`); glides over ≤ 5 ms; CV: ignored |

The editor shows and takes every value as two decimal digits: `vel` and the
`A B D S W` values are scaled 00–99 ↔ 0–127 (exact round trip), the `G R P K`
values are the plain numbers.

**Header layout** (offsets in bytes; mirrors `struct SongHeader` in `song.h`)

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 12 | name (ASCII, NUL-padded) |
| 12 | 2 | tempo hi, lo |
| 14 | 1 | swing 50–75 (% of the row pair given to the first row) |
| 15 | 1 | song_len |
| 16 | 1 | cv_zero_note: MIDI note that outputs 0 V. **Always 12 (C0) since v5**; older firmware used it as a setting (default 36 = C2). |
| 17 | 2 | macro_excl[2] (v2): PERFORM macro routing, [0] = decay macro (Main knob), [1] = colour macro (X knob). **Bit v set = drum voice v is *not* affected.** Stored inverted so that 0 (old songs, v1 flash slots) means "all voices". |
| 19 | 2 | macro_cvin[2] (v3): routing of Main [0] / X [1] to the CV and IN modules, **bit set = routed**: bit 0/1 = CV1/CV2 (Main: gate length, X: glide), bit 2/3 = IN1/IN2 (Main: VCA decay, X: drive). New songs: 0x0F. Songs migrated from v2: 0. |
| 21 | 6 | fx_send[6] (v6): reverb send of DR1–4, IN1, IN2, 0–127 (square law: 64 ≈ −12 dB, 127 = 0 dB, post level/mute) |
| 27 | 1 | fx_ver (v6, 2 since v8): 2 = the reverb fields and drum bytes 6/7 are valid. 1 (a 0.4.0–0.4.2 song) keeps its reverb fields, drum bytes 6 and 7 are set to 0. Anything else (an older song: these bytes were reserved = 0) is migrated on load/receive: all sends 0, reverb parameters to their defaults, drum bytes 6/7 = 0. Afterwards fx_ver = 2, so an old song sounds exactly as before. |
| 28 | 16 | pat_len[16] |
| 44 | 64 | entries[32] {pattern, repeats} |
| 108 | 32 | drum[4][8]: engine, decay, A, B, tune (64 = 0), vel-sens, (v8) click/grit, – |

Drum param 5, **vel-sens**: `level = 127 − (127 − vel) × sens / 128` (0 = every
hit at full level, 127 = the step velocity sets the level fully, default 100).
Drum param 6 (v8), **x**: Kick = click level, every other engine = grit
(0 = off, the sound of 0.4.2). Drum param 3 (B) of the Snare is a continuous
snare → clap morph since v8: ≤ 44 snare, ≥ 84 clap (both as in 0.4.2), 45–83
blended (0.4.2: a switch at 64).
| 140 | 16 | cv[2][8]: gate (1/16 row, 0 = tie), glide, transpose (64 = 0), … |
| 156 | 48 | mix[6][8]: level, pan (64 = C), drive, tone (64 = flat), VCA decay, mute (≥64), vcaSrc, inGate. `vcaSrc` and `inGate` are only used on the IN channels 4–5 (v3). |
| 204 | 8 | master: level, drive, filter (64 = off), then (v6) reverb **return** (default 96; 90 ≈ 0 dB, 127 = +6 dB), **size** (64; delay lengths 40–100 %), **decay** (64; T60 = 0.3·30^(v/127) s = 0.3–9 s), **tone** (56; damping, low = dark), **lo-fi** (40; bits dropped = v/16, wobble depth = v/8 samples, sample-and-hold output from 64) |

**IN channel VCA** (mix[4/5]): `vcaSrc` 0 = AUTO (v2 behaviour: follows CV/gate
track n when the VCA decay is > 0, else open), 1 = OPEN, 2 = CV GATE (follows
CV/gate track n at full level; v4 and older: its velocity = level), 3 = IN TRACK (the IN track's TRIG/LVL).
`inGate` = how long an IN trigger keeps the VCA open, in 1/16 row (0 = until
the next trigger/OFF, default 8). The VCA decay is the release time after the gate.

The firmware sanitises every incoming value (`header_sanitize`, `set_cell`),
so a buggy or malicious host cannot put the engine in a bad state.

## Commands: web → card

| Cmd | Name | Payload |
| --- | --- | --- |
| 01 | Hello | – → card answers **Info** |
| 02 | GetSong | – → **SongHeader**, 512 × **PatternChunk**, **DumpEnd** (fw 0.2: 384) |
| 09 | Ping | – (keep-alive; while pings arrive at least every 3 s the card streams **Status** every 50 ms) |
| 10 | SetCell | pat, track, row, note, vel, fx, fxv (7 bytes; the 9-byte form of a 0.3.0 editor is accepted, see Compatibility) |
| 11 | SetCells | pat, track, row0, count (≤16), count × 4 cell bytes (count × 6 from a 0.3.0 editor is accepted) |
| 12 | SetPatLen | pat, len |
| 13 | SetDrum | voice, param, value |
| 14 | SetCv | track (0–1), param, value |
| 15 | SetMix | channel (0–5), param (0–7; **8 = reverb send**, v6), value |
| 16 | SetMaster | param (0–2; **3–7 = reverb return, size, decay, tone, lo-fi**, v6; **8 = reverb return mute, 9 = master mute**, v7, value ≥ 64 = muted, live only: not stored in the song), value |
| 17 | SetEntry | index, pattern, repeats |
| 18 | SetSongLen | len |
| 19 | SetTempo | hi, lo |
| 1A | SetSwing | percent |
| 1B | SetName | 12 ASCII bytes |
| 1C | SetZeroNote | note (v5: accepted and ignored, the 0 V note is fixed at C0 = 12. The editor still sends 12 to older firmware.) |
| 1D | SetMacroMask | macro (0 = decay/Main, 1 = colour/X), **enabled**-voice bits 0–3 (v2) [, CV/IN bits 0–3 (v3, see `macro_cvin`)] |
| 1F | SetHeader | 212 raw header bytes |
| 20 | Transport | mode (0 stop, 1 song, 2 pattern), arg (entry or pattern) |
| 21 | Cue | entry (song mode) or pattern (pattern mode); applied at the end of the current pattern |
| 22 | Preview | track (0–7), note, vel (plays one note now; IN: opens the VCA for 1/4 s in IN TRACK mode) |
| 23 | NextPart | – (same as a tap on Z Down) |
| 30 | Save | slot 0–5 → **Ack** when the flash write has finished |
| 31 | Load | slot → **Ack**; the editor then sends GetSong |
| 32 | InitSong | kind (0 empty, 1 demo) → **Ack** |

All edits are applied to the **live song in RAM** right away (a running
pattern picks them up on the next row). Nothing is written to flash until
**Save**.

## Messages: card → web

| Cmd | Name | Payload |
| --- | --- | --- |
| 41 | Info | protocol, fw major, minor, patch, tracks, drum tracks, patterns, rows, slots, entries, current slot, valid-slot bitmask |
| 42 | SongHeader | 212 raw header bytes |
| 43 | PatternChunk | pat, track, row0, count, count × 4 cell bytes (fw 0.3.0: × 6; fw 0.2: × 4, tracks 0–5) |
| 44 | DumpEnd | – |
| 45 | Status | 29 bytes (fw ≤ 0.3.2: 28), see below |
| 46 | Panel | 37 bytes (fw 0.2: 27), knob/macro feedback, see below |
| 50 | Ack | op (30/31/32), status (0 ok, 1 empty slot, 2 busy, 3 error), slot |

**Status payload**

| # | Field |
| --- | --- |
| 0 | mode (0 stopped, 1 song, 2 pattern) |
| 1–4 | entry, pattern, row, repeat |
| 5 | slot |
| 6 | flags: 1 ext clock, 2 next part queued, 4 song swap pending, 8 knob waiting for pickup, 16 panel on SELECT page, (v7) 32 reverb return muted, 64 master muted |
| 7–8 | tempo hi, lo (live, includes panel changes) |
| 9 | swing |
| 10–12 | macros decay, colour, filter (+64) |
| 13–14 | current CV notes |
| 15 | gate bits: 1/2 = CV gate 1/2, 4/8 = IN1/IN2 VCA gate (v3) |
| 16–17 | CPU % now / peak since last status (ProcessSample only) |
| 18–25 | peak meters 0–127: channels 1–6, master L, R |
| 26 | drum trigger bits since last status |
| 27 | song swap counter (7-bit). +1 on every song swap on the module (Load, Init, panel slot select). The editor re-reads the song when it changes without the editor having asked for a Load/Init (v5 editor). |
| 28 | (v6) reverb return peak 0–127 (same scale as the meters; 0 = the reverb is not running) |

**Panel payload (v2)**

Sent by core 1 only while the editor pings, **only when something changed**,
at most every 33 ms (~30/s), plus a keep-alive once a second and right after
Hello. Knob positions are reported with hysteresis so ADC jitter does not
cause a stream of messages. Building it never touches the audio core: it
reads the status words core 0 publishes every 64 samples.

| # | Field |
| --- | --- |
| 0 | flags: 1 = Z on SELECT page (Up), 2 = Z held down |
| 1–3 | knob positions Main, X, Y (0–127) |
| 4 | bit k = knob k is waiting for soft takeover (pickup) |
| 5–7 | macros decay, colour, filter (+64). The filter macro includes CV in 2. |
| 8–9 | macro routing: enabled-voice bits for decay (Main) and colour (X) |
| 10–13 | **effective** decay of drum voices 1–4 (song value + decay macro if routed, clamped 0–127) |
| 14–17 | **effective** param A of drum voices 1–4 (song value + colour macro if routed) |
| 18 | **effective** master filter (64 = off) |
| 19–20 | tempo hi, lo |
| 21 | swing |
| 22 | slot (the playing slot; Status byte 5 is the same) |
| 23–25 | mode, song entry (part), pattern |
| 26 | CV in 1 transpose in semitones (+64) |
| 27–28 | (v3) CV/IN routing bits for Main and X (`macro_cvin`) |
| 29–30 | (v3) **effective** gate length of CV 1–2 (1/16 row, with the Main macro if routed; 0 = tie) |
| 31–32 | (v3) **effective** glide of CV 1–2 (with the X macro if routed) |
| 33–34 | (v3) **effective** VCA decay of IN 1–2 (with the Main macro if routed) |
| 35–36 | (v3) **effective** drive of IN 1–2 (with the X macro if routed) |

Effective CV gate = gate + Main macro / 2, clamped 1–64; a tie (0) stays a tie.

### Macros are offsets

The PERFORM macros (Main = decay / CV gate length / IN VCA decay, X = colour /
CV glide / IN drive, Y = master filter) are
**live offsets** added when a drum is hit (filter: continuously). They are
**never written into the song**, and saving a slot saves the song values without them.
The editor draws the song value as the knob position (gold arc) and the
effective value from the Panel report as a red arc + dot. Tempo, swing and the
slot selected on the panel's SELECT page *are* song/session changes and are
synced into the editor's song.

### Compatibility

Protocol versions: 1 = fw 0.1, 2 = fw 0.2 (6 tracks, 4-byte cells), 3 = fw 0.3.0
(8 tracks, 6-byte cells with a second effect), 4 = fw 0.3.1 (8 tracks,
4-byte cells: the second effect was removed again because it did not fit the
editor), 5 = fw 0.3.2, **6 = fw 0.4.0/0.4.1** (lo-fi reverb), **7 = fw 0.4.2** (live mutes for the reverb return and the master), **8 = fw 0.4.3** (kick click, grit, snare/clap morph). Protocol 4 is 3 without the second effect; everything else (header,
Status, Panel, SetMacroMask) is identical.

**v8 (fw 0.4.3)** gives drum param 6 (until v7 a reserved byte that SetDrum
already accepted and stored) a meaning: Kick click / grit, see the header
table. fx_ver becomes 2 (header_sanitize and `Song.migrateFx` clear drum bytes 6/7 of
fx_ver-1 songs; cross-checked in `web/test/xcheck.test.mjs`). The wire format,
the cell format and the flash format (3) are unchanged.

* **Editor 0.4.3 + module ≤ 0.4.2 (protocol ≤ 7):** works; the Click/Grit knob
  is greyed out and nothing is sent for it; the header goes out with fx_ver 1
  (so 0.4.0–0.4.2 keep their reverb settings). Snare B between 45 and 83 plays
  the old switch on such a module.
* **Module 0.4.3 + editor ≤ 0.4.2:** works, but the old editor sends its
  header with fx_ver 1, and the module then resets Click/Grit to 0 on all four
  voices (the reverb is kept). Use the 0.4.3 editor with 0.4.3 firmware.

**v7 (fw 0.4.2)** adds two **live mutes** as SetMaster params 8 (reverb return)
and 9 (master output), value ≥ 64 = muted, reported back in Status flags bit
32 / 64. They are deliberately **not part of the song** (the header has no
free master byte, and a slot that comes back muted after power-up on stage
would be a trap): the module starts unmuted, Save to module / Export do not
store them, and a song swap keeps them as they are. Both fade over ≈ 5 ms
(gain steps of 128/32767 per sample, the reverb's at its 16 kHz rate); the
reverb keeps running while its return is muted, so unmuting brings back the
tail that is still ringing, like the return mute on a mixing desk. With
nothing muted the output is bit-identical to 0.4.1 (`tools/sound_check`).
The eight channel mutes stay what they were: mix param 5, saved in the song.

* **Editor 0.4.2 + module ≤ 0.4.1 (protocol ≤ 6):** works; the REVERB and
  MASTER mute buttons are greyed out and nothing is sent for them (firmware
  0.4.1 would reject params 8/9 anyway: a reject is only counted, nothing
  changes).
* **Module 0.4.2 + editor ≤ 0.4.1:** works; the old editor never sends params
  8/9 and ignores the two new flag bits.

**fw 0.4.1** adds the cell effect `W` without changing the protocol (still 6),
the song or the flash format: an effect letter is just a cell byte, firmware
0.4.0 ignores `W`, and the 0.4.1 editor offers it only on drum and IN tracks.

**v6 (fw 0.4.0)** adds the reverb: header bytes 21–27 (sends, fx_ver), master
params 3–7, SetMix param 8, Status byte 28. Cells, patterns, Panel and the
flash format (3) are unchanged; the reverb fields live in formerly reserved
header bytes, so a 0.3.x slot loads with fx_ver 0 and is migrated (sends 0 =
dry). Firmware `header_sanitize` and editor `Song.migrateFx` apply the same
rule (cross-checked in `web/test/xcheck.test.mjs`).

* **Editor 0.4.0 + module ≤ 0.3.2 (protocol ≤ 5):** works; the send knobs and the
  reverb module are greyed out, nothing reverb-related is sent.
* **Module 0.4.0 + editor 0.3.2:** works; the old editor keeps the reverb bytes
  in the header it holds (it sends them back unchanged) but cannot edit them,
  and ignores the extra Status byte.

**v5 has the same wire format as v4.** What changed is the meaning:

* the CV 0 V note is fixed at C0 (`SetZeroNote` ignored, header byte 16
  forced to 12), CV notes are folded into C0–B5, CV `vel` is forced to 0
  (see *Cell*);
* an IN VCA in CV GATE (or AUTO following the CV gate) opens fully;
* the slot knob on the panel's SELECT page splits its travel over the saved
  slots (see CONTROLS.md); core 1 tells core 0 which slots hold a song
  (internal command, not on the wire).

Migration of CV data (firmware `song_migrate_cv`, editor `Song.normalizeCv`,
the same rule on both sides): `shift = old cv_zero_note − 12`, every CV note
becomes `fold(note − shift)`, CV `vel` becomes 0, `cv_zero_note` becomes 12.
A song made with the default C2 = 0 V therefore keeps its **voltages**
(a `C-2` that put out 0 V becomes `C-0`, still 0 V); notes that used to be
clamped at 0 V or 6 V are moved by octaves into range. The step is
idempotent (a migrated song has zero 12 and nothing left to move), so it runs
on every load from flash, on every dump and file import, and the flash
format stays 3.

* **Editor 0.3.2 + module 0.3.1 (protocol 4):** works. The editor sends CV
  notes already in C0–B5, `vel` 0 and `SetZeroNote 12`, so the old firmware
  plays the same voltages. After reading an old module song that had another
  0 V note, the editor sends `SetZeroNote 12` and the converted CV tracks
  back. Not available with 0.3.1: velocity-free CV GATE (0.3.1 treats `vel` 0
  as full level, so the result is the same), the folding of the CV in 1
  transpose (0.3.1 clamps at 0 V), the new slot knob. Banner recommends 0.3.2.
* **Module 0.3.2 + editor 0.3.1:** works; the old editor may show a CV VEL
  column and a 0 V note knob, but the module ignores both.

* **Editor 0.3.1/0.3.2 + module 0.3.0 (protocol 3):** the editor sends the 6-byte cell
  forms with the second effect set to 0, so no hidden second effect stays on the
  module for a step you edit, and shows a banner recommending the update. A 0.3.0
  dump (6-byte chunks) is converted on reading: the second effect is dropped, or
  moved into the effect column if that one is empty.
* **Editor 0.3.1/0.3.2 + module 0.2 (protocol 2):** the editor sends the 4-byte cell
  forms for tracks 0–5 only, skips edits and previews of the IN tracks, sends
  SetMacroMask without the CV/IN byte, and shows a banner. A 0.2 dump (4-byte
  chunks, 6 tracks) is converted on reading: IN tracks empty, the IN VCAs on
  AUTO, so they behave as before.
* **Module 0.3.1 + editor 0.3.0:** the old editor sends 9-byte SetCell and
  6-byte SetCells cells; the firmware accepts them with the same rule (second
  effect dropped, or moved into an empty effect column). Its dump parser would
  expect 6-byte chunks, so use the matching editor (`web/`) for reading songs.
* **Module 0.3.1 + editor 0.2:** works for tracks 0–5 (4-byte cells are the
  native form again).
* **Flash:** 0.3.1 and 0.3.2 write format 3 (4-byte cells, 32,996-byte record, 9 sectors)
  at the **same slot addresses** as 0.3.0 (52 KB stride), so songs saved by 0.3.0
  are still found and converted when loaded (`song_from_v030`: header unchanged,
  second effect dropped or moved into an empty effect column). Slots saved by
  0.1/0.2 stay where they are and are converted when loaded (`song_from_v2`:
  CV/IN routing 0, IN VCAs on AUTO, IN hold 8). Saving writes the new format;
  older records of that slot are then ignored.
* Song files (`.wtrk.json`) from editor 0.1/0.2 (6 tracks) and 0.3.0 (6-byte cells) are converted on import.
* The protocol version is the first byte of **Info**. The editor shows a banner
  for a v1 module ("knob feedback and voice routing need firmware 0.2") and keeps
  working: everything else in v2 is a superset of v1, and Status is unchanged.
  On v1 firmware, macros affect all voices, and the editor draws the red arcs
  that way, using the macros in Status.
* A v1 module rejects SetMacroMask (unknown command, ignored). A v2 module
  loads v1 flash slots unchanged (routing bytes are 0 = all voices).
* Unknown commands are counted and ignored; messages for other devices
  (`F0 7D` with a different device byte, or other manufacturers) are ignored.

## Flow

```
editor                         card
  Hello  ───────────────────▶
         ◀─────────────────── Info
  GetSong ──────────────────▶
         ◀─────────────────── SongHeader, PatternChunk × 512, DumpEnd
  Ping (every 500 ms) ──────▶
         ◀─────────────────── Status (every 50 ms)
         ◀─────────────────── Panel (on change, ≤ 30/s)
  SetCell / SetDrum / … ────▶  (applied to live RAM)
  Save n  ──────────────────▶  core 1 writes flash
         ◀─────────────────── Ack(save, ok, n)
```

A full upload ("Send to module") is SetHeader + 512 SetCells messages
(512 × 73 bytes, ≈37 KB; fw 0.3.0: ≈53 KB, fw 0.2: 384 messages, ≈28 KB). The editor pauses 8 ms after every 24 messages so the card's 256-byte
MIDI RX FIFO can drain; the card handles USB on core 1 and never blocks audio.

## Firmware side

* TinyUSB runs on **core 1** (`tud_task()` in a loop); the audio engine on core 0
  never touches USB.
* Edits are written straight into the live `Song` (single-byte stores, so a
  torn read is never worse than one old/new cell for one row).
* Load/Init build the new song in a **spare buffer** and hand it to core 0
  through a lock-free SPSC queue. Core 0 swaps the pointer between samples.
* Save snapshots the live song into the spare buffer, then erases and programs a
  slot one 4 KB sector at a time (9 sectors = 36 KB per save; the slots are
  52 KB apart, as in 0.3.0, so older records can still be read) with interrupts disabled **only on
  core 1**. The binary is built `copy_to_ram`, so core 0 never reads XIP flash
  and the audio keeps running during the write. Each record starts with a 16-byte
  header (`'WTRK'`, version, size, CRC-32, ~CRC). The CRC covers the whole song,
  so if power is cut mid-write that slot simply reads as empty/invalid. It is
  never loaded half-written (the old contents of *that* slot are lost, though).
  A small journal in the last
  flash sector remembers the last-used slot for boot.
