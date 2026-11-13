# Changelog

What changed in Blocks, newest first, in plain language. The first release is 1.0.0; the dated history below it is how the work landed. Hardware results are recorded in the project's test log.

## 1.0.0 (unreleased)

The first release. Everything in it:

- **Four tracks:** CV1 and CV2 send pitch and gate, AU1 and AU2 send stepped CV or triggers. Each has its own length (up to 64 steps), clock division, direction, scale, root, and range or output type.
- **Per step:** a gate, five lanes of seven levels (note, length, probability, ratchet, condition), an octave on the CV tracks (-2…+2) and **microtiming** (up to 11/24 of a step early or late, Y knob in edit; early and late steps are tinted on the gate row).
- **Knobs:** Main is always BPM (80-180 by default, range set in the editor). In edit, X is the note and Y the microtiming of the selected step; in perform, X transposes and Y is live swing (50-71 %). Other lanes are set with the pads.
- **Patterns:** 16 slots, chains of up to 32 entries, N / A / I / D pads (new, append, insert, delete), copy with Shift. Pattern switches and chain steps wait for the end of the longest track loop, so tracks of different lengths make polyrhythms.
- **Launchpads:** the Mini MK3 is verified; the Launchpad X and Pro MK3 are supported but not yet tested on hardware. The Novation logo pulses to the beat in a random colour.
- **Clock:** internal, or external on Pulse In 1 (1, 2, 4, 24 or 48 PPQN); Pulse In 2 resets; CV In 1 and 2 transpose.
- **Web editor and manual** (`web/index.html`, works from disk in Chrome or Edge): backup and restore, eight hidden settings, the firmware version on the Connect line.
- **Saves itself** to the card's flash after Stop. Version numbers come from the `VERSION` file: builds are `1.0.0+<commit>`, a `v1.0.0` tag builds plain `1.0.0`.

## Earlier history (dated)

### Logo LED beat (2026-11-13)

- The Novation logo LED flashes on every quarter note while playing (BPM or external clock) and stays dim while stopped. Its colour is random at each power-up.

### Main knob is BPM (2026-11-13)

- Main knob is always BPM; in edit, lanes other than the note are set with the pads.

### Switches at the longest loop (2026-11-13)

- **Pattern switches and chain steps now happen at the end of the longest track loop, not CV1's.** A track's loop is its
  length times its division. With CV1 at 16 steps and CV2 at 8, CV2 plays its loop twice and then all four tracks switch
  together; a shorter track that doesn't divide the switch point is cut mid-loop. A reset (Pulse In 2) on the last step of
  the longest loop counts as the switch point.
- Fixes: an external reset at the bar line no longer plays an early step 1 twice; early steps on tracks that don't share
  CV1's boundary now play from the new pattern; a queue or jump changed in the last step no longer double-fires or leaves
  stale look-ahead; a late step no longer re-fires after a division change; late ratchets fit all their hits before the
  next step; a pending jump survives deleting several chain entries at once.

### Delete pad (2026-11-13)

- **A red D pad on the Patterns screen** (third row, fourth pad). Hold it and tap one or more slots: each pattern is
  emptied and every chain entry for it is removed (the playing entry too: the next one plays at the next loop end, or the
  playing pattern carries on alone if none are left). The pads flash D1…D16. The colour is a placeholder until picked on
  the Launchpad.

### Microtiming (2026-11-13)

- **Per-step microtiming.** Push any step up to 11 units early or late (1/24 of a step each) for feel and flams: select
  a step and turn the **Y** knob with Z in the middle. The pads flash the offset (−11 … 0 … +11). Early and late steps
  draw their gate pad in their own colours. The first step after Play can't play early; everything else can, including
  across a pattern switch.
- **Live swing.** With Z up, **Y** now sets the swing (50-71 %), the same eight values as the Settings screen.
- **Old cards and backups load as before.** Patterns saved by earlier firmware (and backups made before this) come back
  with every step on the grid. Backups made now hold bigger pattern blocks (2084 bytes instead of 1828) and need this
  firmware or newer to restore.

### Version numbers (2026-11-13)

- Every build now carries a version: the release number (1.0.0) plus the commit it was built from, e.g.
  `1.0.0+031c836`; a release build is plain `1.0.0`. The web editor's Connect line shows it, and the firmware file is
  also saved under that name.
- The manual now says to power-cycle (not just Reset) when switching the card from the Launchpad to a computer.

### Launchpad X and Launchpad Pro MK3 (2026-11-13)

- Blocks now recognises the **Launchpad X** and **Launchpad Pro MK3** as well as the Mini MK3, using the same layout.
  On the Pro MK3 the extra buttons (left column, bottom rows) stay dark and do nothing.
- These two are built from Novation's documentation and **not yet tested on real hardware**; if you have one, the
  project's test log has a checklist of what to try. The Mini MK3 is verified.

