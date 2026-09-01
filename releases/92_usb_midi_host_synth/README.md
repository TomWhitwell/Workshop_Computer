# USB MIDI Host

USB MIDI → CV/Gate/Audio for the Music Thing Modular Workshop System Computer, with SETUP + MIDI learn, multiple engines, arpeggiator, reverb, stroke LED glyphs, and a browser editor.

## What it does

| Output | Source |
|--------|--------|
| CV Out 1 + Pulse Out 1 | Voice A (default MIDI ch 1), optionally arpeggiated |
| CV Out 2 + Pulse Out 2 | Voice B (default MIDI ch 2) |
| Audio Out 1 / 2 | 4-voice poly — **121 voice matrix** (see [`docs/VOICE_MATRIX.md`](docs/VOICE_MATRIX.md)) |

- **CV pitch:** MIDI note 60 (C4) = 0 V (Simple MIDI EEPROM cal)
- **Audio engines (CC 24):** **4-voice poly** — `0–7` basic/stacks; `8–12` famous-inspired (Moogish, Junoish, Sync, Acid, FM bell). Filter / chorus / glide / sync / FM stay **on each voice**, not on the mix bus.
- **Arp (CC 26):** `0` off; `1–4` Up / Down / Up-Down / As-played (8ths); `5–8` same patterns faster (16ths) — voice A
- **Reverb (CC 22):** wet param stored (bus FX; separate from voice character)
- **ADSR (slots 7–10):** amp envelope on each voice; factory maps **Attack → X**, **Release → Y** (Decay/Sustain unmapped — learn MIDI CC or the other knob in SETUP). Kill-switch `A=D=R=0`, `S=127` is clickless gate; X/Y only update their slots when the knobs actually move (so Live ADSR settings are not constantly overwritten).
- **Cutoff (slot 11):** per-voice filter cutoff / FM index (`0` dark … `127` open)
- **PWM (slot 12):** pulse width, sync ratio, acid wave select, or FM ratio (`64` ≈ 50% pulse)
- Last-note priority on CV; pitch bend (default ±2 semitones)
- **Drum pads (MIDI ch 10, notes 36–43):** always-on synth kit — kick, snare, closed/open hat, low/high tom, crash, ride (velocity sensitive; open hat chokes on closed hat / note-off)

## Host SETUP mode

With a MIDI source available (**host** keyboard on the Computer, or **device** mode with the web **MIDI relay**):

1. Move **Z** to Down and hold **~1 s** → enter SETUP (release and stay in SETUP — it is a toggle, not a hold-to-stay)
2. **LEDs:** all six flash on enter; while in SETUP, **LED4/LED5 alternate** and LED0–3 show the learn slot in binary
3. **Main** selects learn slot `0–12` (stroke digit on the LEDs)
4. Move a CC, press a key, **or twist X/Y** → learn that control to the slot
5. **Z Up → Middle** → save maps + config to flash, LED wipe, return to PLAY  
   (releasing Down often lands on Middle; that edge is ignored so it won’t false-exit)
6. Hold **Z Down ~1 s** again → exit SETUP without saving (same LED wipe)

Factory reset (hold Down at power-on) is separate and only applies during the first ~0.1 s of boot.

### Learn slots

| Slot | Parameter | Factory map |
|------|-----------|-------------|
| 0 | *(reserved)* | — |
| 1 | Voice A MIDI channel | — |
| 2 | Voice B MIDI channel | — |
| 3 | Pitch bend range (1–12) | — |
| 4 | Audio engine (voice matrix) | **CC 24** Omni — CC value 0–120 selects patch (see `docs/VOICE_MATRIX.md`) |
| 5 | Arpeggiator 0–8 | **CC 26** Omni |
| 6 | Reverb wet 0–127 | **CC 22** Omni |
| 7 | Attack 0–127 | **Knob X** |
| 8 | Decay 0–127 | — |
| 9 | Sustain 0–127 | — |
| 10 | Release 0–127 | **Knob Y** |
| 11 | Cutoff 0–127 | — |
| 12 | PWM 0–127 | — |

