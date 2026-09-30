# Gnarly C1ZZL3 8mu v2 Banks

Import these files one at a time with the [16n Faderbank editor](https://16n-faderbank.github.io/editor/).
Use the 8mu's bank buttons to select the destination bank before importing.

1. `01-performance.json` keeps the original Gnarly performance controls.
2. `02-amp1-stages.json` through `07-pitch2-stages.json` map faders 1-8 to stages 1-8 of the named envelope lane.
3. In the lane banks, Button A selects level editing, Button B selects time editing, Button C saves the current custom envelope, and Button D reverts the current lane.
4. `08-envelope-utility.json` maps fader 1 to envelope/custom-slot selection, faders 2-7 to Amp1, PD1, Pitch1, Pitch2, PD2, and Amp2 depth, and fader 8 to master depth.

Only saved custom sound-preset slots are editable. Select one with bank 8 before moving to a lane bank; factory envelopes remain read-only. Gestures are disabled in all supplied banks to avoid unintentional edits.