### 16 slots and the chain page (2026-11-13)

- **16 patterns instead of 8.** Slots 9-16 sit on the second pad row of the Patterns screen. Patterns, chain and
  settings from older cards are still there after flashing; slots 9-16 start empty.
- **A chain page.** The bottom four pad rows of the Patterns screen show your chain, entry by entry, up to **32** long
  (it was 16). Tap an entry to jump to it at the next loop end. **Shift** + tap removes it. **Shift** + hold for a second
  deletes the whole chain.
- **N, A and I pads** build chains without holding a slot: **N**ew (replace), **A**ppend (add to the end), **I**nsert
  (after the selected entry).
- Copy flashes `C1` ... `C16`.
- Backups now hold 17 blocks (16 patterns and the settings). Older 9-block backups still restore.

### Octaves (2026-11-12)

- **Every note step on CV1 and CV2 has its own octave**, from two below to two above the base octave. The new **OCT**
  button (bottom side button, lit on the note lane of a CV track) moves the view up one octave; **Shift** + OCT moves it
  down. The pad flashes the root note and octave (for example `E4`).
- Each step's bar is drawn in its own octave's colour, from dark orange (octave 1) to light (octave 5), the same shades
  as the OCT button.
- Old patterns play at the same pitches as before.

### 8-step pages and seven levels (2026-11-11 to 2026-11-12)

- **Pages are 8 steps wide** (P1-P8 for 64 steps). Row 1 holds the gates and the seven rows below show the selected
  lane for all 8 steps at once, so you see every step's value without switching.
- **Every lane has seven levels** (0-6). Ratchets go up to 7 hits, gate length from 5 % to 98 %, probability from 14 % to
  100 %, and conditions get `1:3` and `1:4` (every third or fourth loop). On AU tracks level 3 is exactly 0 V.
- Overview and the length rows now show pairs of pages (16 steps).
- Patterns saved before this change convert automatically when they load.

### BPM range (2026-11-10)

- **The range of the BPM knob is a setting** in the web editor: 80-180 (default), 60-160, 40-140 or 100-250. If the
  current BPM is outside the new range the knob takes over at once.

### Mod lane removed (2026-11-10)

- The MOD lane and its button are gone, and the Y knob does nothing in this version. The lane had no output (there is
  no free jack on the Computer for it). Saved patterns and backups are unchanged; the old byte is now reused for octaves.

### Labels and scale/root text (2026-11-10)

- **Elektron-style text on the pads:** lane names (NTE LEN PRB RTG CND, VAL on AU tracks), directions (FWD REV PNG RND)
  and AU types (BIP UNI TRG). Three-letter labels slide across so all three letters are readable.
- Tapping a scale or a root note in Settings **flashes its name** (MAJ ... CHR, C ... B). The font gained `J H X #`.

### The web editor (2026-11-10)

- **Plug a computer in instead of the Launchpad** and the card shows up as a MIDI device called "Blocks". Open
  `web/index.html` in Chrome or Edge.
- **Backup and restore** all patterns and settings to a `.blk` file. Restore is checked in full first, and is refused
  while the card is playing.
- **Six hidden settings:** external clock rate (1, 2, 4, 24, 48 PPQN), CV In 1/2 transpose mode, auto-save timing
  (3 s, 10 s or manual), AU output range, CV gate mode (length lane or 10 ms trigger), and BPM range.
- **Manual saving:** with auto-save set to manual, **Shift** + Play while stopped saves and flashes `OK`.
- **The full user manual** lives on the same page and works without a card connected.

### Ordered chains (2026-11-10)

- **Chains play patterns in the order you tap them, with repeats**: hold a slot, tap others, release. Hold a lower pad to
  build a chain from taps only, in any order. The chain is saved and resumes after a power-cycle.

### M0 to M4: the first playable card (2026-11-10)

- **M0:** the card drives a Launchpad Mini MK3 over USB, with a splash and replug recovery.
- **M1:** a playable two-track sequencer. Gates and pitch on CV and Pulse Out, panel knobs with a pickup guard (no
  jumps when you change bank), Z-switch play/stop and rewind, external clock on Pulse In 1, reset on Pulse In 2, scale
  and root, and the playhead on the pads.
- **M2:** probability, ratchet and condition lanes; AU1 and AU2 on the audio outputs as stepped voltage (bipolar or
  unipolar) or triggers; the LED meter; the BLOCKS splash.
- **M3:** clock divider, direction (forward, reverse, ping-pong, random), swing, length up to 64 steps with pages, the
  full Settings view, Overview with mutes, **Shift** + track to clear, and CV In 1/2 transpose.
- **M4:** pattern switching queued to the end of CV1's loop, simple chains, and **automatic saving to the card's flash**
  after Stop, so a power-cycle brings everything back.
