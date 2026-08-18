# v1.2.1

2026-08-18

Stability release. Fixes the module freezing when saving settings or connecting from the web UI, and restores the web UI on Linux.

Fixes:

* Fix module freeze when saving settings. The settings sector is now written without locking out the audio core, so audio runs continuously through a save instead of the ~45ms stall that scrambled the knob, CV and switch inputs
* Fix module freeze on connect and on save. USB is now driven entirely from one core; previously a burst of serial commands could race the USB driver across both cores and silence the module until it was replugged
* Fix the web UI exchanging no data on Linux. The page now asserts DTR when opening the port, which the firmware requires before it will send or receive. macOS asserted it automatically, so this only affected Linux
* The web UI now confirms a save, verified by reading the settings back from flash. Cards on older firmware cannot confirm, and are reported honestly rather than as an error

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
