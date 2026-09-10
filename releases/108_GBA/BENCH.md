# Bench checklist — GBA PSG Voice, first power-on

Work down this list. Each step isolates one thing, so a failure tells you *where* rather than
just *that*. Stop at the first step that fails and read its "if it fails" note — most of them
point at a specific fault this project has already had once.

Flash `build/gba_link.uf2`. Power the **Computer first, then the GBA**, cartridge-less, on the
logo screen.

---

### 1. It boots

**Expect:** a **green screen** while the payload uploads (about six seconds, with LEDs 1-5
running as a progress bar), then straight to the play screen. Workshop **LED 0 goes solid**.

There is deliberately no title card: the green screen is the boot proof, and the host starts
talking within milliseconds of the link coming up. If the host stays silent you get
*WAITING FOR HOST* instead — that message existing at all means the image ran and the link did
not.

**The green flash is a deliberate boot proof**, painted in assembly before the C runtime
exists. It makes three previously identical white screens tell themselves apart:

| Screen | Meaning |
|---|---|
| stays **WHITE** | the image never ran — multiboot, entry point or header |
| stays **GREEN** | the image ran; `.bss` zeroing or `main()` died |
| title appears | normal boot |

*If it stays WHITE:* flash `diagnostics/mbrate.uf2` and leave it on LED 3 (100 kHz). Power-cycle
the GBA a few times. Solid = the upload itself is fine and the fault is in the image; blinking
= multiboot is failing and nothing in the payload is implicated.

*If LED 0 blinks and LED 1 is lit on its own:* that is the NoGBA code — nothing answered the
sync. Almost always it means **the console is still running the previous payload**; power-cycle
it. This is the single most common way to lose an afternoon here.

*If the title appears but the play screen never does:* the host is not talking. LED 0 will be
blinking. The splash holds for about 3 s and then gives up, so this is visible.

*If it boots and then the screen goes dark or freezes:* suspect the **serial IRQ**. Set
`LINK_USE_IRQ` to `0` at the top of [`payload/link.h`](payload/link.h), re-run
`payload/build.sh`, rebuild the firmware. That reverts to the polled servicing that has worked
since bring-up, and the CAL page will then read `POLLED`.

### 2. It makes a sound

**Expect:** press **A** on the GBA. A note sounds from the headphone jack. LED 2 lights.

*If silent but the screen responds:* check the headphone jack actually carries audio (the
**GBA SP has no headphone socket** — it needs the SP adapter). If the jack is fine, this is
almost certainly PSG register ordering: `SOUNDCNT_X` bit 7 must be set before any other sound
register write, and the envelope volume must be written before the trigger. Both are done in
that order deliberately — see `CLAUDE.md`.

*If the FIRST note sounds and later ones do not:* that is exactly the DAC-off failure the
trigger ordering exists to prevent. Worth reporting precisely, because it means the ordering
fix did not take.

### 3. The envelope shapes

**Expect:** hold **A**: the note attacks, decays to a sustain, and stops when released. The
envelope bar on screen follows it.

*If the volume never changes:* the software envelope writes `NRx2` every control tick. If this
hardware needs a re-trigger to apply a volume write ("zombie mode"), the note will play at a
fixed level. Report it — the fix is a different envelope strategy, not a tweak.

### 4. Pitch tracks CV

**Expect:** patch a keyboard or sequencer into **CV In 2**. The big note name follows it, and
**CV Out 2** into a tuner reads the same note.

**Then trim it.** `START` → `SELECT` to the **CAL** page. `CV SCALE` is counts per semitone in
sixteenths; it starts at **455** (28.4 counts, the arithmetic estimate). Play an octave apart
and adjust until the two notes really are twelve semitones apart. `CV OFFSET` shifts the whole
range. There is a live `READS` note and `RAW` count on that page to trim against.

*Why this needs doing at all:* ComputerCard calibrates the CV **outputs** from EEPROM, but
there is **no factory calibration for the CV inputs**. The starting number is arithmetic, not
measurement.

### 5. The gate works, including short triggers

**Expect:** a gate into **Pulse In 2** plays notes. LED 1 follows the gate.

Then try a **very short trigger** — well under 1 ms. It should still fire a note. That is what
the sticky edge bit and the edge counters on both sides are for; if short triggers are missed
while long ones work, that mechanism is broken.

### 6. The default mapping does something

**Expect:** with a note sounding —

- **CV In 1** sweeps the pulse duty (timbre changes)
- **Audio In 1** detunes the second square — *turn CH2 on first*, VOICE page row 2
- **Audio In 2** brings in noise — *turn CH4 on first*, VOICE page row 4

*If Audio In 1/2 respond to movement but drift back to centre when you hold a steady voltage,*
those inputs are AC-coupled on this hardware and are modulation sources rather than CV sources.
Nothing to fix in firmware — re-point them at something where that suits (vibrato depth, wave
scan) on the MAP page, and keep the two CV jacks for anything that must hold still. The play
screen's input meters show this immediately.

