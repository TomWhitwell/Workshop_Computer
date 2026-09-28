# Graphic Osc

**Draw an oscillator.** Eight faders are the eight stages of one cycle of a wave; whatever shape you draw
is what comes out of **Audio Out 1**, at the pitch set by the **Main knob + CV In 1** (1 V/oct). The faders
can be a Music Thing **8mu**, a **web page**, or both, connected any of three ways:

1. an **8mu plugged straight into the Computer** (no laptop needed);
2. an **8mu plugged into a laptop** that is plugged into the Computer (the web page passes it on);
3. **just a laptop**, dragging on-screen faders.

The card works out which by itself at power-up: see [USB: three ways to use it](#usb-three-ways-to-use-it).

It also contains a small **Turing Machine sequencer** (clock in, quantised pitch out) that runs alongside the
oscillator, and **FM** (Audio In 1) and **sync** (Audio In 2) for the oscillator itself. The **Z switch**
decides what the X and Y knobs do; see [X and Y knobs](#x-and-y-knobs-and-the-z-switch).

The oscillator is **band-limited**: hard steps, sharp corners and folded shapes do not alias, at any
pitch (measurements below).

> **Status: 0.7.0, WIP.** Built and tested on a PC and cross-compiled for the RP2040. Host/device USB
> switching, the web page (including an 8mu plugged into the laptop) and FM are now confirmed working on
> real hardware; sync, symmetry modulation depth and 0.7.0's own remapping (see [X and Y
> knobs](#x-and-y-knobs-and-the-z-switch)) are not yet - see [Known limits](#known-limits). Please report
> what you hear.

Written with [Claude](https://claude.com/claude-code) (Anthropic) in collaboration with the card's author. All
AI-written code is built and tested as described under [Testing](#testing).

---

## What you need

- A Workshop Computer with **Rev 1.1 hardware** (it can tell which way round its USB port is, which is
  what lets it be a host *or* a device). On older boards the card is always a USB device, so only
  the laptop ways work.
- For the faders, either or both of: a Music Thing **8mu** on its **factory settings** (faders = CC 34-41,
  buttons = notes C2 C3 C4 C5; nothing to configure), and/or a laptop with **Chrome or Edge** to open
  `web/index.html` (see [Web editor](#web-editor)).

## Quick start

1. Copy `UF2/800_graphic_osc.uf2` to the card (hold BOOTSEL, plug in, drop the file).
2. **With an 8mu:** plug it into the front USB-C. The bottom-left LED goes from slow blink to steady.
   Move fader 3 up or down to start drawing.
   **With a laptop:** plug the laptop into the front USB-C (a data cable), **switch the module off and on**,
   open `web/index.html` in Chrome, press **Connect card & 8mu**, and drag the faders.
3. You hear a sine-ish tone. Turn **Main** to change pitch.

## The four pages (8mu buttons A-D)

Top-left LED = page 1 ... middle-right LED = page 4. Every page remembers its settings while you use
the others.

| Page | Button | What faders 1-8 do |
|---|---|---|
| **1 Levels** | A | Height of stage 1-8. Bottom = -1, middle = 0, top = +1. |
| **2 Times** | B | How long each stage lasts, relative to the others. Middle = normal, top = 4x longer, **bottom = 0: an instant jump.** |
| **3 Curves** | C | The path a stage takes to the next level. **Bottom = hold, then jump. Middle = straight line. Top = smooth S-curve.** In between it morphs continuously. |
| **4 Perform** | D | 1 **Fine tune** (+-1 semitone) - 2 **Output level** - 3 **Symmetry** (bends the wave's midpoint; smooth, and **CV In 2** controls it too) - 4 **Fold** (wavefolder, 1x-8x) - 5 **Brightness** (low-pass on harmonics, top = off) - 6 **Odd/Even** (middle = off; down removes even harmonics; up removes odd ones, which sounds an octave up) - 7 **Formant position** - 8 **Formant amount** (boosts a band of harmonics up to +24 dB; bottom = off). |

A drawing is a closed loop: stage 8 flows back into stage 1, so there is never a seam.

### Fader pick-up (so nothing jumps)

The 8mu's faders are not motorised. When you change page, the faders stay where they are while the new
page has its own stored values. So a fader **does nothing until you move it through its stored value**,
and only then takes over. The bottom-left LED **blinks fast** while any fader on the current page is still
waiting. (The web page does the same for an 8mu plugged into the laptop, and shows a dashed marker where
the 8mu's fader is.) A value changed on the web page also has to be met by a physical fader again.

One exception: right after power-up, page 1 grabs on first touch (there is nothing worth protecting yet).

## Jacks, knobs and LEDs

| | |
|---|---|
| **Audio Out 1** | The oscillator. Full output level is about +-4.8 V for a drawn +-1.0, and it cannot clip. |
| **Main knob** | Pitch, 5 octaves: C1 (32.7 Hz) fully counter-clockwise to C6 (1047 Hz) fully clockwise. 12 o'clock is about 185 Hz. **Pitch in every switch position.** |
| **CV In 1** | Pitch, 1 V/oct, added to the knob. Reads 0 V when nothing is patched. Total range 0.5 Hz to 16 kHz. |
| **CV In 2** | **Symmetry**, added to the page-4 symmetry fader by an amount set with the Y knob (switch middle). At full amount +-5 V sweeps the whole range, as it always did before that knob existed; at zero it does nothing. Reads 0 V when nothing is patched. |
| **Audio In 1** | **FM**: modulates the oscillator's pitch. Amount = X knob with the switch in the middle. See [FM and sync](#fm-and-sync). |
| **Audio In 2** | **Sync source** only - type stepped by a quick press of the Z switch, see below. |
| **Pulse Out 1** | High for the first half of each oscillator cycle. A clock/sync at LFO speeds. |
| **Pulse In 1** | Turing Machine **clock**. |
| **CV Out 1** | Turing Machine **note**, quantised, 1 V/oct. |
| **Pulse Out 2** | Turing Machine shift register's **least significant bit**. |
| **X knob** | Switch up: Turing Machine **lock**. Switch middle: **FM amount**. |
| **Y knob** | Switch up: Turing Machine **scale**. Switch middle: **symmetry modulation depth** (how much CV In 2 moves symmetry). |
| **Z switch** | Up / middle: which pair of settings X and Y control. Down: a **quick press steps the sync type** (off / soft / hard); **holding it down for 1 second instead** restores factory defaults. |
| LED top four | Which page is showing. |
| LED bottom-left | Slow blink: nothing connected yet. Steady dim: an 8mu (or a laptop) is connected. Fast blink: a fader, or the X / Y knob for the current switch position, is waiting to be picked up. |
| LED bottom-right | The output wave as brightness (you can watch it at LFO speeds). |
| LED top three, briefly | For one second after you turn Y to a new scale they show its number (0-7) in binary. |
| Everything else | Unused: Pulse In 2, CV Out 2, Audio Out 2. |

Settings are **not** saved: the card starts from the defaults every time (keep presets in the web page).
Unplugging the 8mu while playing leaves the sound exactly as it was.

## X and Y knobs, and the Z switch

| Z switch | X knob | Y knob |
|---|---|---|
| **Up** | Turing Machine **lock** | Turing Machine **scale** |
| **Middle** | **FM amount** (carrier = the drawn oscillator, modulator = Audio In 1) | **Symmetry modulation depth** (how much CV In 2 moves symmetry; at zero, CV In 2 does nothing) |
| **Down** | nothing | nothing |

**Down is momentary**, and does one of two different things depending how long it is held:

| Held for | Does |
|---|---|
| a quick press (under 1 s) | Steps the **sync type**: off -> soft -> hard -> off. Source is Audio In 2. |
| a full second | Factory defaults, on every page and every knob setting. |

The **Main knob is pitch in every position.**

**Soft takeover, for the four knob-controlled settings** (lock, scale, FM amount, symmetry modulation
depth). Each remembers its own value. When you flip the switch, the knobs are suddenly in charge of two
different settings and will not be where those settings are. A knob therefore **does nothing until you
move it through (or onto) the value its setting already holds**, then takes over; nothing jumps. The
bottom-left LED blinks fast while either knob is waiting. Straight after power-up the knobs of the current
position take over immediately (there is nothing worth protecting yet), and after a factory reset they
must pick the defaults up like anything else. Values changed from the web page also have to be met by the
knob again.

**Sync type has no knob**, so there is nothing to take over: a quick press of Down just steps it, on the
spot, regardless of what the switch was doing a moment before or goes back to afterwards.

Defaults: lock mostly clockwise (a loop that mutates slowly), scale major, FM 0, symmetry modulation depth 0
(CV In 2 does nothing until you turn it up), sync off.

FM amount and symmetry modulation depth both follow a squared law (more travel at the gentle end, the
same law, so the two feel alike).

## FM and sync

Both act on the **drawn oscillator**, so any wave you draw can be frequency-modulated or synced.

**FM (Audio In 1).** Linear FM: the modulator moves the oscillator's pitch in proportion to itself
(a full-scale input at full amount swings the pitch by up to +-2x), so the same setting sounds the same
at any pitch (the classic "ratio" behaviour). A DC blocker on the input stops a steady offset detuning
the oscillator. Plug in an audio-rate oscillator (a second card, a VCO) and turn X up with the switch in
the middle. With nothing patched it does nothing.

**Sync (Audio In 2).** A rising zero-crossing on Audio In 2 (found to sub-sample precision) acts on the
oscillator. **Soft** reverses the direction the oscillator runs (it then repeats every two source cycles);
**hard** resets its phase. Hard sync's reset is a jump in the waveform, which would alias, so a band-limited
correction smooths that one edge (this is why the whole oscillator output is delayed by one sample, 21 us).
Soft sync has no correction: it reverses direction without a jump, and everything I tried to do about the
remaining kink (polyBLAMP) measured as making no difference, so it was left out. Unlike FM, sync's type
(off / soft / hard) has no dedicated knob: a quick press of the Z switch steps through it (see
[X and Y knobs](#x-and-y-knobs-and-the-z-switch)). Audio In 2 is only ever the sync source; symmetry
modulation (also 0.7.0) uses CV In 2 instead, not this jack.

### How clean FM and sync are (measured, and not as clean as the plain oscillator)

The plain oscillator is measured at -76 dB off-harmonic energy or better. FM and sync are **not** that clean,
and I would rather say so than imply it. Same method as below (the unchanged firmware DSP on a PC; energy that
is not on a harmonic of the pitch is what aliasing looks like; "dB" is relative to the signal):

| Measurement | Result |
|---|---|
| **FM**, full-scale sine modulator at 1, 2, 3, 5 and 8 x the carrier pitch (only where the modulator is below 20 kHz), five drawings, amounts 0.05 to 1.0 | **worst -34 dB**; typically **-40 to -60 dB** at 400 Hz to 3 kHz, -54 dB or better at 100 Hz, -44 to -72 dB at 200 Hz, and at or below -80 dB from 4 kHz up, where the limiter switches FM off |
| **Hard sync**, slave at 2.72 x the source, 40 source pitches 40 Hz to 3.5 kHz, six drawings | median **-36 to -47 dB**, worst -23 to -32 dB; the band-limited edge gains **4 to 17 dB** (median) over an uncorrected reset |
| **Soft sync**, same setup | median **-39 to -56 dB**, worst -18 to -23 dB (uncorrected: a kink, no jump) |

So FM and sync will alias audibly in the worst cases; they behave, at their better settings, like a decent
analogue-style modulation source rather than a perfectly clean one. What holds the FM figure where it is:

- The card measures the **modulator's frequency** (time between zero-crossings, held at its recent peak so
  it errs cautious), and limits the **FM amount** so the carrier's highest sideband stays below Nyquist
  (the amount is also faded in and out slowly, over about 0.7 s: a burst of FM shifts the carrier's phase for
  good, and an early version that let the limiter flick FM on and off measured about -17 dB at worst).
- It also chooses a **duller table** when FM is deep (as symmetry does).
- Consequence: **at high pitch or with a high-frequency modulator, FM is reduced or off** (the rule reserves
  7 modulator periods of sideband reach; less was measured worse). A modulator faster than a few kHz has
  little effect on higher notes.
- The modulator's frequency is measured from zero-crossings, so a **very complex modulator** (noise, a
  chord) is treated as fast, and FM is limited (probably more than it needs to be).

## USB: three ways to use it

The Computer's USB-C socket can be a **host** (an 8mu is plugged in, the card reads it) or a **device** (a
computer is plugged in, the card appears to it as a MIDI device called **Graphic Osc**). The card looks at the
port's power state about 0.2 s after power-up and picks one:

| Plugged in | The card is | The faders are |
|---|---|---|
| an 8mu | USB host | the 8mu's, read directly. **No web page.** |
| a laptop | USB device | the web page's. An 8mu plugged into the *laptop* is read by the web page (Chrome) and passed on. |
| nothing | USB host, waiting | (plug an 8mu in any time) |

- **The decision is made once, at power-up.** After plugging a laptop in, **power-cycle the module** (the port's
  power state may not settle until power is applied, and a reset of the chip alone may not be enough).
  Plugging an 8mu in after power-up works, as the card defaults to host.
- Only one thing can use the socket, so the 8mu and the web page cannot both talk to the card at once through
  it; that is why an 8mu on a laptop goes through the page.
- On boards older than Rev 1.1 the port state cannot be read and the card is always a device.
- The card and web page talk with **SysEx** (protocol in `sysex.h`, unit-tested).

## Web editor

Open **`web/index.html`** in Chrome or Edge (it is a single file, needs no network, and also works from the
repo's web-editor deploy, or from anywhere else you can host a static file). Use a USB-C **data** cable and close anything else that might be using the card or
8mu. Safari and iOS cannot do WebMIDI with SysEx.

- **Waveform, left.** The wave the faders define, drawn with the card's own renderer logic (levels, times,
  curves, fold and the brightness / odd-even / formant tone controls). The page you are on is highlighted on it:
  page 1 puts a numbered dot on each stage's level, page 2 shades each stage by how long it lasts (as a percentage
  of the cycle), page 3 colours each stage's curve and labels hold / line / S-curve, page 4 shows the drawing
  before fold and tone (dotted) and the harmonic spectrum after them. The fader you touch is highlighted.
- **Four banks of eight vertical sliders, right** (Levels, Times, Curves, Perform), each showing its value (0-127, the
  same units as the 8mu) and a plain reading of it (`+0.71`, `x1.50`, `hold 30%`, `+27%`, `H90`...). Perform's are
  named: Fine tune, Output level, Symmetry, Fold, Brightness, Odd / even, Formant pos, Formant amt. Click a bank's name
  to make it the current page (like the 8mu's buttons A-D); drag, use the arrow keys, or double-click to reset.
  Below them are the **X / Y knob settings** (Turing lock, scale, FM amount, symmetry modulation depth, sync
  type), with the module's Z switch position shown, and the currently-relevant pair highlighted (sync type
  highlights only while the switch is Down - it has no knob on the module, but the page's fader for it still
  works: a drag sets it directly, where the module itself only steps it one press at a time).
- **Presets**, kept in your browser (`localStorage`): save what is on screen (optionally with the Turing / FM / sync
  settings), load, update, rename, delete, hover a preset to see its wave over the current one, and export / import
  as a JSON file (imports are checked and clamped). Seven built in: sine, saw, square, triangle, folded sine, hollow,
  formant saw. Loading a preset changes the faders and **sends it to the card** if one is connected.
- **Connect card & 8mu**: reads the card first (**the card is the source of truth**), then every edit is sent as it is made.
  If an 8mu plugged into the laptop is not recognised (the status line says so, naming the MIDI port it found), press
  **Rescan**, which appears after connecting - some browsers do not notice a device plugged in after the page granted
  MIDI access. **Read from card** and **Send to card** are there for when you want to force either direction; **Reset** restores
  factory defaults on both. When the card changes something itself (a knob, the switch, a factory reset), the page follows.

**What the web page cannot do.** There is **no "save to card"**: the card keeps nothing across power-off
(the firmware has no flash storage), so presets live only in the browser and you re-send after each power-up.
The wave preview shows the wave *before* symmetry (applied while playing) and before band-limiting for pitch.

**How the preview was checked.** The page's renderer was compared with what the real firmware plays
(`tools/shape_check.cpp` runs 32 fader values through the firmware's own `Ui`, table builder and oscillator and
writes one cycle; the page's JS render was compared against that, in a browser, for eight drawings including
three random ones that use every control). Relative RMS error: sine 0.3%, folded 1.9%, tone controls 1.4%, saw
5% and square 4% (the difference is confined to the samples at the hard edges, where the firmware's wave is
band-limited and the preview is crisp), random drawings 1.5% to 10% (the 10% one is all hard edges). The preview
does not, on its own, prove the *sound*: it is a picture of the drawing.

## Symmetry: smooth, and CV-controlled

Symmetry bends the wave's midpoint. With a square drawn, it is pulse-width; with anything else it slides the
shape's centre of gravity towards the start or end of the cycle, without changing the shape.

It is applied **while playing**, not by rebuilding wavetables like the other page-4 controls. That is what
makes it smooth:

- **Fader steps are smoothed.** The 8mu's 128 steps become a glide of about 10 ms each, so sweeping the fader
  is continuous. Measured on a 150 Hz tone with the fader swept through all 128 steps: applied raw the steps
  make bends 6.3x larger than an ideal continuous sweep; through the card's smoothing they are 1.00x, i.e.
  indistinguishable from continuous. (Before this feature existed each step also waited for a full table
  rebuild, so it was rougher still.)
- **CV In 2 is added on top, unsmoothed**, so an LFO or envelope gives true audio-rate-smooth PWM-style
  movement. The fader sets the centre; the CV swings around it. **How much it swings is set by the Y knob
  with the switch in the middle (0.7.0)** - at full depth +-5 V sweeps the full range, exactly as CV In 2
  always did before that knob existed; at zero depth CV In 2 does nothing. In between, proportional.

**The range** is about +-42% of a cycle: the drawing's midpoint can land anywhere from about 8% to 92% of the
way round. (0.4.0 shipped +-35%; see [Known limits](#known-limits) for the two bugs found getting there, and
below for how +-42% was reached on request in 0.6.0. An earlier draft of the 0.4.0 work claimed +-45%/5-95%,
worked out on paper rather than measured - the actual checked number was, and still is, well short of that.)

**The catch, and how it is handled.** A warp applied while playing cannot be made perfectly band-limited the way
a prebuilt table can: it acts like frequency modulation, so its sidebands run above the drawing's highest
harmonic and can fold past Nyquist at high pitch. Two things keep that in check, both measured (see below):

1. When symmetry is deep, the oscillator picks a slightly **duller table**, leaving room for the sidebands.
2. Its **depth is limited by pitch**: full depth up to about **900 Hz**; it then tapers (0.31 at 1.3 kHz,
   0.27 at 1.7 kHz, 0.15 at 2.1 kHz, 0.08 at 2.5-4.5 kHz, 0.07 at 5.4 kHz, 0.01 from 6 kHz) and is gone
   above 12 kHz.

So symmetry is a full-strength control across the low part of the range, and a gentle one from the upper-mid
up. The limit follows the *sounding* pitch (knob + CV In 1), and glides smoothly as pitch changes.

**Widened again in 0.6.0, on request, past where 0.4.0 stopped.** The oscillator can go no further than
+-35% (0.4.0's setting) before the warp itself would glitch (make the wave briefly play backwards) *for that
version's corner shape* - see the `static_assert` mentioned in [Known limits](#known-limits). But that ceiling
is a property of how rounded the warp's corner is (`GOSC_WARP_SHIFT`), not a law of nature: every corner width
shares the same hard rule (never let the wave run backwards), so a *narrower* corner raises the ceiling further,
at the cost of aliasing a bit more at every depth, not just at the new top end (a sharper corner is itself a
"louder" shape to warp with, before considering how far it moves). A narrower corner still (`GOSC_WARP_SHIFT` 5) and the 0.4.0 corner width (`GOSC_WARP_SHIFT` 3) were both
measured at low pitch, where a full warp should be at its cleanest, alongside the chosen SHIFT 4:

| Corner (`GOSC_WARP_SHIFT`) | Ceiling | Low-pitch (60-900 Hz) worst at its own full depth |
|---|---|---|
| 3 (0.4.0's) | +-37.5% | -74.6 dB |
| **4 (this release)** | **+-43.75%** | **-69.2 dB at the new 0.42 max, -67.3 dB tried as far as 0.427** |
| 5 (tried, not used) | +-46.9% | -60.9 dB at its own 0.44 - clearly worse even before pitch is considered |

SHIFT 5 was rejected on this evidence: it does not just add more range at the extreme, it measurably degrades
*every* depth setting, which is not what was asked for. SHIFT 4 does not: at 0.42 it stays inside the -65 dB
target at low pitch, same as 0.4.0's 0.35 did, and the *whole* re-measured pitch-depth curve (all of 0.42, 0.35,
0.20 and 0.08 requested) now sits within about half a dB of -65 dB across 70 random pitches 31 Hz-15.5 kHz - the
worst case anywhere is **-63.8 dB**, at the smallest tested depth (0.02), 1.1 dB short of the target and about
the same distance 0.4.0 itself fell short by (-64.9 dB). So the honest summary is: **the same cleanliness as
before, now available up to a noticeably wider +-42%**, not "more range in exchange for worse sound" - though
if you go looking for it, a corner sharper than SHIFT 4 is there to be tried, and will alias more at every
setting for it.

## Turing Machine sequencer

A 16-bit shift register that steps on every clock. Each step, the bit that falls off the end is fed back
in at the other end, and the **X knob (lock)**, with the Z switch **up**, decides how likely it is to be flipped
on the way (with the switch in the middle X is FM amount; the lock stays where it was, see
[X and Y knobs](#x-and-y-knobs-and-the-z-switch)):

| X knob | What happens |
|---|---|
| Fully clockwise | Never flipped: the same 16 steps repeat forever. |
| Towards 12 o'clock | Flipped more and more often: the loop keeps its shape but slowly mutates. |
| 12 o'clock | Flipped half the time: every step is random. |
| Towards counter-clockwise | Flipped most of the time: the loop turns into its own inverse each pass. |
| Fully counter-clockwise | Always flipped: repeats inverted every pass, so the true pattern is 32 steps long. |

The register's low 8 bits are quantised to the scale chosen by the **Y knob** (switch up) and sent to **CV Out 1** as
1 V/oct (C = 0 V), over **2 octaves** (0 to just under 2 V). The bits are spread evenly over the scale's notes,
so every note of the scale is equally likely, even for the two-note scale. Turning Y re-quantises the note
that is sounding straight away.

| Y knob zone (left to right) | Scale |
|---|---|
| 0 | Chromatic |
| 1 | Major |
| 2 | Natural minor |
| 3 | Major pentatonic |
| 4 | Minor pentatonic |
| 5 | Dorian |
| 6 | Whole tone |
| 7 | Root and fifth |

**Pulse Out 2** carries the register's least significant bit as a level, held between clocks (it is not
gated by the clock).

The sequencer is independent of the oscillator. To have it play the oscillator, patch **CV Out 1 to CV In 1**:
the Main knob then transposes the melody. It starts from a fixed seed, so the first pattern after
power-up is always the same. Nothing is saved.

## Recipes

Set page 1 first, then the other pages. The sine, saw, square and triangle recipes are exactly the drawings
used in the measurements below. The last two use controls that were tested, but not with these exact settings.

- **Sine-ish (the default).** Levels 64 109 127 109 64 19 0 19, all times middle, all curves middle.
- **Saw.** Page 1: levels rising evenly, **0 18 37 55 73 91 109 127**. Page 2: fader 8 to the **bottom**
  (the last stage is instant, so the wave snaps back). Curves middle.
- **Square.** Page 1: faders 1-4 at the top, 5-8 at the bottom. Page 3: **all faders at the bottom**
  (hold, then jump).
- **Triangle.** Page 1: 0 32 64 95 127 95 64 32. Curves middle.
- **Pulse width.** Draw a square, then use page 4 **Symmetry** (or an LFO into **CV In 2**) to move the duty cycle, about 15% to 85%.
- **Rich / metallic.** Take any drawing and turn up **Fold** on page 4, then sweep **Formant position**.

## How it stays alias-free

Aliasing happens when a wave contains harmonics above 24 kHz (half the sample rate): they cannot exist
in a sampled signal, so they come back as out-of-tune tones. A hand-drawn wave can contain anything, so
the card never generates anything that could alias:

1. Whenever the drawing changes, core 1 renders one cycle (4096 points), takes its spectrum with an
   FFT, applies the spectral controls, and builds **18 wavetables** of the same wave, each with fewer
   harmonics (512 down to a pure sine, half an octave apart).
2. The audio interrupt on core 0 reads the richest table whose highest harmonic still sits below Nyquist
   at the current pitch (blending smoothly between two as pitch moves), using a cubic B-spline.
3. New tables fade in over 21 ms, so moving a fader is a smooth morph, not a click.

Because fold, brightness, odd/even and formant all reshape *one cycle before it is trimmed*, they cannot alias
either. Symmetry is the exception, because it has to move smoothly and answer to CV: see
[Symmetry](#symmetry-smooth-and-cv-controlled) for how it is kept clean. The DAC's own 12-bit rounding would create pitch-locked distortion that folds back like aliasing, so
TPDF dither turns it into steady noise instead. And a ceiling on table levels guarantees the output can
never clip (clipping would alias).

## Measured results

Measured by compiling the **unchanged firmware DSP** for a PC (`tools/`). Each figure is the worst case
over 90 pitches from 31 Hz to 15.5 kHz, 65 536 samples each, for seven test drawings including a hard
saw (a zero-length stage), a square, a heavily folded sine and a pseudo-random drawing that uses every
control at once. "Off-harmonic" means energy anywhere except exactly on a harmonic of the pitch, which
is what aliasing looks like.

| Measurement | Result |
|---|---|
| Off-harmonic energy at the oscillator output | worst **-76.5 dB** (median -85 dB) relative to the signal |
| Worst single off-harmonic spur | **-79 dBc** (typical -85 to -92 dBc) |
| Same test on a *naive, non-band-limited* saw (control) | -15.5 dB total, -26 dBc spur, so the method does detect aliasing |
| Final 12-bit DAC output, default and full level | worst spur **-79 dBc**; the rest is the dither noise floor, about -60 to -70 dB below the signal; **0 clipped samples** |
| Fixed-point tables vs float64 reference | -68 to -74 dBFS RMS error |
| Pitch maths error, -6 to +8.5 octaves | under 0.015 cent (the ADC and the analogue side add far more) |
| Crossfade between two drawings | adds no step larger than the drawings' own |
| Symmetry at full requested depth, 90 pitches x 7 drawings, 8 random seeds | worst off-harmonic energy **-64.9 dB**, worst single spur **-65.7 dBc** (with the pitch limit; without it, some combinations of high pitch and deep symmetry break down almost completely - see [Known limits](#known-limits)) |
| Symmetry fader swept through all 128 steps | bends **1.01x** an ideal sweep with smoothing (5.1x without) |
| FM, hard sync, soft sync | see [How clean FM and sync are](#how-clean-fm-and-sync-are-measured-and-not-as-clean-as-the-plain-oscillator): **much worse than the plain oscillator** (FM worst -34 dB) |
| 8mu page / pick-up / MIDI-parser unit tests | 196 checks pass, run under UBSan |
| X / Y knob modes, soft takeover, sync-type stepping and FM/symmetry-mod mappings | 459 checks pass, run under UBSan |
| Web-app SysEx protocol (encode, decode, clamping, malformed input, round trip) | 82 checks pass, run under UBSan |
| Symmetry warp shape, smoother, and audio-input modulation unit tests | 2 079 checks pass, run under UBSan |
| Turing Machine: loop length, randomness, quantiser | 7 129 checks pass, run under UBSan |

The DAC is 12-bit, so **about -70 dB of noise is the floor of the hardware**; no code can go below that.
The claim here is "no aliasing above that floor", which is what the numbers show.

## Known limits

- **The 0.6.0 symmetry range has not been run on hardware.** It only changes the symmetry constants and the
  taper curve (see "Widened again in 0.6.0" above); everything else in this section still applies as before.
- **0.7.0's remapping has not been run on hardware.** The Y knob now controls symmetry modulation depth
  (how much CV In 2 moves symmetry) instead of sync type, and sync type is stepped by tapping the Z switch
  instead (see [X and Y knobs](#x-and-y-knobs-and-the-z-switch)). The knob-to-depth mapping reuses the exact
  fader/CV-mixing code CV In 2 always went through (see "Symmetry" above), so there is no new aliasing
  question to measure - the open question is only whether the control feels right, not whether it is clean.
  The tap-vs-hold timing (5 ms minimum, 1 s for a reset) is chosen, not measured against a real switch's feel.
- **The audio callback is not division-free any more.** It contains no floating point, and no 64-bit division, but it
  now calls the SDK's hardware-divider routine in three places: once per sync pulse (finding where the crossing fell
  between two samples), once per modulator cycle (turning its period into a frequency), and in the FM amount cap
  (recomputed at the 6 kHz control rate, only while FM is being capped). The RP2040's divider takes a few cycles,
  but this breaks the repo's "no division in the audio path" guideline, deliberately, and is unmeasured on hardware.
- **Confirmed on hardware:** USB host mode with an 8mu plugged straight into the Computer; USB device mode with
  the web page, including an 8mu plugged into the laptop (read by the page, not the card) driving the on-screen
  faders and the card together; and FM. **Not yet confirmed:** sync, symmetry modulation depth, the Z-switch
  tap, and everything under "0.6.0 symmetry range" above. Report anything that does not match what is written
  here.
- **Only one role per power-up**, and a card powered with **both** an 8mu and a laptop connected (needs a hub) will
  pick one and ignore the other.
- **FM and sync are not clean** (numbers above). FM is reduced or off at high pitch and for fast modulators;
  hard sync has audible aliasing at higher slave ratios. (FM's behaviour and these numbers were accepted as-is
  after listening on hardware; sync has not been checked against these numbers yet.)
- **Audio In levels.** FM and sync assume the input swings a few volts. The Computer's audio inputs are not
  calibrated here; a very quiet modulator gives little FM and may not trigger sync (the zero-crossing needs about
  +-0.3 V of swing to re-arm).
- **Web page: no "save to card"** (there is no flash storage in the firmware), and presets are in this browser only;
  clearing site data deletes them (Export makes a backup). The preview is a picture of the drawing, checked against
  the firmware as described above, not a rendering of the final sound.
- **Sequencer on hardware.** The sequencer logic is unit-tested on a PC (7 000+ checks), but the clock
  edge, CV Out 1 voltage and Pulse Out 2 have not been checked on a real Computer. CV Out 1 uses
  ComputerCard's calibrated millivolt output, so it is in tune to the extent your Computer has been
  calibrated (an uncalibrated one is approximate).
- **Symmetry is limited at high pitch** and slightly darkens the tone when deep (see above). The range is
  about +-42% of a cycle. The limit curve was refitted for 0.6.0 (see "Widened again in 0.6.0" above) from a
  binary search per pitch point against six test drawings, with a safety margin; a very different drawing
  might alias a little more at the edges than the numbers above suggest.
- **Two bugs found while widening this range for 0.4.0 (0.2.0 to 0.3.0), fixed before that release.** Worth
  recording because both were silent - nothing crashed, the numbers were just wrong:
  1. The warp shape's own peak value depends on how rounded its corners are (`GOSC_WARP_SHIFT`), so a fixed
     "depth" number was landing at a different real angle depending on that setting - already switching from
     a quarter-cycle corner to an eighth-cycle corner had silently made the same depth number warp about 50%
     further than intended, which was the real (unnoticed) source of the earlier "wider range". Fixed by
     rescaling the depth to the shape's actual peak (`kWarpShapePeak` in `osc_core.h`), so a given depth now
     means the same real fraction of a cycle for any corner width.
  2. Separately, the arithmetic that turns a depth number into a phase shift halves it somewhere in the
     fixed-point scaling (a left shift that changes a fraction's denominator, not its value - see the note on
     `kWarpDepthMax`). That halving was already there, unnoticed, in every version of this card; it just means
     "depth" was never quite what its own comments claimed. Rather than hunt further for a similarly-silent
     third bug, the fix was to measure the *actual* resulting warp directly (`tools/warp_check_range.cpp`,
     deleted after use, and the `warp is monotonic` check in `tools/warp_test.cpp`, kept) and set
     `kWarpDepthMax` from that measurement, not from the arithmetic's stated units.
  Both fixes are why 0.6.0's widening (below) could be done by measurement and a couple of constants, with no
  further arithmetic bugs to rediscover: `kWarpDepthMax`, `kWarpShapePeak` and the `static_assert`s all still
  mean exactly what their comments say for any `GOSC_WARP_SHIFT`.
- **0.6.0 widened the range again, from +-35% to +-42%**, by narrowing the warp's corner (`GOSC_WARP_SHIFT`
  3 to 4) rather than just raising `kWarpDepthMax` - see "Widened again in 0.6.0" above for why, and the
  measurements that ruled out going one step further (`GOSC_WARP_SHIFT` 5). The corner shape now has a hard,
  provable ceiling of +-43.75% before the oscillator's phase would run backwards for an instant (a glitch, not
  just more aliasing); `osc_core.h` has a `static_assert` that catches this if the constants are ever changed
  without re-checking it. A genuinely wider range than +-42% is possible the same way again (a narrower corner
  still), but SHIFT 5 was measured to cost more cleanliness at every depth, not just the new top end, which is
  why it was not used - see the table above.
- **Latency.** A rebuild takes about 150 000 "units" of work (each roughly 40-80 CPU cycles), which I
  estimate at 40-80 ms on the chip, plus the 21 ms fade. So a fader move is heard after about 0.1 s.
  Estimated, not measured.
- **The output is always centred.** The wave's average level is removed, so a drawing whose stages are all
  at the same height (a flat line) is silent, as it should be.
- **Level ceiling.** If fold or formant would make a wave louder than the ceiling (1.25x a drawn 1.0), the
  whole wave is turned down to fit rather than clipped.
- **Pitch tracking.** ComputerCard has no input calibration yet and the RP2040 ADC has known non-linearities,
  so CV tracking over many octaves is good to a few cents, not perfect. The Main knob has a small dead band
  to stop ADC noise wobbling the pitch.
- **Fader resolution.** The 8mu sends 7-bit values, so stage levels move in steps of about 1/64.
- **Rev 1.1 only** for USB host (and for detecting which way to be). **Factory 8mu mapping only.**
- **TinyUSB.** Built with Pico SDK 2.3.1 (TinyUSB 0.19). The vendored USB MIDI driver needed a one-line
  compatibility shim for that version (in `tusb_config.h`, guarded so older TinyUSB is unaffected). The
  repo dev container uses SDK 2.2.0 / TinyUSB 0.20; that combination has not been built.

## Building

VS Code with the Raspberry Pi Pico extension, or from a shell (adjust paths):

```sh
cmake -S . -B build -G Ninja      # needs PICO_SDK_PATH and the arm-none-eabi toolchain
cmake --build build               # produces build/graphic_osc.uf2
cmake -S . -B build_profile -G Ninja -DGOSC_PROFILE=1   # the CPU-profiling variant
```

The build prints RAM use; it is 179.6 KB of 256 KB (68.5%), mostly the two wavetable ladders (43 KB) and the
table builder's FFT buffer (39 KB). Flash use is 64 KB (the card runs from RAM).

## Testing

No hardware needed. Requires Python with `numpy` and `ziglang` (`pip install numpy ziglang`).
(On a machine with application control, freshly built test programs may be blocked until the folder is allowed.)

```sh
tools/run_tests.sh
```

This runs the unit tests, compiles the firmware DSP for the PC (with undefined-behaviour checking on),
and reports the table accuracy, aliasing sweep, crossfade, final-DAC, symmetry, FM and sync measurements above.
(`tools/shape_check.cpp`, which the web page's preview was checked against, is a separate tool; see its header.)

## Files

| File | Role |
|---|---|
| `osc_core.h/.cpp` | The engine: wavetable builder (core 1) and oscillator (core 0). No hardware. |
| `ui.h/.cpp` | Pages, fader pick-up, what each fader means. No hardware. |
| `panel.h` | What the X / Y knobs mean per switch position, soft takeover, sync-type and FM mappings. No hardware. |
| `sequencer.h` | The Turing Machine and its quantiser. No hardware. |
| `sysex.h` | The web page's protocol (messages, parser, what they do). No hardware. |
| `midi8mu.h` | 8mu byte stream to events, with MIDI running status. No hardware. |
| `core1.cpp` | USB (host or device, chosen at power-up), the knob logic, and the scheduler that rebuilds tables in small slices. |
| `main.cpp` | The 48 kHz audio callback, pitch, FM / sync input, LEDs. |
| `usb_descriptors.c` | USB MIDI device descriptors (from the ComputerCard `midi_device_host` example). |
| `web/index.html` | The web editor: one static file, no network needed. |
| `shared.h` | State shared between the two cores. |
| `tools/` | PC test drivers and measurement scripts. |
| `ComputerCard.h`, `usb_midi_host.*` | Vendored, unmodified (Chris Johnson; rppicomidi). MIT. |

## Credits

ComputerCard by Chris Johnson (the USB device / host switching and the SysEx blocking-write pattern follow its
`midi_device_host` and `web_interface` examples). USB MIDI host driver by rppicomidi. The USB receive-path hardening
(copy in the callback, parse afterwards) is from what `105_voder` learned about the 8mu. The 8mu is
by Music Thing Modular.
