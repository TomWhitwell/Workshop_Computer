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

This remains one monophonic instrument, not two independently playable voices.
Pitch, gate, amp/filter envelopes, portamento, CV choices/routing, MIDI
handling, LFO timing, and preset state must remain shared. Any future A/B
parameters are limited to oscillator/filter colour and detune, and the Web UI
must not expose an A/B control until the firmware implements that parameter.

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

- this folder starts from the tested stable firmware baseline, but uses an
  incompatible experimental Web MIDI v10 patch format and flash bank
- the Web UI exposes independent A/B source mixer levels, output levels, pulse
  width, PWM, HP cutoff, LP cutoff, and resonance; MIDI CC source/level
  controls continue to move both outputs together as shared performance gestures
- hardware and Web UI now cover the same A/B tonal controls; detune remains a
  hardware performance control. All note, envelope, CV, MIDI, portamento, and
  LFO state remains shared
- this is not a stable firmware candidate until rapid Web MIDI updates, saved
  slots, MIDI playback, CV playback, and A/B level balance have been tested
