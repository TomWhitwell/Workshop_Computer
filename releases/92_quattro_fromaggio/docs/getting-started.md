# Getting started

This walkthrough gets you from a blank Computer to notes on CV, gates, and audio. You can do almost all of it with a USB MIDI keyboard and headphones or a mixer; the web editor is optional for the first test.

## What you need

- Workshop System Computer, hardware **Rev 1.1 or later** if you want USB host (keyboard plugged into the Computer itself).
- This program card flashed (`UF2/92_usb_midi_host_synth.uf2`). Hold the boot button if you need to copy a new UF2 onto the card.
- A USB MIDI keyboard, or a laptop with the web editor and MIDI relay (see [Web editor](features/web-editor.md)).
- Cables to CV/gate/audio destinations you actually want to hear. Audio is line level; use a mixer or interface if you need headphone volume control.

## Step 1: Install the program

Copy `UF2/92_usb_midi_host_synth.uf2` to the Computer when it appears as a USB drive. It reboots into the USB MIDI Host program automatically.

After flashing, a normal power cycle is enough. You only need the boot/UF2 workflow when you change firmware.

## Step 2: Choose how MIDI reaches the card

There are two everyday setups:

**Host (keyboard on the Computer)**  
Plug a class-compliant USB MIDI controller into the Computer’s USB port. The card acts as a USB host. This is the simplest way to play without a laptop.

**Device (laptop on USB-C)**  
Connect the Computer to a Mac or PC with a data-capable USB-C cable. Open `web/index.html` in Chrome or Edge, enable **MIDI relay**, and play a keyboard attached to the laptop. The page forwards notes to the card.

You cannot use host and device at the same time. Power-cycle to switch roles. Details: [USB host and device](features/usb-host-and-device.md).

## Step 3: Confirm you are in PLAY, not SETUP

On boot the card is in **PLAY**. If you previously entered SETUP, LED4 and LED5 alternate slowly and the Main knob selects learn slots instead of doing anything musical.

To leave SETUP without saving: hold **Z Down** for about one second (same gesture as entering). The LEDs wipe and you return to PLAY.

If you are unsure, power-cycle with Z in the middle position.

## Step 4: Patch outputs

| Jack | Default behaviour |
|------|-------------------|
| **CV Out 1** | Pitch for Voice A (MIDI channel 1), last note held |
| **Pulse Out 1** | Gate for Voice A |
| **CV Out 2** | Pitch for Voice B (MIDI channel 2) |
| **Pulse Out 2** | Gate for Voice B |
| **Audio Out 1 / 2** | Four-voice poly synth mix (stereo on some chorus patches) |

Pitch uses **1 V/oct**. MIDI note **60 (C4) = 0 V**, using the Computer’s Simple MIDI EEPROM calibration.

Patch Audio Out to a mixer or monitor input. CV and gates go to whatever modular voices you want to drive in parallel with the onboard audio engine.

## Step 5: Play a note

With a keyboard on **channel 1**:

1. Hold a key. Pulse Out 1 should go high and CV Out 1 should move with pitch.
2. You should hear the onboard synth on Audio Out. Factory patch is **CC 0** on the audio engine map (pulse, single oscillator).
3. Move the pitch wheel if your controller has one; CV Out 1 bends within the configured range (default ±2 semitones).

Channel 2 drives CV/gate pair 2 the same way. The audio engine listens on **all channels** for note data unless you change the voice maps.

## Step 6: Try a few sounds

The synth patch is chosen by **MIDI CC**, not by program change. Factory mapping: send **CC 24** with value **0–120**. Value 0 is the default pulse sound; 33 is a plain saw; 40 is a filtered “Moogish” saw; 8 is a chorus pulse reminiscent of a Juno.

If your keyboard has no dedicated CC knob, use the web editor **Live** tab or learn a knob in SETUP (see [SETUP mode](features/setup-mode.md)).

Full patch list: [Voice matrix](features/voice-matrix.md).

## Step 7: Save your work (when you are ready)

Live tweaks from the web editor sit in RAM until you write them to flash. On the hardware, learned MIDI maps save when you exit SETUP with **Z Up, then Middle**.

Factory reset: hold **Z Down** during the first fraction of a second after power-on (about a tenth of a second). That restores default channels, maps, and envelope.

## Where to go next

- [Outputs and routing](features/outputs.md) if you are splitting Voice A/B across a rack.
- [Envelope and tweaks](features/envelope-and-tweaks.md) for filter and amplitude shaping.
- [Web editor](features/web-editor.md) if you prefer configuring from a laptop.
- [FAQ](faq.md) if something in the list above did not behave as described.
