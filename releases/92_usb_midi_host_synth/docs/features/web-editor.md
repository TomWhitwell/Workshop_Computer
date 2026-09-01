# Web editor

The folder `web/` contains a browser app for device mode: read and write configuration, watch the front panel, tweak the live engine, and forward MIDI from a controller on your laptop to the card.

Requirements: **Chrome or Edge**, a **USB-C data** connection to the Workshop System, and no other app holding the Computer’s MIDI ports.

## Opening the page

Open `web/index.html` locally or from a static server. Allow MIDI access when prompted.

The header shows connection status, firmware version after **Identify**, and **card engine state** (voice, ADSR, cutoff, PWM) when panel telemetry is active.

## First connection

1. Plug the Computer into the laptop. It boots in **USB device** mode (not host).
2. Click **Connect** on the Front panel card, or open Settings (gear) and **Identify**.
3. Confirm firmware **0.10.x** appears. Panel status should go **Live**.
4. Use the virtual keyboard at the bottom to send notes on channel 1.

If ports are missing, close DAWs and Serial Monitor and refresh the page.

## Settings (gear icon)

- Pick **MIDI in** and **MIDI out** for the Workshop Computer (often named with “Workshop” or the card title).
- **Voice A / B channels** and **pitch bend range** live here.
- **Preview** sends channel config to RAM for a quick test.
- **Save config to flash** persists channel settings separately from the maps block.

## Live tab

Change **voice matrix** patch, ADSR, cutoff, and PWM. Changes apply immediately over SysEx (RAM on the card).

Use the row/column dropdowns or the 11×11 grid. CC number updates automatically.

These values are **not** permanent until you **Write maps to flash** on the Maps tab (which stores engine defaults together with the learn table).

## Maps tab

Shows the same slot table as hardware SETUP. **Read maps** pulls flash contents; **Write maps to flash** saves. **Reset to factory maps** restores CC 24 engine map and X/Y attack/release assignment without a full hardware factory reset.

## Relay tab

Plug a USB MIDI keyboard into the **laptop**. Choose it under **Controller MIDI in**, enable **Relay to card**, and play. Note and CC traffic forwards to the Workshop System so you can perform and use SETUP learn without unplugging the Computer from the PC.

Relay sends channel messages only, not SysEx. Turn relay off to clear stuck notes.

This is the usual way to test SETUP while watching the **SETUP monitor** on the left column.

## Log tab

TX log shows SysEx and other messages sent to the card. Use it when Identify or Write maps fails silently.

## Front panel view

When telemetry runs, the graphic highlights **Main**, **X**, **Y**, and **Z** positions. Readouts show raw values. Engine knobs in the header are **not** the hardware X/Y; they show the running synth parameters (including ADSR from Live or learned CCs).

Enter SETUP on the hardware while connected; the SETUP monitor section activates with slot number, current mapping, and a learn log.

## In-page tutorial

The **Tutorial** tab duplicates much of this documentation from the editor’s perspective: getting a sound, tweaking, SETUP, maps, relay, factory reset.

## What the editor cannot do

- Switch the card into USB **host** mode (power-cycle with a keyboard on the Computer port instead).
- Use CV or audio inputs.
- Flash firmware (use UF2 copy for that).

For host-mode playing without a laptop, see [USB host and device](usb-host-and-device.md).
