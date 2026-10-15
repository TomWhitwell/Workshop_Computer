# USB host and device

The Workshop Computer USB port serves **one role at a time** for this program: either a **host** for a plugged-in MIDI controller, or a **device** talking to a laptop running the web editor. Switching roles requires a **power cycle** (or full reboot) with the desired cable layout.

Hardware **revision 1.1 or later** is required for reliable USB host (keyboard on the Computer). Device mode works on revisions that expose USB device to a PC; that is the normal programming cable path.

## Host mode

**Cable:** USB MIDI keyboard or controller → Workshop Computer USB port (often via USB-A adapter or OTG cable depending on your controller).

**Use when:** you want to play and use SETUP learn without a computer in the path.

**Behaviour:**

- Card mounts class-compliant MIDI devices.
- LED0 indicates host mount when a device is connected.
- SETUP and MIDI learn work from the hardware panel.
- No SysEx editor unless you also implement a separate path (you do not get the web UI in host mode).

If the keyboard does not appear, try a simpler controller, confirm Rev 1.1+, and check that the card booted into this program (not bootloader).

## Device mode

**Cable:** Workshop Computer **USB-C** → laptop (data-capable cable).

**Use when:** configuring maps, testing with the virtual keyboard, or relaying MIDI from a controller attached to the PC.

**Behaviour:**

- Computer enumerates as a USB MIDI device to the host OS.
- Open `web/index.html` in Chrome or Edge.
- SysEx read/write, Live engine, panel telemetry, and relay all expect this mode.
- LED0 blinks with editor/MIDI link activity; LED4 often indicates web association.

Close other programs that might grab the MIDI port before opening the editor.

## You cannot combine both

Unplugging the laptop and inserting a keyboard (or the reverse) without rebooting leaves the USB stack in the previous role. Power-cycle the Workshop System after changing which machine is on the USB port.

Typical workflow: develop maps on the laptop in device mode, save to flash, then power-cycle with only the keyboard connected for live host performance.

## MIDI relay (device mode only)

Relay forwards performance MIDI from a **second** port on the laptop (your USB keyboard) to the card. The Computer stays in device mode on USB-C. This is how you practice SETUP learn while watching the browser monitor.

Relay does not turn the card into a host; it only pipes messages through the editor.

## Power

Some keyboards expect the host to supply USB power. The Workshop Computer supplies host power on supported revisions; bus-powered hubs are rarely needed for one small controller. If a device fails to boot, try it on a PC first to rule out a faulty cable.

## Quick reference

| Goal | Mode | Connection |
|------|------|------------|
| Gig with one USB keyboard | Host | Keyboard → Computer |
| Edit maps / Live tab | Device | Computer → laptop, open web app |
| Learn in SETUP with browser open | Device + relay | Laptop ↔ Computer, keyboard → laptop, relay on |
| Flash new UF2 | Bootloader | UF2 drag (not host or device MIDI) |

More troubleshooting: [FAQ](../faq.md).