### Stroke LED glyphs

On discrete changes (slot, channel, bend, engine, arp), the six LEDs briefly draw the digit as a stroke (not a bitmap), then return to status. Layout:

```text
1 2
3 4
5 6
```

| Digit | Flash order |
|-------|-------------|
| 0 | all at once |
| 1 | 2-4-6 |
| 2 | 1-2-4-3-5-6 |
| 3 | 1-2-4-3-6-5 |
| 4 | 1-3-4-2-6 |
| 5 | 2-1-3-4-6-5 |
| 6 | 2-1-3-5-6-4 |
| 7 | 1-2-4-6 |
| 8 | 4-1-2-3-5-6-3 |
| 9 | 5-4-1-2-6 |

Each digit finishes in under ~200 ms. Continuous params (reverb) do not spam glyphs.

## USB host vs device

| Connection | Mode |
|------------|------|
| USB MIDI keyboard / controller | **Host** — play + SETUP learn |
| Laptop / `web/index.html` | **Device** — SysEx config + virtual keyboard |

Power-cycle the Workshop System to switch roles. Host needs hardware **Rev 1.1+**.

## Web editor (`web/`)

Open `web/index.html` in **Chrome or Edge** (WebMIDI + SysEx), Computer in device mode:

1. Select Workshop Computer MIDI ports → **Identify** / **Read config**
2. **MIDI relay** — choose a USB keyboard on this PC → enable **Relay to card** (play + SETUP learn without host cabling)
3. **Front panel** graphic tracks Main / X / Y / Z; readouts show engine / arp / reverb / ADSR
4. **Live engine** + **MIDI maps** — edit voice/arp/reverb/envelope/cutoff/PWM and learn table
5. **Virtual keyboard** (C1–C5) for quick CV/audio tests
6. TX log + PC MIDI monitor (monitor is log-only; use relay to forward)

Factory defaults: hold **Z Down** ~0.1 s at power-on.

## Build

Build inside the [Dev Container](../../.devcontainer/README.md) (recommended):

```bash
cd releases/92_usb_midi_host_synth
make build
```

From the repo root:

```bash
make releases/92_usb_midi_host_synth
```

Flash `UF2/92_usb_midi_host_synth.uf2`.

SysEx protocol is documented in [`sysex_spec.json`](sysex_spec.json) and [`docs/CONTROL_FLOW.md`](docs/CONTROL_FLOW.md).

## SysEx

`F0 7D 79 <cmd> [data…] F7`

| Cmd | Role |
|-----|------|
| `01` | Preview 8-byte channel/bend config to RAM |
| `02` | Save 8-byte config to flash |
| `03` | Read config |
| `04` | Card ID (`04 4F maj min patch`) |
| `05` | Panel state — streamed on change (+ knob hysteresis) with ~2 Hz keepalive |
| `06` | Panel stream on/off |
| `07` | Read extended maps block (`ExtConfig`) |
| `08` | Write extended maps block to flash |
| `09` | Set engine live: voice, arp, reverb[, ADSR][, cutoff, PWM] (RAM) |
| `0A` | SETUP learn notify → editor (slot, source, channel, CC/note) |

Firmware **0.9.0** (121-patch voice matrix, poly engines + ADSR).

### Voices
| # | Engine |
|---|--------|
| 0 | Square |
| 1 | Sine |
| 2 | Saw (polyBLEP) |
| 3 | Triangle |
| 4 | Dual saw (detuned) |
| 5 | Square + sub (octave) |
| 6 | Dual sine (detuned) |
| 7 | Saw + sub square |

Live engine / CC 24. Still 4-voice poly, clickless gate, fixed headroom.

### ADSR on/off
- **On** by default as clickless gate (factory `A=0 D=0 S=127 R=0`; X→Attack, Y→Release). Cutoff defaults open (`127`), PWM defaults `0`.
- **Envelope shape:** raise Decay/Release (and optionally Attack) in Live engine or via learned maps.
- **Off in code:** `#define USB_MIDI_HOST_ADSR 0`.
