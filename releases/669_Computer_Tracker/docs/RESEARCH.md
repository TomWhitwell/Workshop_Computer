# Research notes: USB control of a Workshop Computer card

Sources: the official repository
<https://github.com/TomWhitwell/Workshop_Computer> (clone at commit `c79a254`,
10 Nov 2026), ComputerCard **v0.4.0**, Pico SDK **2.2.0** (bundled TinyUSB).

## What the existing cards do

A survey of `releases/*` (USB config and web pages):

| Approach | Cards (examples) | Count |
| --- | --- | ---: |
| **TinyUSB MIDI device + WebMIDI SysEx** | 00 Simple MIDI, 06 usb_audio, 33 drumdrum, 62 BioMimicry, 67 Fragments, 118 JP8K, 369 Asterisk, 798 turings_palette, 802 CVSeq, 806 Weights, 82 Computer Grids, 93 Turing Matrix, … | ≈ 30 |
| USB CDC serial + WebSerial | 08 bytebeat, 21 resonator, 51 grains, 662 Workshop Scope, 666 ws_doom, … | ≈ 10 |

The official ComputerCard example **`examples/web_interface`** (Chris Johnson)
is the reference pattern: TinyUSB as a MIDI device, `tud_task()` on core 1,
SysEx in both directions, and a single HTML page that talks to it with
`navigator.requestMIDIAccess({sysex: true})`. `tusb_config.h` and
`usb_descriptors.c` in this repo are taken from that example (product
string changed to "Workshop Tracker", PID `0x10C1`).

## Choice: USB-MIDI SysEx + WebMIDI

| | USB-MIDI + WebMIDI | CDC + WebSerial |
| --- | --- | --- |
| Driver | class-compliant on macOS, Windows, Linux | class-compliant, but Windows sometimes needs a reboot or a COM port pick |
| Browser | Chrome, Edge, Opera (desktop and Android). Firefox has WebMIDI behind an add-on permission. No Safari. | Chrome, Edge, Opera (desktop). No Firefox or Safari. |
| Port selection | automatic: the page finds the port named "Workshop Tracker" | the user must pick a port in a dialog every time |
| Coexists with a DAW | yes (other apps see an idle MIDI port) | no (exclusive) |
| Throughput | ~100+ KB/s on full-speed USB-MIDI, plenty for 28 KB songs | higher, not needed |
| Proven in ComputerCard | official example + ~30 cards | some cards |
| Payload | 7-bit bytes only | 8-bit |

The 7-bit limit costs nothing here: every song value was designed to fit in
0–127 (tempo is split into two bytes). A whole-song upload is about 28 KB and
takes well under a second.

**Device mode only.** The Computer's USB-C port can also be a USB host (e.g. for
MIDI keyboards), but this card only implements device mode. If the module is
powered with nothing on USB, TinyUSB just idles on core 1.

## Things learned from the reference cards

* Keep USB **off the audio core**. `tud_task()` on core 1; the audio interrupt
  only talks to core 1 through lock-free queues / volatile fields.
* Flash writes stall XIP for the whole chip. Either both cores run from RAM
  or the audio core must be parked. This card uses **`copy_to_ram`** (as
  e.g. Simple MIDI, turings_palette and Spatial Disorientation do), so core 0 keeps running during
  erase/program and audio never drops out. Interrupts are disabled only on
  core 1 while a sector is erased/programmed, and `tud_task()` runs between
  sectors.
* 192 MHz needs `vreg_set_voltage(VREG_VOLTAGE_1_15)` before
  `set_sys_clock_khz(192000, true)`. In the release cards, `set_sys_clock_khz` is 192 MHz in ~39 places and 200 MHz in ~14 (144 MHz in ~43).
* `EnableNormalisationProbe()` must be called before `Run()` for
  `Connected()` to work (used here for the clock/reset inputs, transpose CV and
  skipping unplugged audio inputs).
* The switch's Down position is momentary; debounce it and treat it as a button.

## Browser requirements

* WebMIDI with SysEx needs a **secure context** (`https://` or
  `http://localhost`) and a permission prompt ("Control and reprogram your MIDI
  devices"). Opening `index.html` as a `file://` URL will **not** work in Chrome.
* GitHub Pages (https) or `npx serve web` / `python3 -m http.server`
  (localhost) both work.
