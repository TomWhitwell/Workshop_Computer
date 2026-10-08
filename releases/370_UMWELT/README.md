# UMWELT

A four-track generative soundscape synthesizer for the Music Thing Modular Workshop Computer.

**Background, Texture, Voices, and Percussion** combine continuous synthesis, generated note sequences, and probabilistic event triggering. Chaotic and coupled nonlinear modulators vary trigger activity, track levels, timbre, and effect parameters over time.

Three knobs control track crossfade, reverb level, and event activity. Flicking the switch down **generates a new sound configuration on the hardware**: synthesis modes, tuning, note-generation rules, rhythm, modulation, and effect settings are created from a fresh random seed. Audio, events, and modulation continue to be generated during playback.

The generator uses eight broad sound categories to structure its combinations, with newly generated parameters each time. A short history discourages the two most recent categories. Shared synthesis algorithms can still produce related timbres.

## The four tracks

The available synthesis modes are listed below. Each generated configuration selects a combination of modes for the four tracks.

| Track | Synthesis methods |
|---|---|
| **Background** | Coupled oscillator banks, reaction–diffusion wavetable synthesis, resonant filtered noise, or continuously excited modal resonators for sustained tones and textures. |
| **Texture** | Short sinusoidal grains, frequency sweeps, and noise-excited resonances. Trigger timing uses coupled phase oscillators or stochastic event generators, depending on the selected configuration. |
| **Voices** | Harmonic oscillator synthesis with breath noise and formant or resonant filtering. Two phrase generators alternate playback, with pitch and amplitude modulation. |
| **Percussion** | Modal synthesis for struck resonances, FM synthesis, or impulse excitation through cascaded all-pass filters. Some configurations use short-buffer granular playback. |

The four tracks run alongside event scheduling, stereo spatialization, and a **16-line feedback delay network (FDN) reverb**. This concurrent DSP workload is demanding for the Workshop Computer's RP2040. The firmware distributes processing across both cores and uses different sample rates for different parts of the signal path.

## Quick start

Connect **Audio Out 1 / 2** to a stereo mixer or audio interface. Set all three knobs to the center and the switch to its middle position. At startup and after Randomize, allow approximately **1–3 seconds of silence for generation**, followed by the fade-in. Timing varies with the generated configuration; the estimate comes from instruction simulation, with fades and audio prebuffering adding a little more time. Then listen as the engine generates events and modulation from the new configuration. No audio input or external clock is required.

Turn the **large Main knob** to crossfade Background against the other three tracks, **X** to adjust Reverb, and **Y** to adjust Life / event activity. Flick the switch down to generate another configuration.

## Controls

| Control | Function |
|---|---|
| **Main (large knob) — Crossfade** | Turn left to emphasize Background, or right to emphasize Texture, Voices, and Percussion. Center retains both groups at their base levels. |
| **X — Reverb** | Adjust the reverb return level. Reverb decay time is determined by the generated configuration and internal modulation. |
| **Y — Life / Event Activity** | Adjust trigger probability and event rates. Turn left for lower activity and right for higher activity. Minimum retains a nonzero activity level. |
| **Switch down — Randomize** | Generate a new configuration, with a fade-out, approximately **1–3 seconds of silent generation** (simulation estimate; hardware timing may vary), and a fade-in. Holding the switch down does not repeatedly randomize. A half-second retrigger lockout applies. |
| **Switch center — Run** | Play, or resume the current sound after Stop. Returning to center does not randomize. |
| **Switch up — Stop** | Fade audio to silence in approximately 20 ms after switch debounce, then pause synthesis, sequencing, modulation, and effects. Pulse outputs go low. Return to center to resume the buffered audio and current sound. |

Randomize retains control from the current knob positions. The same knob settings can produce different results with different synthesis and sequencing parameters.

### Life response

Life adjusts internal activity values used by the event generators. It is not a fixed notes-per-second control or a master volume control. Reducing it does not interrupt sustained Background output, notes already playing, or reverb tails.

Parameter smoothing reaches approximately 95% of a knob change in 0.3 seconds. The resulting change in event density becomes apparent over subsequent triggers, so compare settings over several seconds. Existing phrases and internally triggered event bursts can temporarily remain dense at low Life settings.

For a clearer comparison, turn **Main / Crossfade** somewhat to the right, reduce **X / Reverb**, and compare the low and high ends of **Y / Life**. Disconnect **CV In 1**, since its voltage offsets the Life setting.

## Clock and connectivity

