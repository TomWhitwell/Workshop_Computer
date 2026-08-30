# CS80 Dual Output Experimental

This folder is the experimental branch for the next CS80 direction. The main
`580_CS80` contains the current stable split-output monophonic firmware and
rollback UF2s. This folder remains the place for deliberately experimental
per-voice extensions.

The purpose of this folder is to explore a split-output dual-voice layout with:

- `Audio Out 1`: Voice A
- `Audio Out 2`: Voice B
- short `Down` tap toggling which voice page is being edited
- visible LED feedback for whether Voice A or Voice B is selected

Target control layout for this experiment:

- `Switch Up`: filter page
  - `Main`: LP cutoff
  - `X`: HP cutoff
  - `Y`: resonance
- `Switch Middle`: oscillator page
  - `Main`: overall base pitch / pitch offset for both voices
  - `X`: pulse width for the selected voice page
  - `Y`: PWM amount for the selected voice page
- `Switch Down` while held: performance page
  - `Main`: Voice B detune relative to Voice A, with a target range of
    `-12` to `+12` semitones
  - `X`: ring-mod amount
  - `Y`: LFO-to-pitch depth

Target LED behavior for this experiment:

- `LED0` / `LED1`: selected voice page
  - Voice A selected: `LED0` bright, `LED1` dim
  - Voice B selected: `LED0` dim, `LED1` bright
- `LED2` / `LED3`: active `X` value and pickup
- `LED4` / `LED5`: active `Y` value and pickup

Current implementation note:

- the existing experimental firmware is still an earlier split-output prototype
  with shared shaping for many parameters
- the experimental firmware and copied Web UI now include the stable `v7`
  patch protocol, `Init` preset, and combined CV filter pairing behaviour
- the experimental Web UI labels the current dual-output behaviour, but Web MIDI
  still sends one shared patch; per-output LP/HP/resonance and pulse/PWM remain
  hardware-panel experiments until the protocol grows proper A/B parameter fields
- this folder is now the place where the new A/B page, detune, and per-voice
  control work should happen next
