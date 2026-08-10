# v1.2

2026-07-08

Adds multiple configurable outputs to pulse and CV out — arpeggio (with loop toggle, pedal & random-walk patterns and a selectable root tone), pitch detector, envelope followers and audio/onset detectors. All pitch CVs share a middle-C (0V) reference. Includes stability fixes for pitch-tracking audio glitches, settings-save lockups and octave jumps.

# v1.1.1

2026-02-18

Bugfixes:

* Fix crash when resetting chord progression through long hold of Z switch
* Reduce output level of tuning mode to match normal mode

# v1.1

2026-02-04

This versions adds chord change from pulse trigger as well as a browser UI for picking chords and their order.

# v1.0.1

2026-01-30

Bugfix: Adds PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64 to CMakeLists.txt

# v1.0

2026-01-12

First public version. Includes 11 chord modes, 1v/oct, audio and pulse in & stereo output.
