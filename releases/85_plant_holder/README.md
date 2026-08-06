# PlantHolder — Computer Card

**PlantHolder** is a Workshop Computer card originally intended as a companion for the [Instruo Pocket
Scion](https://instruomodular.com/product/pocket-scion/). The name is a nod to
two ideas: the Scion’s origins in **plant biosonification**, and the
**Householder-reflection** feedback matrix at the heart of the Fairfield
Circuitry [**Placeholder**](https://cdn.shopify.com/s/files/1/0234/8231/files/EB_Specifications.pdf?v=1774487936) reverb this card emulates.

While it was originally designed for the Scion, it's just a generally fun reverb that's also a MIDI host. Use it with any USB MIDI device you like! 

**The reverb will self-oscillate at around 2–3 o'clock on the decay knob.** This is highly interactive with Size, Tone, LPF, and overall Mix. 

---

## Features

- **USB MIDI host** — plug the Pocket Scion or any USB MIDI device into the Computer’s USB port. The Computer's USB port will power the Scion, which is a nice bonus!
- **2-channel CV/Gate** — MIDI channels **1** and **2** (route voices in the
  Scion companion app, or your device's MIDI Settings); v/Oct + gate with ±2-semitone pitch bend.
- **Reverb** — three cross-coupled delay lines with a tone-tilted feedback
  path (Placeholder EB, 12–160 ms). DECAY runs from a single reflection up
  into self-oscillation.
- **CV** — CV In 1 → DECAY, CV In 2 → SIZE.

---

## Board requirement

USB host mode requires **Rev1_1** or newer (Q2 2025+). On an older board the
USB host is not started and **LED 0 blinks** slowly; on a Rev1_1 board LED 0 is
off until the USB device is mounted.

---

## Hardware connections

| Computer jack | Role |
|---------------|------|
| Audio In 1/2  | L/R audio input |
| Audio Out 1/2 | L/R reverb output |
| CV Out 1 / Pulse Out 1 | v/Oct + gate — MIDI ch 1 |
| CV Out 2 / Pulse Out 2 | v/Oct + gate — MIDI ch 2 |
| CV In 1       | DECAY (bipolar trim) — controls the reverb's decay time |
| CV In 2       | SIZE (bipolar trim) — controls the reverb's size |

---

## Controls

### Switch Up — SIZE / RATIO

| Knob | Function |
|------|----------|
| Main | MIX |
| X    | SIZE (12–160 ms) |
| Y    | RATIO |

### Switch Middle — DECAY / TONE

| Knob | Function |
|------|----------|
| Main | MIX |
| X    | DECAY |
| Y    | TONE |

### Switch Down (hold > 1 s) — MOD (LED 5)

Toggle with a 1 s hold on Down.

| Knob | Function |
|------|----------|
| Main | Wet hi-cut (1 kHz → open) |
| X    | MOD depth |
| Y    | MOD type — none / cyclical / random / both |

---

## Reverb topology

Per the [EB Specifications](https://cdn.shopify.com/s/files/1/0234/8231/files/EB_Specifications.pdf):

```
mono in ──[HPF]──► 3 delay lines (SIZE / RATIO)
                     ├─ nested self-feedback, out of phase
                     └─ 4th path: Σ ──[TONE tilt]──► in phase
                    both scaled by DECAY, so self cancels
                              └──► Σ/3 ──[TONE]──[HI-CUT]──► stereo wet
```

All four feedback paths share the DECAY gain, so each line's own contribution
cancels and what remains is pure cross-feedback — "every delay line feeds back
only to every other delay line". The in-phase mode therefore has a gain of
2 × DECAY, which crosses unity around three quarters up the knob: that is the
pedal's documented onset of self-oscillation at 2–3 o'clock. Past it the card
sings, bounded by a soft limiter rather than running away.

TONE sits **inside** that fourth path as well as on the wet output, so it acts
as a damping control — which is why oscillation is easier to provoke at either
extreme of TONE than at noon.

Implementation: `placeholder_reverb.h` (`PlaceholderReverb`).

**Known limitation:** delay-line length is slewed one sample per sample with no
fractional interpolation, so large SIZE sweeps pitch-shift audibly and deep MOD
is grainy. Fine for slow moves; not a smooth-scrub delay.

---

## LEDs

| LED | Indication |
|-----|------------|
| 0 | Solid: Scion mounted. Blinking: board older than Rev1_1 |
| 1 | MIDI activity |
| 2 | Gate ch 1 |
| 3 | Gate ch 2 |
| 4 | Unused |
| 5 | MOD page latched |

---

## Building

```bash
cd releases/85_plant_holder
cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk
cmake --build build
```

Or, from inside the repo's dev container (`.devcontainer/`), just `make` —
the harness stages the firmware into `UF2/`.

Flash `UF2/plant_holder.uf2`.

---

## License

MIT — see [LICENSE](LICENSE).

---

## Credits

- Fairfield Circuitry Placeholder (EB) — reverb topology.
- [rppicomidi/usb_midi_host](https://github.com/rppicomidi/usb_midi_host).
- [ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard) — Chris Johnson.