| Connection | Function |
|---|---|
| **Audio Out 1 / 2** | Stereo left / right. |
| **Audio In 1 / 2** | An input envelope follower modulates event activity and track levels. Configurations with granular playback can also capture and replay short input segments. When both inputs are connected, they are averaged to mono. |
| **CV In 1 — Life CV** | Add a bipolar CV offset to the Y Life setting. |
| **CV In 2 — Pitch CV** | Pitch transposition, smoothed and limited to ±2 octaves. Nominal scaling is 1 V/oct; tracking has not been calibrated on hardware. |
| **Pulse In 1 — Clock** | Advance event sequencing and adjust the phase of Texture's coupled rhythm generators where those are used. The active sequencing rules determine which events are triggered. |
| **Pulse In 2 — Randomize** | Generate a new configuration, with the same retrigger lockout as the switch. It also works while stopped; playback waits until the switch returns to Run. |
| **CV Out 1 — Modulation CV** | Bipolar output from the slow chaotic modulator. |
| **CV Out 2 — Activity CV** | Unipolar output proportional to the combined Texture and Voices activity parameters. |
| **Pulse Out 1 — Percussion triggers** | Short triggers indicating Percussion events. |
| **Pulse Out 2 — Phrase triggers** | Short triggers indicating the start of a Voices phrase. |

While stopped, Clock input is ignored, audio capture pauses, pulse outputs remain low, and CV outputs hold their latest modulation values.

The internal sequencer operates without an external clock. After approximately three seconds without incoming clock pulses, external tempo following expires. Connecting a clock does not quantize every event: independent event generators and sustained synthesis remain active. Pulse and audio outputs use different buffering paths and are not sample-aligned.

## Indicators and startup state

The first four LEDs show smoothed source levels for Background, Texture, Voices, and Percussion. The lower-left LED shows the magnitude of the slow modulation signal and lights fully in Stop. The lower-right LED shows audio output activity and briefly lights when Randomize is requested.

The lower-right LED also reports **audio buffer underruns**. A fixed pattern of approximately 0.34 seconds on, then 0.34 seconds off, means that the output buffer ran empty at least once. This indication remains latched until restart; individual flashes do not represent additional underruns. Normal brightness changes follow the output level. The LED does not measure clipping.

Restart to clear the indication. If the fixed pattern returns, another underrun has occurred. The indication identifies a gap in audio delivery, but does not identify which track or processing stage caused it.

The active configuration and current synthesis state are not saved. Each power-up and each Randomize request uses a fresh 64-bit seed from the Pico SDK random-number generator, which mixes hardware entropy into its state. Generation time varies with the configuration: allow approximately **1–3 seconds of silence before the fade-in**, based on instruction simulation. Actual hardware timing may vary. Recent-category history lasts only until power-off.

## Firmware and installation

- **Creator:** Laboratory 0
- **Version:** 1.0
- **Firmware:** [Download UMWELT v1.0](UMWELT_Workshop_v1.0.uf2)
- **Platform:** Workshop Computer / RP2040, 192 MHz

UMWELT fits a **2 MB Program Card**. Its UF2 transfer file is approximately 2.90 MiB because UF2 includes transfer framing; the firmware occupies approximately **1.45 MiB of flash**. Follow the [official Program Card installation instructions](https://www.musicthing.co.uk/workshopsystem/program-cards/install/).

The stereo output runs at 48 kHz through the Workshop Computer's 12-bit DAC. DC removal, a smoothed stereo lookahead limiter, smooth master gain changes, and dither with noise shaping help control transients and quantization noise. Different parts of the synthesis engine run at lower processing rates to fit the four tracks and effects within the available CPU budget.

## License

UMWELT is distributed as freeware under the [Laboratory 0 Freeware Distribution Terms](LICENSE.txt). You may use the firmware and share unmodified copies free of charge, with the license and [third-party notices](THIRD_PARTY_NOTICES.txt) included. These terms place no restrictions on music or recordings made with UMWELT. Source code is not included in this release.

## Original VST

This firmware is adapted from **Laboratory 0's UMWELT VST**. The hardware version uses reduced polyphony and adapts one of the VST's slow modulation generators: a known chaotic model is varied through track assignment, coordinate scaling, and time scaling, keeping generation time practical. Major foreground sources run at 24 kHz, Background and Texture at 12 kHz, and reverb and the granular playback buffer at 6 kHz, with stereo DAC output at 48 kHz. The dispersive Percussion model also runs at 12 kHz. These processing choices change the sound relative to the original VST.

[Download the original UMWELT VST](https://laboratory0.gumroad.com/l/UMWELT?layout=profile).
