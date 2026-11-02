# Castle Process

Chop up drums, voices or oscillators and switch them against crude internal
square waves. Castle Process is a harsh noise processor for the Music Thing
Modular Workshop Computer, inspired by the Fort Processor. A separate bass
pulse adds a low layer beneath the chopped sound. It is an interpretation of
the idea, rather than a faithful clone.

---

# What It Does

Castle Process combines:

* distorted and chopped external audio
* crude internal squarewave texture
* variable switching between internal and external material
* a dedicated bass pulse voice

The outputs are split by role:

* `Audio 1` carries most of the chopped external input character
* `Audio 2` carries the bass voice with supporting texture

---

# Controls

## Main Knob

Controls overall drive and voicing.

## X Knob

Controls chopping behaviour and motion.

## Y Knob

Controls tuning and internal interaction.

## Switch Middle

The default mode gives tighter, more direct chopping.

## Switch Up

Leave the switch Up for more body and squarewave colour.

## Switch Down

Hold Down to bend the sound and add chaos while you play.

---

# Inputs

## Audio In 1

Main external audio input.

The processor chops, gates and distorts this signal.

## CV 1 / CV 2

Internal control modulation inputs.

## Pulse In 1

External bass trigger input.

Patch a trigger here to set the bass rhythm yourself; it replaces the internal
bass timing.

## Pulse In 2

Reserved for further interaction.

---

# Outputs

## Audio 1

Primary chopped external-input output.

## Audio 2

Bass-focused output with supporting noise texture.

## Pulse Out 1

Bass activity pulse output.

## Pulse Out 2

Internal chop pulse output.

---

# Patch Ideas

* feed a drum loop, voice, or oscillator into `Audio In 1` for chopped destruction
* use `Audio 1` as the main harsh-noise output
* use `Audio 2` as the low companion layer
* trigger `Pulse In 1` from a sequencer or clock divider for controlled bass rhythms
* use switch `Down` as a manual performance accent

## Attribution

Castle Process uses the ComputerCard hardware framework by Chris Johnson. The
framework's original notice is retained in `ComputerCard.h`.

## Firmware distribution notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency
copyright and license texts. Keep the accompanying `NOTICE.txt` with
each UF2 when redistributing it.