### 7. The editor

**START** opens it and always lands on the first page. **SELECT + Left/Right** walks the tab bar.

| | |
|---|---|
| D-pad alone | move the cursor (row, and on grid pages a column) |
| **A** + Up/Down | change the value |
| **A** + Left/Right | change coarsely, or the row's second field |
| **START** | back to the performance screen |

**No button carries its performance meaning inside a menu** — not trigger, not hold, not the
mapped D-pad actions. HOLD set before you entered still holds, so latch a drone in PLAY and then
go and edit it.

Worth checking specifically:

- **CHAN** — all four channels side by side. Cells a channel does not have read `-` and ignore
  edits: only channel 1 has a sweep, only channel 2 a detune partner, only channel 3 a wavetable,
  and channel 4 is not pitched.
- **TRIG** — the pin grid, A alone toggles. Turn every column off for channel 1 and it should go
  silent even with HOLD on; a channel wired to nothing is parked on purpose.
- **MIX** — Left/Right picks a channel, A+Up/Down sets level, A+Left/Right sets OFF/L/R/BOTH. The
  dim bar is the fader, the bright bar inside it is the live envelope.
- **MAP** — seven sources, each with a destination, an amount and four per-voice tickboxes (A
  toggles a tick). Set one to `ORNMNT`: the amount column becomes a slot, and the source works as
  a switch above halfway.
- **ORN** — sixteen steps, edited graphically. A+Up/Down is a semitone, A+Left/Right an octave.
- **DRUM** — arm DRUM MODE, then any input going high fires its sound. Drums borrow channels 1
  and 4 while armed.
- **SET** — pick a USER scale, then on `SCALE NOTES` use Left/Right to walk the twelve degrees and
  A+Up/Down to switch each on or off.

### 7b. The Workshop switch plays notes

Flicking the momentary switch **down** fires a note in both modes. It is a column of the TRIG
grid, and the host sends it with priority the moment it moves.

### 7c. HOLD

**B** in PLAY latches: on fires a note and sustains it, off releases. Nothing else — no retrigger
on repeated presses, and no effect inside the editor.

### 7d. The envelope actually moves

On ENV, set ATTACK to about 250 MS and hold a note. The volume should **rise** to full, fall to
sustain, and stay there until release.

*This is the specific thing that was broken once.* The PSG only loads an envelope register's
volume into the channel **on a trigger** — writing it while the channel plays is ignored ("zombie
mode"). Our envelopes are computed in software, so every attack and decay step was discarded and
the note came out at whatever volume it was triggered at. A volume change is now a trigger, which
is what tracker engines on this hardware do.

*What to listen for:* a retrigger resets the waveform phase, so **listen to the noise channel** —
its LFSR restarts too, and a fast decay could buzz rather than fade.

### 7e. Portamento

`PORTAMENTO` on ENV, per channel. 0 is instant; higher slides. Set a long one and flip between
PLAY and an edit page — the slide should sound **identical**. It did not once: the control tick
only advanced once per main-loop pass, so its rate was the frame rate and a heavier page ran the
envelopes slower. The tick now runs while the screen is drawn.

### 7f. Patches

On **MEM**, the D-pad walks the sixteen slots (Left/Right one box, Up/Down a row of eight).
**A+Up saves, A+Down loads.** The bar shows progress; the box fills once a slot holds something.

Expect a **click** on save: erasing flash stops XIP, so the 48 kHz callback does not run for the
few milliseconds the write takes.

Then the real test: **save, power-cycle the GBA, load it back.** The patch lives on the card, so
it should survive the console losing power entirely.

### 8. Soak

Play for ten minutes. **LED 0 must never drop back to blinking**, and a note must never stick
on after the gate releases. The play screen has a `REJ` counter — words rejected by the tag or
check bits. On a healthy link **it should not move at all**; any steady climb is worth
reporting with the rate.

---

## The link diagnostics still work

The same payload still carries the characterisation screens, so nothing here has cost you the
ability to re-measure:

| Build | Question it answers |
|---|---|
| `diagnostics/mbrate.uf2` | which multiboot speed uploads *repeatably* (LED 3 = 100 kHz) |
| `diagnostics/linkrate.uf2` | how many words/s the live link sustains cleanly |
| `diagnostics/cablecheck.uf2` | which wire actually carries the GBA's SO |

**These are sources, not binaries — build them first.** From `diagnostics/`:
`cmake -G Ninja -B build -S .` then `cmake --build build --target cablecheck` (or `mbrate`,
`linkrate`), which leaves the `.uf2` in `diagnostics/build/`.

## The one habit worth keeping

**When a measurement contradicts something already known to work, doubt the measurement
first.** Several days of this project went into faults that turned out to be in the
diagnostics rather than the hardware. `diagnostics/POSTMORTEM.md` records each one.
