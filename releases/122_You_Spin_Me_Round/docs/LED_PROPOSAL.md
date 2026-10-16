# LED behaviour suggestions

Suggestions only: no firmware changes made. Original design notes: soveda,
2026, MIT. Existing index/layout reference: Chris Johnson's ComputerCard 0.4.0;
see ../THIRD_PARTY_NOTICES.md.

## Recommended: retain the alpha layout

```
0 Horn motion     1 Drum motion
2 Fast range      3 Brake active
4 Inside mode     5 Timing warning
```

- 0/1: smooth one-cycle-per-revolution brightness pulses, tied to the actual rotor
  phase and inertia, not the requested speed. As the motor brakes, pulses slow
  and freeze at the stopped phase. This is already how the alpha behaves.
- 2: steady on for fast, off for slow, including the patched speed gate override.
  It shows the selected range, not whether acceleration has finished.
- 3: steady on whenever either Down or the brake gate is held. It indicates
  brake command even while coasting, rather than falsely claiming a complete stop.
- 4: steady on for Inside, off for default Outside. No perspective animation is
  needed while playing; keep the musical movement on the top row.
- 5: retain the latched timing warning during alpha testing. It should normally
  stay dark. Reset clears it. Do not repurpose it until timing is validated.

At boot selection, use the whole left column for Outside and the whole right
column for Inside, as implemented. Optional refinement: a single short pulse
of the selected column on confirmation, then performance display. This would
add confirmation without introducing menus or extra control gestures.

This is the simplest mapping to test first: two motion LEDs and four clear
status meanings. Avoid flashing every status or adding hidden LED pages.

## Alternative: two visible stereo rotors

Use LEDs 0/1 as a left/right horn crossfade, and LEDs 2/3 as a left/right drum
crossfade. Both rows would animate from the actual rotor phases, with opposite
directions. Bottom-left stays Inside; bottom-right indicates Brake.

This makes rotation feel more physical, but loses the dedicated Fast indicator
and timing-warning display. It would require a separate diagnostic arrangement
and should wait until hardware timing is verified. It is not recommended for
the first alpha audition.
