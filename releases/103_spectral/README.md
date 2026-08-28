# SPECTRAL

A Spectral filter.

## What it does

The card is a real-time spectral filter, not a granular processor and not a reverb.

- **Low band counts** (4–8) give broad, obvious sweeps, close to a random multi-band gate.
- **High band counts** (64–128) give fine, comb-like textures.
- **Slow smoothing** makes bands drift open and shut.
- **Fast smoothing** makes them snap, which with a fast re-roll rate becomes a stuttering spectral gate.
- **TEXTURE** randomises bin phases, smearing transients into a diffuse wash.
- **FREEZE** holds the current magnitude spectrum while still tracking incoming phase, so the held sound stays alive rather than becoming a static drone. See [What freeze actually does](#what-freeze-actually-does) — it is not a sampler, and it still needs the input.

Both outputs carry the same wet signal. Mix dry externally, and patch through a stereo effect downstream if you want width — the spectral movement itself is what carries the interest.

## What to feed it

**It needs sustained, harmonically rich material.** The card carves moving holes in a spectrum, so it can only work with a spectrum that is actually there. The best sources:

- **Pads, drones, held chords** — the obvious win. Bands opening and closing over a sustained chord is exactly the effect.
- **Noise** — white/pink noise becomes pitched, shifting resonances. One of the most dramatic uses: BANDS low, SMOOTHING slow, and it turns noise into a slow chord progression.
- **Feedback loops, cymbals, room tone, whole mixes** — anything broadband and continuous.
- **Distorted guitar, organ, bowed strings** — sustained and harmonically dense.

**What works poorly:**

- **Percussion and drums** — 21 ms of latency plus a 1024-sample analysis window smears transients. You get a wash, not a beat.
- **Sparse plucks, single notes with gaps** — during the gaps there is nothing to filter, so the card falls silent and the effect is only heard on the decays.
- **Sine waves and thin sounds** — a single partial means one band is doing all the work; most of the filter has nothing to act on.
- **Anything quiet** — the effect scales with what comes in. Bring it to a healthy Eurorack level.

A good first patch: a noise source or a held chord into Audio In 1, BANDS at about 2 o'clock, SMOOTHING slow, PROBABILITY around 12 o'clock. Then sweep PROBABILITY and listen to the holes open and close.

## Controls

The switch's two stable positions (Middle and Up) select between two knob pages. **Knobs are always live**.

Bottom left LED (4) lit = page 1, Bottom right LED (5) lit = page 2.

**Switch MIDDLE — page 1**

| Control | Parameter |
|---|---|
| **Main** | **PROBABILITY** — how many bands are open |
| **X** | **BANDS** — 4 to 128, logarithmically spaced |
| **Y** | **SMOOTHING** — how fast band gains morph: snap at the bottom, then 0.05 s to about 2.6 s |

**Switch UP — page 2**

| Control | Parameter |
|---|---|
| **Main** | **TEXTURE** — phase randomisation depth |
| **X** | **RE-ROLL RATE** — fully anticlockwise = never (Pulse In 1 only), then ~3 s up to continuous |
| **Y** | **OUTPUT LEVEL** — holds its current value until you actually move Y, so switching pages never drops the volume |

**Switch DOWN — toggle freeze.** Momentary position, so it acts as a button:

- **Short press** — toggle normal freeze (holds magnitudes, tracks live phase).
- **Hold 2 seconds** — **SEALED** freeze: holds phase as well, so nothing of the live input reaches the output. It engages at the 2 s mark, while you are still holding, so you get feedback without letting go. LED 3 pulses slowly in sealed mode, steady in normal.
- **Any short press while frozen** returns to live, whichever mode was active.


(Pulse In 2 freezes while held, and always uses normal freeze.)

| Jack | Function |
|---|---|
| **Audio In 1** | signal to process |
| **Audio Out 1 / 2** | wet output (mono, duplicated) |
| **CV In 1** | added to PROBABILITY |
| **CV In 2** | added to BANDS |
| **Pulse In 1** | re-roll all band gains |
| **Pulse In 2** | freeze while held |
| **Pulse Out 1** | fires on every re-roll |
| **Pulse Out 2** | high while more than half the spectrum is open |
| **CV Out 1** | how much of the spectrum is currently open |
| **CV Out 2** | measured DSP load diagnostic — full scale = 100% of the frame budget |

LEDs 0–2 show spectral density, LED 3 freeze (steady = normal, slow pulse = sealed), LEDs 4–5 the current page.

### What to expect as you turn Main up

PROBABILITY is a *density* control, not a volume control, and the two halves of its travel do different things:

| Main | What you hear |
|---|---|
| low | sparse isolated resonant bands — pitched, gated, hollow; the source is barely recognisable |
| ~50% | the source clearly present with moving holes punched through it |
| high | nearly the full spectrum, with only a subtle shimmer of movement |

Most of the *level* arrives by about halfway (measured: 81% of the total energy by Main=50%). Above that you are opening more bands, but they are the quiet ones — so the top half reads as **increasing density and smoothness**, not as getting louder. That is how the effect works rather than a fault; if you want the top of the knob to feel more dramatic, turn SMOOTHING down so the band gains spread out again instead of clustering near the middle.

The lowest bins (below about 200 Hz) all share a single band rather than each getting their own at high band counts. Our bins are 46.9 Hz wide where Clouds' are 7.8 Hz, so without that merge a single band down there gates a whole bass partial on and off at the frame rate — audible as low-frequency grit with BANDS and PROBABILITY both high.

### Why Main can be silent through part of its travel

PROBABILITY sets a threshold every band gain has to clear. With **few bands** there are few gains to clear it, so a low setting can shut all of them and the card goes quiet — at 4 bands roughly the bottom half of Main is silent, at 128 bands only the bottom 5%. This is how the effect works rather than a fault (the original has the identical structure), but it does mean **BANDS and PROBABILITY interact**: turn X up if you want a smooth Main sweep.

Smoothing sweeps the time constant evenly rather than the filter coefficient, so equal movement of the knob multiplies the morph time by a constant factor — the whole range is usable rather than everything happening in the last tenth.

PROBABILITY's threshold is scaled by how spread out the band gains currently are, so equal knob movement opens an equal number of bands whatever SMOOTHING is set to. Without that the control was very uneven at slow smoothing — nearly nothing until noon, then everything at once.

BANDS is floored at 4 for this reason — below that PROBABILITY degenerates into a random on/off gate, and at a single band it was silent through about 95% of its travel.

### Reading the DSP load

CV Out 2 reports the *measured* time to process one FFT frame, live, scaled so full scale (+6V) means core 1 is exactly keeping up. It sits around 4V — about 68% — in normal use. Patch a voltmeter there if you want to confirm the headroom on your own hardware rather than take that figure on trust.



## TAPE mode (alt-boot)

**Hold the switch DOWN while powering on**, and keep holding for the first half second. The card shows which mode it booted into, following NIBBLE-KO's convention:

- **Left LEDs 0, 2, 4** (even) — normal boot
- **Right LEDs 1, 3, 5** (odd) — TAPE

Tape adds a delay *after* the spectral engine, so the card still carves its moving holes in the spectrum and the delay carries the result off into the distance. It is deliberately tape-like rather than clean:

- **Wow and flutter** — the read head wanders up to ±4 ms, driven by two detuned LFOs at incommensurate rates, so repeats bend and drift instead of copying exactly.
- **Damping** — each pass loses top end, so repeats get darker rather than piling up into a metallic ring.
- **Saturation** — the feedback path soft-clips, so high feedback thickens instead of blowing up, and the dry/wet sum is soft-limited rather than hard-clipped.
- **Loop highpass** — the repeats do not accumulate DC or rumble, which is what stops the low end piling up into a thump over successive passes.

Feedback is capped at 0.85 of unity: a long tail that clearly resolves rather than a freeze (the card already has two freeze modes for that).

Page 1 is unchanged. Tape takes over page 2:

| Control | Page 2 in tape mode |
|---|---|
| **Main** | **TIME** — 25 ms to 1.35 s |
| **X** | **FEEDBACK** |
| **Y** | **WOW** — depth of the pitch wobble |

Moving TIME while repeats are sounding gives the pitch swoop of a transport changing speed, because the read position is slewed rather than jumped.

## What freeze actually does

Freeze holds the **magnitudes** of the spectrum. It does **not** hold the phases — those keep coming from the live input, every frame. That is deliberate: a fully frozen spectrum is a static additive drone, and taking live phase keeps the held sound moving.

Two consequences worth knowing, because they surprise people:

**The input is still doing something while frozen.** It supplies all the phase information. The frozen spectrum decides *what frequencies* you hear; the live input decides *how they line up in time*. Play something different into a frozen spectrum and the character shifts, even though the held frequencies do not.

**Pull the input and the level drops.** With no signal, every bin's computed phase collapses toward zero, the bins start summing coherently instead of spreading out, and the output falls sharply (measured about 10 dB) into a thin buzz rather than sustaining. Freeze is an effect *on* a signal, not a sampler that holds after the source stops.

**Sealed freeze** Holding the switch down for 2 seconds captures each bin's per-frame phase *advance* as well as its magnitude, then keeps rotating every bin at its own captured frequency instead of reading the input. Measured against normal freeze, the passthrough drops from −30 dB to **−69 dB**, and with the input unplugged the level holds steady instead of collapsing (rms 372 → 1144). It sustains as a held chord rather than a static buzz, because each partial carries on at the frequency it was actually running at.

Normal freeze remains the more animated of the two — it responds to what you play. Sealed is the one to reach for when you want the held sound to stay put.

**In normal freeze, a quiet copy of the live input 'leaks' through — about −41 dB.** No bin is ever exactly zero; each one holds the window sidelobes of whatever was captured. Frozen, those residuals still get the live phase, so they reconstruct as a faint but correctly-tuned copy of whatever you play in. Turning **TEXTURE** up reduces it by scrambling the phase coherence the leak depends on, without touching the held spectrum.

**Freezing on a quiet moment holds a quiet spectrum.** The captured level tracks the input amplitude linearly, so freezing during a gap, a decay tail, or between notes holds something close to silence. That is faithful — it is holding exactly what was there. It is deliberately not auto-levelled, because normalising every capture to full scale would make quiet freezes roar and flatten the dynamics of the effect. If a freeze comes out silent, re-freeze while the sound is actually playing.

Down is a plain toggle: press to freeze, press again to return to live. Each freeze captures whatever is playing at that moment — the unfrozen path is re-analysing every frame anyway, so there is nothing to arm or reset.

## How it works

A 1024-point FFT at 48 kHz, hop 256 (75% overlap), sine window. The audio
interrupt only fills a ring buffer; all the FFT work runs on the second core,
which is why the card can afford a transform this size at all.

Latency is one frame — **21.3 ms** — which is inherent to an STFT of this size
rather than a shortcoming. Output is mono.

Full detail, including the fixed-point techniques the RP2040 needs and where
this card departs from the original, is in **[docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md)**.

## Building

Requires the Raspberry Pi Pico SDK.

```
mkdir build && cd build
cmake -G Ninja ..
ninja
```

Then drag `build/spectral.uf2` onto the Pico in bootloader mode.

The DSP has a host-side test suite in `tools/` — run them if you change
anything, they take seconds. See
[docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md) for what each one covers and
[docs/FFT_VALIDATION.md](docs/FFT_VALIDATION.md) for the bugs they caught.

## Credits

- **Julius Kammerl** — the Spectral Clouds algorithm ([kammerl.de/audio/clouds](https://www.kammerl.de/audio/clouds/), [jkammerl/eurorack](https://github.com/jkammerl/eurorack)).
- **Émilie Gillet** — Clouds itself ([pichenettes/eurorack](https://github.com/pichenettes/eurorack)).
- **Chris Johnson** — the `ComputerCard` library.
- **Tom Whitwell** — the Workshop System.

## License

MIT — see [LICENSE](LICENSE). Both upstream projects are MIT; this card keeps the same terms.
