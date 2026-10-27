# Gnarly C1ZZL3 8mu v2 Bank Files

The complete setup instructions, per-bank slider and button tables, and
save/revert guide are in the [main Gnarly README](../README.md#if-you-have-an-8mu).

Import each numbered JSON file into the matching 8mu controller bank and choose
**Update controller** after each import. Use these files with the current 8mu
UF2. Buttons use **Keyboard** mode (mode 0): notes 120–123 select edit actions
in banks 2–8, and notes 124–127 are ignored in bank 1. The firmware consumes
these reserved notes without playing synth pitches. Re-import all banks if
you used the earlier CC-button files.

The bank mappings are unchanged by the soft pickup update. The bundled 8mu
release UF2 shows dim slider LEDs while waiting for pickup and full brightness
after pickup, using a direct USB connection to the Workshop Computer. A factory
reset of the 8mu replaces these mappings; re-import all eight banks afterwards.
See [Soft Pickup And Slider LEDs](../README.md#soft-pickup-and-slider-leds)
for the complete behaviour, including factory envelopes and bank 8 depths.

The release UF2 includes the C-save stack correction, confirmed after reset
and readback in the web editor. It does not change these bank files. C saves the selected custom slot; reselect that same
slot after restarting to check persistence.
