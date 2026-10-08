# Dot Dash

A Morse code USB keyboard card for the **Music Thing Modular Workshop Computer**.

Plug a USB keyboard into the Workshop Computer, type letters, and the card sends
them out as Morse code — as an audio beep, as a gate, or as a per-character
pitched melody.

```
      .-  -. -..
   D   O   T        D  A  S  H
```

## What it does

All outputs are on separate jacks, so they always play together. Patch whichever
you want; Knob Main is the master level for the two audio outputs.

- **Audio Out 1** — a square-wave beep for every dot and dash.
- **Audio Out 2** — a triangle-wave melody voice at the character's pitch, so
  you can hear the Morse as a tune without external gear.
- **CV Out 1** — one note per character, rising with the alphabet: **A** is the
  lowest (middle C), then B, C, … up through Z and the digits 0–9. A dot and a
  dash within one character share that note, so each letter, number, and symbol
  has its own pitch and a typed word becomes a melody. Uses the card's stored
  calibration for accurate 1 V/oct when available, and falls back to a rough
  voltage when not. Tracks the Transpose input.
- **CV Out 2** — the current transmission speed as a voltage: 0 V at 5 WPM,
  +5 V at 60 WPM.
- **Pulse Out 1** — a gate that is high for exactly as long as each dot or dash
  lasts and low during the gaps.
- **Pulse Out 2** — a short ~2 ms trigger at the start of each dot or dash,
  handy for clocking a sequencer on every symbol.
- **LEDs** — LED 0 lights on dots, LED 1 on dashes, LED 2 while transmitting,
  LED 3 flashes if you over-type the buffer, LED 4 shows the card is paused, and
  LED 5 shows a keyboard is connected.

If **no keyboard is plugged in**, the card loops `SOS` (`... --- ...`) on the
audio and gate outputs and flashes all six LEDs together in that rhythm, so you
can see it is alive but waiting for a keyboard.

## Controls

| Control | Does |
|---------|------|
| **Switch** | Unused — all outputs always play together on their own jacks |
| **Knob Main** | Master audio volume (fully down is silent; the CV outputs are unaffected) |
| **Knob X** | Speed, 5–40 words per minute |
| **Knob Y** | Beep pitch, 300–2000 Hz |
| **CV In 1** | Transpose the notes, 1 V/oct, clamped to ±2 octaves |
| **CV In 2** | Modulate the speed, up to ±20 WPM, final speed clamped 5–60 WPM |
| **Pulse In 1** | Pause while held high (unpatched runs normally) |
| **Pulse In 2** | A rising edge clears anything typed but not yet sent |

## Morse timing

The card follows the standard "Paris" timing, where one unit is the length of a
dot and speed is measured in words per minute:

```
dot         = 1 unit
dash        = 3 units
symbol gap  = 1 unit   (between the dots and dashes of one letter)
letter gap  = 3 units
word gap    = 7 units   (letter gap plus 4 more, triggered by the spacebar)
```

Because the speed knob is re-read every sample but only *latched when a symbol
starts*, turning it never stretches a dot or dash that is already playing.

## Character pitches

Every letter and digit has its own note, rising in order so the alphabet plays
as an ascending scale:

```
A = middle C (MIDI 60)   B = 61   C = 62   ...   Z = 85
0 = 86  1 = 87  ...  9 = 95
```

A character's dots and dashes all sound at that one note; the note is held
through the gaps *within* a character and its trailing letter gap, then drops
to 0 V at a word gap or when the card is idle. Punctuation has no note of its
own — it carries on at the pitch of the character before it. The Transpose
input shifts the whole ladder up or down.

## Characters

Letters `A`–`Z`, digits `0`–`9`, the spacebar, and common punctuation
(`. , ? ' / ( ) : ; = - _ " @`). Letters are case-insensitive — Morse has no
upper case. Keys with no Morse meaning (function keys, arrows, Escape, …) are
silently ignored. The buffer holds 64 characters; if you type faster than the
card can transmit, the newest key is dropped and LED 3 flashes.

## Patching ideas

- Beep or melody into a mixer or effects, gate into an envelope: a talking rhythm.
- Character pitch CV into a VCO and gate into an envelope: the typed words play as a melody.
- Transpose CV from a sequencer or keyboard: play the Morse at different pitches.
- Speed CV from an LFO: the transmission breathes faster and slower.
- Pulse Out 2 into a clock input: every dot and dash advances a sequencer.
- Pulse In 1 as a mute: hold a gate high to freeze the card mid-message.
- Leave it unpatched with no keyboard to use it as an SOS beacon.

## Building

The card builds with the Pico SDK and the ComputerCard library. From the repo root:

```sh
./scripts/build.sh releases/505_dot_dash
```

That produces `dot_dash.uf2`, copied next to the source. Hold **BOOTSEL** on the
Workshop Computer, connect USB, release, and copy the `.uf2` to the `RPI-RP2`
drive.

## Notes on the implementation

- **Two cores.** Core 0 runs the TinyUSB host stack and watches for a keyboard.
  Core 1 runs `ComputerCard::Run()`, the 48 kHz audio engine. They communicate
  through a small lock-free ring buffer of characters plus a couple of volatile
  flags, so the audio side never blocks waiting for USB.
- **Integer audio path.** All per-sample work (timing, square and triangle
  waves, CV) is `int32_t`/`uint32_t` arithmetic, because the RP2040's Cortex-M0+
  has no floating-point unit and division is slow. The single exception is the
  melody note's frequency, which uses a single-precision `exp2f` — but only when
  the note *changes*, never per sample.
- **Caching.** The WPM division, the melody note's phase step, and the beep
  pitch are recomputed only when the relevant knob, input, or character changes,
  keeping the hot path to a few compares.
- **Jack detection.** `EnableNormalisationProbe()` makes unpatched CV/pulse
  inputs read exactly zero, so with nothing plugged in there is no stray
  transposition, speed change or pause.
- **`PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`** is set in `CMakeLists.txt`; without
  it the card can fail after a reset. Code is copied to RAM (`copy_to_ram`) to
  remove flash timing jitter from the audio path.

## Licence

MIT. `ComputerCard.h` is © Chris Johnson. TinyUSB configuration adapted from the
ComputerCard `hid_keyboard_mouse` example.
