# FAQ

Short answers to problems that come up often when using the card. For step-by-step setup, start with [Getting started](getting-started.md).

## No audio from Audio Out

Check the obvious first: cable into the right jack, mixer channel unmuted, reasonable gain. The card outputs full-scale line level; you still need something downstream to listen.

Confirm you are in **PLAY**, not SETUP. In SETUP the card is waiting for learn input, not performing normally.

Send notes on a MIDI channel the card listens to (channel 1 works out of the box for the onboard engine). The drum kit on channel 10 is separate; see [Drum kit](features/drums.md).

If you are using the web editor relay, enable **Relay to card** and pick your **controller** port, not the Computer’s own MIDI port.

## CV or gate dead on one voice

Voice A defaults to **MIDI channel 1**, Voice B to **channel 2**. Many controllers transmit on channel 1 only; CV Out 2 will stay idle until you send channel 2 data or change the channel map in SETUP or the web editor.

CV follows **last note priority** on each voice: only the most recently held note on that channel sets pitch and gate.

Pitch bend affects CV on the voice that receives the bend message. Default bend range is ±2 semitones.

## Stuck in SETUP / LEDs alternating

SETUP is a toggle. Hold **Z Down ~1 s** to enter; hold again ~1 s to exit **without saving**. To save maps and exit: **Z Up**, then **Middle** (see [SETUP mode](features/setup-mode.md)).

Releasing Down often lands on Middle; that edge is ignored so you do not accidentally save when you meant to stay in SETUP.

## USB keyboard not recognised (host mode)

Host mode needs Workshop Computer hardware **Rev 1.1+**. Older boards may not supply host power or routing correctly.

Use a simple class-compliant USB MIDI device first. Controllers that need proprietary drivers on a PC often fail on embedded host stacks too.

If you need to work anyway, use **device mode** with the web editor relay.

## Web editor does not see the card

Use **Chrome or Edge**. Safari and most mobile browsers do not expose Web MIDI reliably.

Close Serial Monitor, DAWs, or other apps that already opened the Computer’s MIDI ports. Only one application can own the device at a time.

Use a **data** USB-C cable. Charge-only cables give power but no MIDI.

Click **Connect** on the front panel section, or open Settings and **Identify**. You should see firmware **0.10.x** in the header.

## Live tab changes disappear after power-off

The Live tab writes to **RAM**. Use **Write maps to flash** on the Maps tab, or save channel config from Settings, to persist. Hardware SETUP saves when you exit with Z Up → Middle.

## X and Y knobs seem to fight the web editor

Factory map: **X → Attack**, **Y → Release**. The firmware only pushes knob values into those slots when the physical knob **moves**. That stops idle hardware from overwriting values you set in the editor.

If you want different behaviour, re-learn X or Y to another slot in SETUP, or unlearn by mapping something else to that slot.

## Envelope clicks or sounds like a gate only

Factory defaults are a **clickless gate**: Attack 0, Decay 0, Sustain 127, Release 0. Notes turn on and off cleanly with no swell.

For a shaped note, raise Decay and/or Release (and Attack if you want a slow start). Do that on the Live tab or by learning CCs to slots 7–10.

## Which CC picks the synth patch?

Factory: **CC 24**, Omni channel, values **0–120**. Each value is one cell in the 11×11 voice matrix. CC 127 on that map resets to patch 0.

Other CCs can be learned to slot 4 in SETUP if you prefer a different controller number.

## Drums double-trigger or open hat never stops

Drums live on **MIDI channel 10**, notes **36–43**. They share the audio mix but not the CV voices.

Closed hat chokes open hat. Sending note-off for open hat (39) stops a long open hat sound. Velocity scales level.

## Factory reset wiped my maps

That is what it is for. Hold **Z Down** briefly at power-on. It restores default channels, CC maps, envelope, and patch 0.

There is no undo. Keep a screenshot of the Maps tab or re-learn from your controller if you rely on custom maps.

## Can I use CV inputs or audio inputs?

Not in this firmware. CV In, Pulse In, and Audio In are unused. All pitch and gates come from USB MIDI (or relay).

## Firmware 0.9.x configs after upgrading to 0.10.0

Version 0.10 removed arpeggiator and reverb slots from saved config. Old flash data may not load cleanly; the card falls back to factory defaults for the extended map block. Re-save maps from the editor or re-learn in SETUP once after upgrading.
