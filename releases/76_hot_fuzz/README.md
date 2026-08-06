# Hot Fuzz

A stereo fuzz/distortion + resonant wah effects card for the
[Music Thing Workshop Computer](https://github.com/TomWhitwell/Workshop_Computer).
Built on the `ComputerCard` library and the Mutable Instruments Braids SVF.
Integer-only fixed-point DSP, no web editor in v1 — just 3 pots, 1 switch,
and 6 LEDs.

> Note: settings reset on power cycle (no save, no recall), and the wah cutoff
> is shared across both stereo channels so the filter stays coherent.

## Playing Hot Fuzz

Hot Fuzz chains a fuzz into a wah, both stereo. A three-position switch
decides what the pots do:

- **Up**: Fuzz + wah. Main picks fuzz type, X = drive, Y = resonance.
- **Mid**: Fuzz + wah blend. Main = dry/wet mix, X = drive, Y = resonance.
- **Down**: Wah only. Main = sweep (or auto-wah base), X = blend, Y = resonance.

### Quick start

1. Plug audio into **Left/Right Audio In**. One channel is fine; the other just stays quiet.
2. Connect **Left/Right Audio Out** to your mixer, amp, or headphones.
3. Set the switch to **Mid**.
4. Set **Main**, **X**, and **Y** to noon.
5. Play. You'll hear dry mixed with fuzz, plus some wah resonance.
6. Turn **Main** CW for more fuzz, CCW for cleaner. **X** up for more drive. **Y** up for a sharper, more vocal wah.

From here, just turn things. The card won't bite.

### The four fuzz sounds

You pick the fuzz type in Up mode. Main is split into four equal zones, CCW to
CW. LED 5 (bottom-right) always shows which one is active.

| Zone | Main pot range | Fuzz | Sounds like | LED 5 |
|---|---|---|---|---|
| 1 | Full CCW - Half CCW | Soft | Warm, singing overdrive. Cleans up when you back off drive. | Steady dim |
| 2 | Half CCW - Center | Hard | Sharp square-wave fuzz. Cuts through. | Steady bright |
| 3 | Center - Half CW | Asym | Classic fuzz. Choked decay, splatty attack, rich in even harmonics. | Slow blink (~1 Hz) |
| 4 | Half CW - Full CW | Fold | Octave-up at low drive; metallic, bell-like, alien at high drive. | Fast blink (~4 Hz) |

The fuzz type you pick in Up mode persists when you switch to Mid or Down.
It just stops being selectable until you come back to Up.

### The three modes

#### Switch Up: Fuzz + Wah

- Main = fuzz type (four zones). X = drive. Y = resonance.
- Always full wet — no dry blend here. Wah holds wherever you last set it.
- Try: pick a fuzz, then sweep Y. That's the voice of the thing.

#### Switch Mid: Fuzz + Wah Blend

This is the one you'll live in. Main = dry/wet blend (CCW dry, CW full
fuzz). X = drive. Y = resonance. Ride the Main pot while you play and dial
in as much grit as you want.

#### Switch Down: Wah Only

Down is momentary — hold it to kill the fuzz and hear the wah on its own,
blended with dry via X. Useful as a quick "clean break" while playing. Main
sweeps the filter (300 Hz - 2 kHz), or with auto-wah on it sets a base
frequency your dynamics sweep above. Toggle auto-wah by double-tapping Down
(Down→Mid→Down within 0.5 s) or with a gate into Pulse2; LED 4 flashes to
confirm.

### Reading the LEDs

Six LEDs in three rows of two. What they show per mode:

```
  Switch Up: Fuzz + Wah

    ●   ○        Drive      —
    ●   ●        Sweep      Resonance
    ○   ●        —          Fuzz type

  Switch Mid: Fuzz + Wah Blend

    ●   ●        Drive      Blend
    ●   ●        Sweep      Resonance
    ○   ●        —          Fuzz type

  Switch Down (manual wah)

    ○   ●        —          Blend
    ●   ●        Sweep      Resonance
    ○   ●        —          Fuzz type

  Switch Down (auto-wah)

    ○   ●        —          Blend
    ●   ●        Sweep      Resonance
    ●   ●        Envelope   Fuzz type

```

### CV inputs (optional)

Two CV inputs let you drive the card from the Workshop Computer or other
modular gear.

- **CV1** (wah CV): overrides the wah cutoff in Up and Mid when patched.
  Patch a slow LFO here for an auto-sweeping wah without giving up fuzz. In
  Down mode CV1 is ignored. On first patch there's a brief debounce (~11 ms)
  before CV1 takes over — filters out normalisation-probe transients, so
  don't be surprised if the pot briefly seems to win.
- **CV2** (drive CV): adds to the X pot's drive value. Patch an envelope
  follower here for touch-sensitive fuzz. In Down mode the X pot controls
  blend, so CV2 adds to blend there instead.

### Pulse inputs

Two pulse (gate) inputs take latched toggles on a rising edge — patch a
footswitch, a clock, or any gate source.

- **Pulse1** (fuzz toggle): flips fuzz on/off. Acts in Up/Mid only — in Down
  the fuzz is already off, so the pulse does nothing. When bypassed, the wah
  still runs on the clean signal. LED 0 goes dark and flashes on each toggle.
- **Pulse2** (auto-wah toggle): flips auto-wah on/off, same as the
  double-tap Down gesture but patchable. Works in any mode. LED 4 flashes
  to confirm.

## Credits

- State-variable filter from Mutable Instruments Braids by Émilie Gillet
- Soft-clip curve inspired by DaisySP (Electro-Smith)
- Built with the Workshop Computer ComputerCard library by Chris Johnson
- Built by Joep Vermaat using opencode, using GLM 5.1

Many thanks to Tom Whitwell and the Music Thing Modular community for making this highly addictive and inspiring platform, and for the excuse to finally build the fuzz pedal I always wanted.

## License

This project is released under the [MIT License](LICENSE). Use it, modify it, fork it, break it, improve it. No warranties, no liabilities. If you build something from this, you don't owe me anything — but I'd love to hear about it.