# Departures 0.5.0 hardware tests

Adrian reported these hardware tests passed on 8 November 2026. The tested image
is promoted unchanged to stable 0.5.0. Repeat this protocol for future changes;
previous stable 0.4.1 remains available through the [archive index](archive/README.md).

1. **Normal boot:** start/reset in Middle with Main on either side. It should
   always start Bells and retain the familiar 0.4.1 sound and control behaviour.
2. **Boot selector:** hold Down through reset until choice LEDs appear. Audio
   should stay silent. Main left shows LEDs 1+3 for Bells; right shows 2+4 for
   Wood Chimes. Hold around noon and check that small jitter doesn't flip choices.
3. **Confirm:** release Z. Playback should fade in, normal LED roles return, and
   the first chord should not have advanced just because Down was held at boot.
   Main is now Activity again; adjust it to the same position for fair comparisons.
4. **Wood sound:** listen with X near noon and Y low, then sweep X and add space.
   Expect a rounder body, short upper-resonance attack and warmer pad colour.
5. **Runtime Down:** tap Down after playback starts. It should advance one chord
   as before, without reopening the selector or changing palette.
6. **Persistence:** after choosing Wood, reset normally without Down. Bells
   should return. Re-enter the selector to choose Wood again.
7. **Both palettes:** test all CV1 scales and LED codes, CV2 prepare/Pulse2 commit,
   Hold, octave answers/rests, beat-clock stop/unplug and external audio.
8. **Selector input isolation:** run beat/commit clocks while selecting. No notes
   or output triggers should be generated until selection is confirmed and inputs arm.
9. **Stress/soak:** in each palette run maximum controls, rapid scale/destination
   CV jumps, 5–10Hz clocks/commits and both audio inputs. LED6 must stay off; check
   reset reliability, controls and a longer listening soak.

Record chosen palette, patch, controls, LED6 state, audible symptoms and reset
recovery. During selector mode LED6 is still a timing warning, never a choice LED.
