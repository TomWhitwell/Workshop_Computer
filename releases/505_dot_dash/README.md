# Dot Dash

A Morse code USB keyboard card for the **Music Thing Modular Workshop Computer**.

Plug a USB keyboard into the Workshop Computer, type letters, and the card sends
them out as Morse code — as an audio beep, as a gate, or as a pitch CV sequence.

```
      .-  -. -..
   D   O   T        D  A  S  H
```

## What it does

- **Audio Out 1** — a square-wave beep for every dot and dash (audio mode).
- **CV Out 1** — a note for every symbol in pitch mode: dot is the higher note,
  dash the lower one (a musical fifth apart). Uses the card's stored calibration
  for accurate 1 V/oct when available, and falls back to a rough voltage when not.
- **Pulse Out 1** — a gate that is high for exactly as long as each dot or dash
  lasts and low during the gaps. Patch it to an envelope, a clock, or an LED.
- **LEDs** — LED 0 lights on dots, LED 1 on dashes, LED 2 while transmitting,
  LED 3 flashes if you over-type the buffer, and LED 5 shows a keyboard is
  connected.

If **no keyboard is plugged in**, the card loops `SOS` (`... --- ...`) on the
audio and gate outputs and flashes all six LEDs together in that rhythm, so you
can see it is alive but waiting for a keyboard.

## Controls

| Control | Does |
|---------|------|
| **Switch Up** | Audio mode — beep each symbol |
| **Switch Middle** | Pitch mode — send each symbol as a CV note |
| **Switch Down** | Momentary "shift": hold it to flip to the other mode while held |
| **Knob X** | Speed, 5–40 words per minute |
| **Knob Y** | Beep pitch, 300–2000 Hz (audio mode) |
| **Knob Main** | Unused in this version |

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

## Characters

Letters `A`–`Z`, digits `0`–`9`, the spacebar, and common punctuation
(`. , ? ' / ( ) : ; = - _ " @`). Letters are case-insensitive — Morse has no
upper case. Keys with no Morse meaning (function keys, arrows, Escape, …) are
silently ignored. The buffer holds 64 characters; if you type faster than the
card can transmit, the newest key is dropped and LED 3 flashes.

## Patching ideas

- Beep into a mixer or effects, gate into an envelope: a talking rhythm.
- Pitch mode into a VCO and gate into an envelope: the Morse spells a melody.
- Gate into a clock input: Morse becomes a tempo source.
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
- **Integer only.** All timing and the square wave use `int32_t`/`uint32_t`
  arithmetic, because the RP2040's Cortex-M0+ has no floating-point unit.
- **`PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`** is set in `CMakeLists.txt`; without
  it the card can fail after a reset. Code is copied to RAM (`copy_to_ram`) to
  remove flash timing jitter from the audio path.

## Licence

MIT. `ComputerCard.h` is © Chris Johnson. TinyUSB configuration adapted from the
ComputerCard `hid_keyboard_mouse` example.
