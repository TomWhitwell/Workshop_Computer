# Changelog

## 0.1.0 — 7 November 2026

First release of Departures for the Music Thing Modular Workshop Computer.

- Three independently breathing chord voices with nearby inversions in D Dorian.
- Four seeded bell phrases with unequal lengths and occasional note mutations.
- Gliding bass, drifting two-head echo and damped stereo reverb.
- Activity, Colour and Space controls; harmony hold and manual/external advance.
- External beat clock with explicit stop and unplug-to-resume behaviour.
- Optional external audio through the space, transposition CV and activity CV.
- Calibrated melody/bass pitch outputs and delayed 10ms note triggers.
- Physical LED 6 provides a latched callback timing warning.
- Host sanitizer checks, audio renders and ARM timing estimate pass.
- Adrian reported all suggested hardware torture tests passed.

The firmware bytes are identical to the tested 0.1.0-prototype image. This release
adds the publication metadata, complete release documentation and packaging.

## 0.1.0-prototype — 7 November 2026

Initial independently written prototype, inspired by Charles Vestal's AMB-1.
Sequencing/control tasks and effects updates were staggered to reduce the
estimated worst-case processing budget from 89.4% to 80.4% at 192MHz.
