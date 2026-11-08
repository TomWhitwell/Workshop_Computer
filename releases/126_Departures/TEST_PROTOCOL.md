# Departures 0.5.1 hardware checks

Adrian reported the new pad balance sounds better and hardware tests pass on
8 November 2026. The tested image is promoted unchanged to stable 0.5.1. Repeat
these checks for future changes. Previous stable 0.5.0 is linked from the
[archive index](archive/README.md).

1. Compare 0.5.0 and this image at the same Main/X/Y positions and monitor gain.
   Start with Y low, then repeat with your normal amount of space. Hardware boots
   use different random seeds, so compare several minutes rather than single notes.
2. Listen through overlapping pad swells: pads should remain audible and breathing,
   while quieter melody notes have more room. Bass level should remain familiar.
3. Repeat in Bells and Wood Chimes. To choose Wood, hold Z Down during reset,
   Main right (LEDs 2+4), release Z, then restore Main to the comparison position.
4. Check X/Y extremes, Hold, runtime Down, all CV1 scales/LED codes, CV2 prepare
   and Pulse2 commit, external clock stop/unplug, pitch/gate outputs and audio inputs.
5. Stress both palettes with maximum controls, rapid scale/chord CV changes,
   5–10Hz clock/commit pulses and both audio inputs. Physical LED6 should stay off.
6. Check reset/power-cycle reliability and a longer listening soak. Normal boot
   still defaults to Bells; palette choice lasts until reset.

Record palette, patch, control positions, balance preference and any LED6 warning.
