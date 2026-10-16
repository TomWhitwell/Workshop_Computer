# Vortex Runner — 8mu Preset Browser Experiment

This third 8mu experiment retains the layered 8mu controller and adds a
hardware preset browser. It is separate from the stable release and earlier
8mu experiments.

Flash `Vortex_Runner_8mu_preset_browser_experimental_20261003.uf2` using the
normal Workshop Computer card-flashing workflow. This is experimental
firmware: it does not replace the stable card-580 release.

## Combined preset bank

The browser contains all 31 factory presets from the Vortex Runner Web editor,
followed by saved card slots in numeric slot order. Factory patches are built
into the UF2 and cannot be overwritten. Saved card slots remain editable in
the Web MIDI editor.

The six Workshop Computer LEDs show the one-based combined preset number in
binary. For example:

| LEDs lit | Browser entry |
| --- | --- |
| LED 1 | 1: Init |
| LED 2 | 2: Doctor Who Theme |
| LED 1 + LED 2 | 3: Initial Brass |
| LED 1–5 | 31: Bass |
| LED 6 | 32: first saved card slot |

The LEDs are the Workshop Computer panel LEDs, not the 8mu LEDs. The 8mu LEDs
continue to show its controller layer.

### Factory preset numbering

The factory portion of the browser is fixed and always occupies entries 1–31:

| No. | Factory preset | No. | Factory preset |
| ---: | --- | ---: | --- |
| 1 | Init | 17 | String 2 |
| 2 | Doctor Who Theme | 18 | Brass 1 |
| 3 | Initial Brass | 19 | Brass 2 |
| 4 | Muted Brass | 20 | Brass 3 |
| 5 | Soft String | 21 | Clavichord 1 |
| 6 | String Brass | 22 | Clavichord 2 |
| 7 | Christmas Bass | 23 | Harpsichord 1 |
| 8 | Warm Horn | 24 | Harpsichord 2 |
| 9 | Blade Runner Brass | 25 | Organ 1 |
| 10 | Organ Glow | 26 | Organ 2 |
| 11 | Bandpass Dream | 27 | Guitar 1 |
| 12 | Plucked Filter | 28 | Guitar 2 |
| 13 | Hounds String 3 | 29 | Funky 1 |
| 14 | PWM Flute Lead | 30 | Funky 2 |
| 15 | Babooshka Guitar | 31 | Bass |
| 16 | String 1 |  |  |

Saved slots begin at entry 32, in numerical card-slot order. For example, if
slots 1 and 4 contain patches, they are browser entries 32 and 33
respectively; empty slots are skipped.

## Boot browser

1. Hold the Workshop Computer switch **Down** while powering on.
2. Turn **Main** to select an entry. The six panel LEDs display its number.
3. Release Down to arm the selection.
4. Press Down again to load it.

Choosing a saved entry at boot also makes that saved slot the later automatic
startup slot. Choosing a factory entry loads it for the current boot but does
not overwrite the saved startup-slot setting.

## Live browser

1. While playing, hold the Workshop Computer switch **Down** for about two
   seconds.
2. Keep holding Down and turn **Main** to browse; the current sound continues
   unchanged while the panel LEDs show the candidate number.
3. Release Down to arm the candidate.
4. Press Down once to load it.

The selection is not saved automatically. Use the Web MIDI editor to save a
modified patch to a card slot.

## Hardware test checklist

1. Boot with no buttons held and confirm the normal patch and normal panel LED
   behavior return.
2. Hold Down at boot, choose entries 1, 2, 3, and 31, and confirm both the
   binary LED indication and the expected factory sound after the confirming
   Down press.
3. Save two visibly different patches to non-adjacent card slots in the Web
   MIDI editor. Reboot into the browser and confirm they appear immediately
   after factory entry 31, with no empty-slot positions between them.
4. At boot, load one of those saved entries, reboot normally, and confirm it
   is now the automatic startup patch. Load a factory entry at boot, reboot
   normally, and confirm that it did not overwrite that saved startup choice.
5. While playing a sustained note, hold Down for about two seconds. Confirm
   the sound continues while Main changes only the candidate number on the
   panel LEDs. Release, press Down once, and confirm the candidate loads.
6. Connect an 8mu at power-on and repeat a live-browser check. Then press A,
   B, C, and D to confirm the 8mu layer LEDs and faders still operate after
   leaving the browser.

Do not connect the Web MIDI editor while the 8mu is connected: the Workshop
Computer USB port is operating as a host for the 8mu in that configuration.

## 8mu controller

The 8mu behavior is unchanged from the layered-controller experiment:

- Button A: Tone
- Button B: Amp envelope
- Button C: Filter envelope
- Button D: Performance

Each layer retains soft takeover. USB host mode with an 8mu and Web MIDI editor
mode remain mutually exclusive.

## Validation status

The firmware completed a clean configure/build/link. Hardware testing is still
required for factory browsing, saved-slot ordering, binary LED display, long
hold/release/confirm behavior, and 8mu reconnects.

SHA-256:

```text
7293f37a6d2cc3dfa698281b8b578d6fc2eb7c3b13e4d0f2a8fd157976d5c6a7
```
