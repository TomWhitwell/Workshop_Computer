# Releases  
| Folder Name | Short Description | Version | Language | Creator |
| ----------- | ----------------- | ------- | -------- | ------- |
| 00_Simple_MIDI | USB MIDI utility firmware for routing incoming MIDI notes to CV/Gate outputs.
Knob and CV positions are transmitted back to the host as MIDI CC values.
Hold the switch during boot to enter calibration mode for the two pitch/CV channels.
 | 0.6.6<br>Working but simple | Arduino-Pico | Tom Whitwell |
| 02_comingsoon | Reserved for upcoming project | 0.0<br>None | None | None |
| 03_Turing_Machine | Dual Turing-style random sequencer with tap tempo or external clocking.
Pulse In 1 overrides tap tempo as the main clock; Pulse In 2 can clock channel 2 independently.
CV In 1 controls divide/multiply on channel 2, CV In 2 applies quantized pitch offset.
Audio/CV In 1 can reset sequences; Audio/CV In 2 can CV-switch between the two presets.
<br>[Web editor](https://www.musicthing.co.uk/web_config/turing.html) | 1.5.3<br>Working but Simple | C++ (ComputerCard) | Tom Whitwell |
| 04_BYO_Benjolin | Chaotic Benjolin-inspired program combining rungler-style modulation, quantized CV, and pulse generation.
Forward/back clocks can be driven from Pulse In 1 and Pulse In 2.
The card blends internal randomness with external modulation for probability, offset, and amplitude.

The Z switch sets how shift-register cells are written on each clock:
  - Up (Double Length): toggles the write cell each clock, producing a stable loop of double length.
  - Middle (Unlock): writes from the data input (or internal noise) when data exceeds the Chaos threshold.
  - Down (Write): forces the write cell to a fixed value.
The Chaos, Offset, and Chaos VCA knobs behave the same in all switch positions.
 | 1.1<br>Released | Pico SDK | Dune Desormeaux |
| 05_chord_blimey | Trigger-driven chord arpeggiator for Workshop Computer.
CV Out 1 outputs arpeggiated notes while Pulse Out 1 emits matching note triggers.
Pulse Out 2 fires at end-of-cycle for chaining/looping; Audio Out 1/2 provide probabilistic random modulation voltages.
Use switch gestures to set fixed step length and arpeggiator direction.
 | 0.9<br>Mostly complete (for now) | C (RPi Pico SDK) | Tom Waters |
| 06_usb_audio | Class-compliant USB composite firmware providing multichannel USB audio plus USB MIDI.
Middle switch mode runs standard audio interface behavior; up switch mode enables configurable MIDI/CV/Gate mapping;
down switch mode disables MIDI for audio-only operation.
Routing, channel count, sample rate, and CV/Pulse behaviors are configurable via the web interface and can be saved to flash.
<br>[Web editor](https://vincentmaurer.de/usb-audio/midi_config.html) | 1.0<br>Release | C++ (RPi Pico SDK) | Vincent Maurer (vincentmaurer.de) |
| 07_bumpers | Bouncing-ball trigger generator and multitap delay card.
Trigger streams can be internally generated or externally clocked, with pulse outputs and related CV/audio outputs.
Patching audio input enables the delay processor; otherwise outputs act as trigger/CV generators.
 | 1.0<br>Released | C++ (ComputerCard) | Chris Johnson |
| 08_bytebeat | Bytebeat generator with 36 built-in formulas across 6 banks plus 6 user formula slots.
Main controls sample rate, X selects formula/bank or user slot, and Y controls parameter 1.
Pulse In 1 resets/triggers and Pulse In 2 toggles reverse playback.
Built-in/user formula editing is available through the included browser tool (`bytebeat.html`).
 | 0.1<br>Functional but WIP | C++/Arduino-Pico | Matt Kuebrich |
| 09_DivCom | DivCom combines a comparator and a clock divider. The comparator compares `AudioIn1` against
`Offset + Scale * AudioIn2`, while the divider counts incoming clocks and emits divided gates,
flip-flop, XOR, and counter CV outputs.
 | 0.1<br>Released | C++ (RPi Pico SDK) compat. cmake. | divmod |
| 10_twists | Twists is a Workshop Computer port of Mutable Instruments Braids. `CVIn1` and Main control pitch,
`PulseIn1` triggers the envelope/strike, and X/Y shape timbre and color. Switch down toggles between
configured oscillator model sets, and `twists.html` can be used over USB for shape selection/config.
 | 0.1<br>Functional but WIP | C (RPi Pico SDK) | Random Works |
| 11_goldfish | Goldfish is a multi-mode stereo delay/looper with synchronized pulse/CV outputs. The switch selects
record, delay, and play behaviors. Audio is streamed to flash (IMA-ADPCM), so loops/delays run from
~55s on a 2MB card up to ~9min on 16MB. Pulse inputs provide clock/reset (and gated recording); the
CV/audio inputs modulate delay time, playback position and speed, and the generated CV/pitch material.
 | 2.0<br>Released | Pico SDK | Dune Desormeaux |
| 12_am_coupler | AM Coupler generates a medium-wave AM carrier and modulates it with audio from `AudioIn1/2` (or an
uploaded WAV when no audio inputs are patched). Main sets coarse carrier frequency, X + `CVIn1` fine tune,
and Y controls modulation amount. Switch (or `PulseIn1` gate when patched) controls RF on/off.
 | 1.0<br>Released | C++ (ComputerCard) | Chris Johnson |
| 13_noisebox | Noisebox selects among 13 noise algorithms with Main, with X/Y shaping the active algorithm.
Main can be offset by `AudioIn1`, output level is VCA-controlled by `AudioIn2`, and switch down
randomizes control offsets (hold to reset). `PulseIn1` clocks sample-and-hold behavior; switch up
or `PulseIn2` enables sample-rate/bit reduction.
 | 1.0<br>Released | C++ (ComputerCard) | Eric Gao |
| 14_cvmod | CVMod records one CV stream into a loop and reads it out through four moving heads. X + `CVIn1`
sets loop duration, Main + `AudioIn2` sets head speed, and Y + `CVIn2` sets head phase offset.
Switch down (or `PulseIn1`) resets read-head position; switch up cycles motion function.
 | 1.0<br>Released | C++ (ComputerCard) | Chris Johnson |
| 15_MLRws | MLRws is a six-track sample-cutting instrument with grid and gridless workflows. It supports on-card
sample storage, recording/playback, and USB grid interaction. In gridless mode, Main/X/Y plus switch
manage track selection, level/speed/start-position/radiate behavior, while pulse/CV I/O provides
reset/clocking and CV envelope/pitch triggers.
<br>[Web editor](https://dessertplanet.github.io/MLRws-web/) | 1.1.5<br>Released | Pico SDK | Dune Desormeaux |
| 16_the_bells | A bellringing methods sequencer: plays 12 traditional English change-ringing methods
(Plain Hunt Minimus through Plain Bob Triples, 4-7 bells), sending each row of the
method as a v/oct pitch sequence quantised to one of six scales, with a synchronised
gate pulse per bell strike. Intended to drive oscillators or other CV inputs that can
respond to permutation patterns.

Z switch selects mode: Up = Play, Middle = Stop, Down (hold) = Edit. While held down,
turning a knob selects that knob's edit-mode parameter (method / clock divide / scale);
releasing Z returns to Play/Stop with the new parameter applied.
 | 1.0.0<br>Released | C++ (ComputerCard) | James Saunders |
| 17_COMET | Clocked lo-fi reverb and delay | 1.0<br>Released | C++ (Pico SDK, ComputerCard) | EME |
| 18_chord_organ | Chord voice generator with 16 chord types, root CV control, and selectable progression patterns.
Main + CV1 select chord, X + CV2 set root pitch (1V/oct), and Y selects one of 9 progressions.
PulseIn1 advances the progression and retriggers; PulseIn2 or Z-down cycles waveform.
Z up enables glide, Z middle disables glide. AudioIn1 acts as a VCA CV.
 | 0.1<br>Working | Pico SDK (C++), ComputerCard | jkeyworth |
| 19_CA_Sequencer | 16-cell gate and quantized CV melody generator inspired by NLC Cellular Automata, using CA rules 90 & 150 on a 4x4 grid | 1.1<br>Released | C++ (Pico SDK) | Ainews |
| 20_reverb | Stereo reverb card with configurable utility routing for CV, pulse, clocks, Turing Machine, and Bernoulli gate.
Top audio jacks are fixed to reverb I/O; most other controls and jacks are assignable in the web editor.
Z up passes input normally, Z middle enables input noise gate, Z down freezes the reverb.
Hold Z down while loading to restore default configuration.
<br>[Web editor](https://www.musicthing.co.uk/web_config/reverb.html) | 1.5<br>Released | C (RPi Pico SDK) | Chris Johnson |
| 21_resonator | Four-string sympathetic resonator inspired by Rings/tanpura techniques.
Feed audio to excite resonant strings, or trigger plucks with PulseIn1.
X sets base pitch (or fine tune with CV1 connected), Y sets damping, Main sets wet/dry mix.
PulseIn2 or short Z-down press advances chord mode; long Z-down hold resets progression defaults.
<br>[Web editor](https://johaneklund.io/resonator) | 1.2<br>Released | C++ (ComputerCard) | Johan Eklund |
| 22_sheep | Granular buffer processor with triggerable grains, reverse/forward playback, loop/glitch mode, and freeze.
Main controls grain speed or CV2 pitch attenuation, X controls delay spread (or CV1 attenuation), Y sets grain size.
PulseIn1 triggers grains; PulseIn2 acts as a grain gate. Z up freezes buffer, Z middle runs normal mode, Z down enables loop/glitch behavior.
Two firmware builds are provided in source comments: lo-fi longer buffer and hi-fi shorter buffer.
 | 1.2<br>Released | Pico SDK | Dune Desormeaux |
| 23_SlowMod | Four related LFO channels with cross-modulation and per-channel VCA behavior.
Main sets global LFO rate from very slow to low audio range (AudioOut1 fastest, CVOut2 slowest).
X sets cross-modulation intensity; Y crossfades each output with an inverted neighboring channel.
PulseIn1 or Z up pauses motion; PulseIn2 or Z down randomizes phases.
 | 0.1<br>Released | C++ (RPi Pico SDK) compat. w/ cmake and Arduino IDE. | divmod |
| 24_crafted_volts | CV utility for generating manual voltages and attenuverting incoming control signals.
Main, X, and Y knobs each drive a paired output; patching into corresponding inputs replaces knob value and applies attenuverting.
Pulse outputs follow Z switch state as complementary high/low gates.
LEDs mirror output voltage levels in the same 2x3 physical layout as output jacks.
 | (see source repo)<br>Released | Rust (Embassy framework) | Brian Dorsey |
| 25_utility_pair | Utility Pair splits the card into independent left and right utility engines.
Each side owns one knob/control and one column of input/output jacks, letting two utilities run at once.
In the 2025 multi-pack firmware, hold Z down at power/reset to enter utility selection mode, then choose left and right utilities and start.
Specific jack behavior depends on the chosen utility pair.
 | 1.0<br>Released | C++ (ComputerCard) | Chris Johnson |
| 26_clockwork | 6-channel polyrhythmic clock, gate, and LFO/envelope generator inspired by Pamela's Workout.<br>[Web editor](https://vincentmaurer.de/clockwork/index.html) | 1.0.0<br>Released | C++ (RP2040 Pico SDK) | Vincent Maurer |
| 27_Siren | Siren is a drone source with 6 oscillator banks (SINE, CLST, DTON, ANLG, WSHP, WAVE).
Use switch up for WARP/SPAN/MORPH and switch middle for SEED/SCAN/BASIS.
Switch down (momentary) cycles banks with a smoothed crossfade.
Pulse In 1 gates the drone; Pulse In 2 randomizes SEED on short trigger and cycles bank on long hold.
Audio In 1 can be processed and mixed with the drone; Audio In 2 modulates SPAN.
 | 0.2<br>Functional but WIP | C++ (ComputerCard / Pico SDK) | Moses Hoyt |
| 28_eighties_bass | Eighties Bass is a complete mono bass voice using 5 saw oscillators plus optional white noise.
Main sets filter cutoff, X sets pitch offset, and Y sets resonance.
Press switch down to cycle filter mode (LPF/BPF/HPF).
CV In 1 provides pitch CV, CV In 2 offsets cutoff, Audio In 1 modulates detune, and Audio In 2 modulates noise amount.
 | 0.1<br>Functional but WIP | Arduino (arduino-pico) with Mozzi 2 | Tod Kurt (@todbot) |
| 29_XHT | XHT Card is a synthesised deep-note-style gesture rather than a bundled sample.
Main sets the note position and one-shot destination. The momentary down switch
resets the note and launches a one-shot from the start toward the current
destination. CV2 also participates in destination control. P2 can clock stepped
movement through the note. X controls delay and Y controls reverb.

CV1 input transposes pitch. CV Out 1 mirrors note position unless CV1 is
patched, in which case CV Out 1 becomes pitch. CV Out 2 always mirrors note
position. P1 gates the note when patched and leaves the note sustained when
unpatched.

The separate MIDI build adds MIDI note input, CC1/mod-wheel destination
control, sustain pedal on CC64, and MIDI note output of the current chord.
MIDI clock is ignored.
 | 0.1.0<br>Beta | C++ (Pico SDK) | Adrian Vos |
| 303_acid | A deliberately slightly fiddly 16-step sequencer capturing some of the obtuse workflow of the
original 303. Walk a cursor through the pattern, setting pitch and step type (rest / normal /
accent / slide) per step. Pitch and step-type sequences can be independently phase-shifted and
reset during playback.
 | 1.0.0<br>Released | C++ (ComputerCard) | Samuel Smith |
| 30_cirpy_wavetable | cirpy_wavetable loads WAV wavetables and plays them as a continuously running oscillator.
Main sets wavetable position, X sets LFO modulation amount, and Y sets LFO rate.
Switch down selects next wavetable file; switch up toggles CV pitch quantization.
CV In 1 controls pitch and CV In 2 offsets wavetable position.
CV Out 1 mirrors wavetable position and CV Out 2 mirrors LFO modulation signal.
 | 0.1<br>Functional but WIP | CircuitPython | Tod Kurt (@todbot) |
| 31_esp | ESP processes incoming audio through a gain stage and bandpass section, then derives envelope, gate, trigger, and pitch control signals.
Main controls preamp gain, X sets lower cutoff, and Y sets upper cutoff.
In switch middle, pitch updates are held unless gate is high; otherwise pitch tracks continuously.
Audio Out 1 is post-gain signal and Audio Out 2 is bandpassed output.
 | 1.0<br>Released | C++ (ComputerCard) | Ben Regnier |
| 32_vink | Vink provides two independently time-modulated delay taps with optional sigmoid saturation blend.
Main controls base delay time, X controls tap 2 offset, and Y crossfades dry to saturated output.
In switch up, outputs are split per tap; in middle/down, taps are mixed to mono on both outputs.
CV In 1 and CV In 2 modulate tap times. Pulse outputs track tap periods.
 | 1.1<br>Functional | C++ (ComputerCard) | Ben Regnier |
| 33_drumdrum | drumdrum is an 8-step DFAM-inspired sequencer card.
Switch up is play (tempo, length, VCO2 offset), switch middle is edit (step pitch and velocity),
and switch down supports short press cursor advance / long press play-pause.
Sequence data is randomized on reset and shared across panel, Monome Grid, 8mu, and browser editor workflows.
Pulse In 1 is external clock and Pulse In 2 resets to step 1.
<br>[Web editor](https://mohoyt.com/drumdrum.html) | 1.2.0<br>Functional but WIP | C++ (ComputerCard / Pico SDK) | Moses Hoyt |
| 34_dual_quant | Dual quantised granular pitch shifter with calibrated 1V/oct CV outputs | 1<br>Beta | C++ (ComputerCard / Pico SDK) | Adrian Vos |
| 35_FreqShift | Dual-input frequency shifter designed for feedback patching and experimental use.
Main sets shift amount (center = 0 Hz). Switch up uses a wide log-scaled bipolar range (±7000 Hz);
middle uses a narrower linear range (±440 Hz). X sets internal feedback amount and Y crossfades the audio inputs.
CV In 1 modulates shift (added to Main). CV In 2 blends the combined feedback path.
 | 1.1<br>Functional | C++ (ComputerCard) | Ben Regnier |
| 36_GradualProcess | A multi-process card: three independent generative composition processes live in one
firmware image, and which one runs is chosen by the Main knob's position at power-on
(or reset) — CCW third = Glass, middle third = Reich, CW third = Tintinnabuli. LEDs
briefly flash to confirm the selection (~0.5s) before it starts. The choice latches for
that session; to switch, set Main to a different zone and power-cycle/reset again.

All three processes share the same Z-switch convention (Up = Play, Middle = Stop, Down
held = Settings), the same 6-scale system (Major, Minor, Pentatonic Major, Pentatonic
Minor, Blues, Chromatic) on Y while Z is held, and the same 7-step clock divide/multiply
(/8 /4 /2 x1 x2 x4 x8) on X while Z is held — only the Play-mode controls and outputs
differ between them, described per-process below.
 | 1.0.0<br>Released | C++ (ComputerCard) | James Saunders |
| 37_compulidean | Drum-machine card built around Euclidean pattern generation and sample playback.
This release folder is a stub; code, full documentation, and UF2 downloads live in the external repository.
 | (see source repo)<br>Functional, but WIP | C++/Arduino, with vscode+platformio. | Tristan Rowley |
| 38_od | Simulates a Lorenz attractor to generate loopable CV and pulse signals.
This release folder is a stub; documentation and UF2 builds are maintained in the external repository.
 | 1.0<br>Released | MicroPython | M. John Mills |
| 39_knots | Six-engine oscillator card with Normal/Alt output modes, MIDI input, and clock/gate utilities.
Main sets pitch, X/Y shape engine parameters, CV and pulse inputs modulate mode/slot behavior,
and Z handles mode switching, engine advance, and Pulse Out 2 clock configuration.
 | 0.2<br>Released | C++ (RPi Pico SDK / ComputerCard) | Jeff Fletcher |
| 41_blackbird | Blackbird runs crow-compatible Lua scripts over USB serial and can also store scripts on card flash.
Panel control behavior is script-defined: Lua code reads `bb.knob.*`, `bb.switch.position`, and inputs,
then decides how outputs respond. LEDs always reflect positive output voltages on the six outputs.
<br>[Web editor](https://dessertplanet.github.io/web-druid/) | 1.1<br>Released | Pico SDK + Lua | Dune Desormeaux |
| 42_backyard_rain | Ambient backyard rain playback card with user-controlled intensity.
This release folder is a stub; docs and release UF2 files are maintained in the external repository.
 | (see source repo)<br>Released | Rust (Embassy framework) | Brian Dorsey |
| 433_sense_of_space | 433 Sense of Space is supplied as 2 MB and 16 MB builds. The 2 MB build uses a
compact 10 kHz stereo 8-bit ambience asset, while the 16 MB build uses a cleaner
24 kHz stereo 16-bit ambience asset.
Switch Up stops and arms the performance, Switch Middle starts three 91 second
loops sourced from 1:30-3:01 of the BBC recording, and the left column LEDs count
down from three loops to zero. Switch Down or Pulse In 1 triggers a short
chair/stool creak one-shot representing the musician shifting in their seat.
Pulse In 2 restarts the full performance from the beginning.
 | 0.2.1<br>stable | C++ (Pico SDK / ComputerCard) | Music Thing Modular / AI-assisted |
| 43_Castle_Process | Castle Process is a performance card built around chopped external audio, crude internal squarewave energy, aggressive switching between sources, and a separate bass pulse layer. It is designed as a playable sound-destruction tool rather than a clean effect or faithful clone. | 1.0<br>Release Candidate | C++ | Adrian Vos |
| 44_Birds | Two interacting birds are generated from a Turing-style looped random sequencer.
Main controls lock/change behavior, X shifts bird pitch, Y controls phrase speed,
Z selects normal/wild/reseed modes, and Pulse In 1 can replace the internal clock.
 | 0.5.0<br>Beta | C++ (Pico SDK / ComputerCard) | Tom Whitwell |
| 47_NZT | Grain-noise generator and noise utility card with density control, seed modulation, CV utilities,
and optional audio-rate excitation via Pulse In 1. Audio Out 1/2 provide complementary dense/sparse textures.
 | 1.0.0<br>Released | C++ (ComputerCard) | @kjnilsson |
| 48_two_tracks | A dual-read-head audio looper inspired by Steve Reich's phase music.
Record a loop and play it through two independent outputs with separately
controllable read positions and loop lengths, creating evolving phase
patterns and interference effects.

Recording uses a hands-free three-state machine: PLAY -> ARMED -> RECORD -> PLAY.
Z Down arms recording; a 3-second countdown with audible ticks begins; recording
starts automatically when the countdown expires; Z Down again stops recording.

Two play modes (Z switch selects after recording):
  Offset mode (Z Middle): each knob sets position offset within the loop.
  Phasing mode (Z Up): right loop is shorter than left, creating natural periodic
  phasing. Knob Y adds a speed offset on the right head.

Audio is stored in flash as IMA-ADPCM (4 bits per sample). Capacity depends on
the card: about 70 seconds on a 2 MB card, 11 minutes on a 16 MB card.
 | 1.1<br>Released | C++ (Pico SDK) | Joep Vermaat |
| 50_flux | Flux combines stereo effects, multiple synth engines, sample/granular playback, and utility generators
on one Workshop Computer card. Switch up is synth mode, middle is effect mode, down-hold selects
synth/effect, and double-down-hold enters performance controls. A web manager is used for CV/pulse routing,
MIDI mappings, sample management, and saving patch-card style configurations.
<br>[Web editor](https://vincentmaurer.de/flux/flux_manager.html) | 1.0<br>Released | C++ (RP2040 Pico SDK) | Vincent Maurer |
| 51_grains | Grains records and replays granular audio from a live buffer or stored sample slots. The interface is page-based:
page 1 (position/density/size), page 2 (envelope/tone/pitch), page 3 (mix/spread/feedback-diffusion), and page 4
(reverb). Flick/hold gestures control page changes, freeze, and load/save menus. Tape Mode is available at power-up
for scrub-style sample playback from the same slot system.
<br>[Web editor](https://vincentmaurer.de/grains/grains_manager.html) | 1.0<br>Released | C++ (RP2040 Pico SDK) | Vincent Maurer |
| 53_glitter | Glitter continuously plays a 2-second stereo loop while up to six grains read random snippets from it.
Switch up/down records into the loop (up for continuous record, down for punch-in style). Main blends
plain loop vs granulated output, X controls repitch behavior in play and overdub balance in record, and
Y sets maximum grain size. CV inputs influence grain repeat/sleep behavior and Pulse In 1 can quantize
grain length/position against a clock.
 | 0.1.2-beta<br>Beta | C++ (Pico SDK 2.1.1, UF2 release) | Steve Jones |
| 54_Tapegrade | Mono-input stereo cassette warble processor with wow, flutter, hiss, crackle, and tape wear morphing. | 0.8.0<br>WIP (functional) | C++ (RP2040 Pico SDK) | Adrian Vos |
| 55_fifths | Fifths quantizes incoming or generated CV to keys arranged around the circle of fifths and outputs both
a quantized note and an ambiguous third harmony voice. It can run from external clock (Pulse In 1) or internal
tap-tempo clock, and can loop or write incoming material depending on switch state/toggles. Loop length is set by X,
key center is selected by Main (or CV In 2), and CV In 1 transposes up to two octaves. Pulse outputs provide
an internal clock pulse plus a probabilistic sequence pulse stream.
 | 1.1<br>Released | C++ (RP2040 Pico SDK) | Dune Desormeaux |
| 56_Krell | Krell outputs two independent channels of random pitch CV and looping AD envelopes, each with pulse outputs
at end of cycle. Main sets overall tendency toward longer/shorter envelopes, X sets left-channel envelope timing,
and Y sets right-channel timing. Switch up quantizes pitch to octaves, middle uses chromatic quantization,
and switch down cycles note range between roughly one and three octaves.
 | 1.0<br>Mostly complete | Blackbird Lua | Benjamin Reily |
| 57_glitch | Glitch records into a shared 2.33-second circular buffer and runs in two power-on modes. Glitch mode loops
ratcheted sub-slices with optional reverse, decimation, and bitcrush; Stutter mode slices beats, shuffles order,
and can repeat slices before advancing. Pulse In 1 sets beat timing, CV In 1 freezes recording while playback
continues, and Audio Out 2 stays dry for parallel routing.
 | 1.4.0<br>Released | C++ (RP2040 Pico SDK) | Andy Jenkinson (uglifruit) |
| 58_LoChoVibes | Stereo chorus and vibrato effect featuring triangle, sine, and slow drift LFO modes, modulation-based delay movement, and tape-style saturation. | 0.1.0<br>released | C++ | Adrian Vos |
| 59_BitPhase | Resonant 4-stage phaser with wide modulation sweeps, tremolo blending, and Burst-mode degradation | 0.1.0<br>Beta | C++ (Pico SDK) | Adrian Vos |
| 60_markov | Dual Markov-chain generator with melody on voice A and rhythm/percussion on voice B.
Knob X selects melodic transition profile, Knob Y selects percussion profile, and
Main is switch-dependent: transpose (Z up), loop lock/mutate length (Z middle), or
scale select while held (Z down). Pulse In 1 clocks both chains; CV Out 1 carries
melody pitch, Pulse Out 1 emits pitch-change gate, Pulse Out 2 emits percussion
triggers/ratchets/flams, and CV Out 2 carries percussion accent.
 | 1.0.0<br>Released | C++ (Pico SDK) | Andy Jenkinson (uglifruit) |
| 64_voices_of_sid | Two emulated MOS 6581 SID chips run in parallel on separate audio outputs.
Switch middle is Play mode (CV/gate, Main = decay/release, X = resonance, Y = pulse width).
Switch up is Sound Edit (X/Y select waveforms for each voice; gates open for drone/tuning).
Hold switch down to randomize attack, sustain, waveforms, ring mod, and sync.
CV and pulse inputs pass through to outputs for chaining modules.
 | 1.1<br>Released | C++ (Pico SDK) | Joep Vermaat |
| 66_stretchcore | Mono sample-loop player with browser-loaded sample bank, tempo control, timestretch,
and jump/selection gestures. Main knob targets playback position or sample index
depending on switch gesture, X sets internal tempo when not externally clocked, and
Y sets timestretch amount (with optional CV1 modulation). Switch down jumps to Main
position and emits Pulse Out 1; switch up selects sample from Main position and emits
Pulse Out 2. Pulse In 1 provides external clock, and Pulse In 2 triggers CV2-position
jumps (or loop start when CV2 is unpatched).
<br>[Web editor](https://infinitedigits.co/stretchcore/) | 1.0<br>Ready% | C++ (Pico SDK) | Infinite Digits |
| 67_Fragments | Six-slot audio recorder and clocked fragment sequencer with browser librarian, MIDI pitch control, random CV outputs, and an alternate long-sample variation mode. | 1.0<br>Released | C++ (RPi Pico SDK) | Max Harnishfeger |
| 69_trace | Oscillograph-focused dual-output oscillator bank for X/Y scope visuals. Main controls
pitch (summed with CVIn1). X and Y control oscillator-specific modulation offsets in
normal mode; with switch up they become attenuation controls for AudioIn1/AudioIn2
modulation depth. Switch down advances oscillator selection, while pulse inputs can
cycle bank/index externally.
 | 0.1<br>Functional but WIP | C++ (ComputerCard) | Ruiyang Wang |
| 71_degenerator | Irreversible layered looper with additive MIX and subtractive DEGRADE workflows.
Z down is RECORD (and slot workflows in SLOT mode), Z middle is MIX, Z up is
DEGRADE. Main controls mix/commit amount (full left freezes state), X selects
harmonic effect zone, Y selects destructive effect zone. Boot with Z down enables
SLOT mode with flash save/load features; otherwise YOLO mode is instant-on.
<br>[Web editor](https://degenerator-web.netlify.app/) | 1.3<br>Released | C++ (Pico SDK) | Joep Vermaat |
| 72_motorik | Generative motorik drum machine with synchronized two-voice bass CV/gate outputs.
Z middle is main drum-performance mode (energy/transpose/fill), Z down is tilt and
bass pattern shift, Z up is texture/decimation and variation/humanize. Pulse In 1
clocks the engine externally; otherwise a fixed internal 120 BPM engine runs. Pulse
In 2 triggers one-bar fills. AudioOut1/2 carry drum mix, while CV/Pulse outputs carry
root and mirrored (+12) bassline voices.
 | 1.0<br>Released | C++ (Pico SDK) | Joep Vermaat |
| 73_VSS | 6-voice polyphonic sampler inspired by the Yamaha VSS-30. Record any audio source at 24 kHz µ-law
(up to 6 s, 6 independent flash-saved banks), then play it back chromatically over USB MIDI with
looping, ADSR envelope shaping, per-voice vibrato, a tape-style echo, an arpeggiator, and a built-in
tuner.
 | 1.0.0<br>Released | C++ (ComputerCard) | @kjnilsson |
| 74_Wild_Pebble | MIDI-clockable generative rhythm and melody organism inspired by Pet Rock | 1<br>Beta. USB MIDI clock input and sequencer note output tested | C++ | Adrian Vos with Vibecode support |
| 75_Turing_Clouds | Turing Machine-driven granular texture generator and rhythmic delay for the Workshop Computer | 1.0<br>Released | C++ (Pico SDK) | Ainews |
| 76_hot_fuzz | Hot Fuzz is a stereo fuzz/wah effects card. It combines a clipper (soft/hard/asymmetric/foldback)
with a resonant state-variable filter for the wah sweep, and supports both manual
and auto-wah modes.

Switch Up:   Fuzz + wah. Main pot selects fuzz type (4 zones: soft/hard/asym/fold,
             CCW to CW). X pot = fuzz drive.
Switch Mid:  Fuzz + wah blend. Main pot = dry/wet blend (CCW=dry, CW=full fuzz).
             X pot = drive.
Switch Down: Wah mode. Main pot = manual wah sweep (auto-wah off) or
             base frequency (auto-wah on). Toggle auto-wah by double-tapping
             Down (Mid→Down→Mid→Down within 0.5 s) or by sending a rising
             edge to Pulse2. LED 4 flashes to confirm.
             X pot = dry/wet blend.

Y pot:       Wah Q / resonance (0 = flat, 4095 = high resonance). Same in all modes.

Pulse1:      Rising edge toggles fuzz on/off (latched). Acts in Up/Mid modes
             only. When bypassed the wah still runs on the clean signal;
             LED 0 goes dark and flashes on toggle.
Pulse2:      Rising edge toggles auto-wah on/off (latched, any mode). Same
             effect as the double-tap Down gesture, but patchable from a
             gate or footswitch.

CV1 (optional): overrides wah cutoff when no pot controls it (Up/Mid modes).
CV2 (optional): adds to the X pot drive value.

Settings are not persisted — fuzz type, auto-wah state, and all settings
reset to defaults on power cycle.
 | 1.0<br>Released | C++ (ComputerCard) | Joep Vermaat |
| 77_Placeholder | Reserved for secret project | 0.0<br>None | None | None |
| 78_Talker | Early proof-of-concept LPC speech card that babbles randomized words. Z switch sets continuous,
off, or single-word behavior. Main + CVIn1 set pitch (with X as attenuverter), while Y + CVIn2
set speaking speed. AudioOut1 is speech, AudioOut2 exposes the LPC exciter components.
 | 0.1<br>Proof of concept | C++ (ComputerCard) | Chris Johnson |
| 81_West_Coast_LPG | Dual vactrol-emulating low-pass gate (combined VCA + low-pass filter) with fast-attack/slow-decay 'plong', self-pinging percussion, and per-channel VCA/VCF/LPG modes. | 0.1<br>Working | C++ (ComputerCard) | Jason Moore |
| 82_Computer_Grids | Grids-inspired trigger sequencer for the Workshop Computer. Control pattern map and fill with knobs,
or per-lane density when switch Z is **up**. Internal clock with swing, or follow external clock on **PulseIn1**.
Configure and save settings via the Web MIDI SysEx editor (Chrome recommended).
<br>[Web editor](https://tomwhitwell.github.io/Workshop_Computer/programs/82-computer-grids/web/index.html) | 0.1.0<br>Released | C++ | Phil Miller |
| 83_Origami | Dual oversampled wavefolder — triangle / sine / hard-clip folding with bias (even-harmonic) control and CV over fold depth, band-limited via 4x oversampling. | 0.1<br>Working | C++ (ComputerCard) | Jason Moore |
| 84_CosmikC1zzl3 | Stable phase-distortion synthesiser and Turing machine firmware with Web MIDI envelope readback, PD, detune, eight waveform families, hosted CZ patch import, USB MIDI device/host operation, and optional Turing MIDI output.<br>[Web editor](https://tomwhitwell.github.io/Workshop_Computer/programs/84-cosmikc1zzl3/web/index.html) | 1.1<br>Released | C++ (Pico SDK) | Adrian Vos |
| 86_tesserae | Variable-voice arpeggiated harmonic generator with 5 patterns and 10 scales. X sets melody
position, Y sets chord spacing, and Main changes by Z mode (pattern select, root note, or held
scale/voice selection). Supports tap tempo, external clock/reset, and dual transpose inputs.
 | 1.0<br>released | C++ (Pico SDK) | Joep Vermaat |
| 87_fr330hfr33 | Performance-focused acid voice with diode filtering, distortion, MIDI, and a persistent sequencer<br>[Web editor](https://tomwhitwell.github.io/Workshop_Computer/programs/87-fr330hfr33/web/index.html) | 0.9.3<br>Stable | Pico SDK | Adrian Vos |
| 88_Blank | Reserved for blank 88 cards | 0<br>None | None | Tom Whitwell |
| 90_Pantograph | Pantograph lets you trace a performance on the X and Y knobs (e.g. oscillator pitch + filter
cutoff), record it, then loop it back out the two CV outputs at variable speeds.

Switch UP auditions live; hold DOWN to record (monitored, so you hear it as you play);
release to MIDDLE and it loops exactly that length. The Main
knob sets playback speed: centre = freeze, left = reverse, right = faster.

Patch a gate into PULSE IN 1 and MIDDLE becomes a triggered envelope; unpatched it free-loops.
PULSE OUT 1 fires a trigger at the end of a recording.
PULSE OUT 2 runs a gate from the shape of the recording.
 | 1.1<br>Ready | Pico SDK | Kenny Shen |
| 91_chorgan | Six-voice morphing chord synthesizer with built-in chord sequencer.
Knob X and CV In 1 set root pitch; Knob Y selects interval above root (0–12 semitones).
Main knob morphs all six voices through sine, triangle, saw, and narrow pulse; CV In 2 offsets timbre.
Audio In 1 controls slew speed (0V=instant, +5V=approx 1 min glide). Audio In 2 shifts chord voicing through octave inversions (bipolar, +/-6 steps).
Tap Switch Down to cycle chord extension presets; hold one second to store a chord in the sequencer.
Pulse In 1 advances preset; Pulse In 2 recalls the next stored chord on rising edges.
Boot with Switch Down held for slew mode (portamento chord changes instead of detune).
 | 1.1.0<br>released | C++ (Pico SDK / ComputerCard) | Andy Jenkinson (uglifruit) |
| 93_Turing_Matrix | Turing Machine sequencer with a switchable mixer layer inspired by the Music Thing Modular Turing Machine and Vactrol Mix combination<br>[Web editor](https://tomwhitwell.github.io/Workshop_Computer/programs/93-turing-matrix/web/index.html) | 0.1.0-beta<br>Beta release candidate | C++ (ComputerCard) | Adrian Vos from initial code by Tom Whitwell / Music Thing Modular / Chris Johnson |
| 95_offair2 | Tune across a virtual shortwave band with the Main knob. Two Stations (live audio
inputs, or baked recordings in baked-in audio mode) plus three interference signals
are scattered across the dial and re-randomised on each band change. Approaching a
Station you hear a sliding heterodyne whistle, the audio pulls into tune and the
static ducks away; off-tune it pitch-shifts (SW/LW) or distorts (AM). Knob X sets IF
bandwidth/brightness, Knob Y the noise level. Tap Switch Down to cycle AM/SW/LW.
Hold Switch Up for dead-air (baked-in mode) or a Pulse In 1-keyed morse Station 2
(audio-input mode). Pulse In 2 fires curated "Insta-ference" one-shots.
 | 1.0.1<br>released | C++ (Pico SDK / ComputerCard) | Andy Jenkinson (uglifruit) |
| 96_cathode | Cathode Ray turns the Workshop Computer into a 1-bit composite video synthesiser.
Wire a two-resistor DAC from Pulse Out 1 (1kΩ) and Pulse Out 2 (220Ω) into an RCA
centre pin (grounds common) and connect to any composite-input TV/monitor. PAL by
default; flash cathode_ray_ntsc.uf2 for NTSC (US) displays.

The Main knob picks the mode across three zones: lower = etch-a-sketch (draw with
CV In 1/2 as X/Y), middle = oscilloscope (Audio In 1 traces, sweep speed slow→fast),
upper = spectrum analyser (24-band, Audio In 1). Greyscale (5 levels) is produced by a
2×2 spatial dither into the 1-bit picture.

Switch MIDDLE = phosphor fade / persistence, UP = static / clean. Knob X/Y are
multi-function (CV scale / position offset / scope gain & baseline / spectrum gain &
rotate) with pickup hysteresis.

Three independent performance-trigger sources — Switch DOWN, Pulse In 1, Pulse In 2 —
each runs a configurable behaviour (invert, clear, cycle/random effect, or one fixed
effect: strobe / fade / fade-to-white / snow / swap / corrupt / roll). Hold Switch
DOWN and twist Main/X/Y to open the on-screen config menu and assign each source.

Hold Switch DOWN at power-on for ALT-BOOT mode: a selector of screensaver / performance
hybrids that also protect a CRT from burn-in. Switch UP shows the selector (Main knob
scrolls through the modes, with on-screen input/output help); Switch MIDDLE or DOWN
plays the shown mode. Modes:
 • COMET — a round comet with a phosphor tail (Main/CV1 = speed, Y/CV2 = tail length).
 • PATCHTEROIDS — patch-controlled Asteroids. Main (+ CV In 1) steers, PU1 / Switch DOWN
   fire. CV Out 1 = pitch (+1 semitone per hit, arpeggiates down on a crash), CV Out 2 =
   gate per hit.
 • BOING — Amiga-style tilted rotating checkered ball. Knob X / CV1 = spin & kick impulse,
   Main / CV2 = bounce efficiency, Knob Y = horizontal speed, PU1 / DOWN = kick.
   CV Out 1 = height, CV Out 2 = trigger on each floor bounce.
 • STARFIELD — fly through space. Main = speed, X/CV1 = horizontal turn, Y/CV2 = vertical.
 • RADAR — a radar scope. Main aims a turret, PU1 / DOWN fires a ballistic missile (hold =
   range); PU2 (CV1 sets radius) drops fading target blocks. CV Out 2 = pulse on a hit.
 • LUNAR — Lunar Lander. Main / CV1 rotate, PU1 / DOWN thrust; land gently on the pad
   (narrows each stage) avoiding drifting UFOs. CV Out 1 = altitude, CV Out 2 = crash.
 • 3DMAZE — first-person wireframe maze. Main turns, PU1 / DOWN walks; Knob X / CV1 =
   hands-free auto-run. Find the glowing white EXIT panel on a wall (reaching it flashes and
   generates a new maze). CV Out 2 = pulse when you reach the exit.
 • FOURTRIG — a trigger-driven visual drum machine. Audio In 1, Audio In 2, Pulse In 1 and
   Pulse In 2 are all triggers; each stamps a decaying "thing" (thick 2×2 strokes) into one
   screen quadrant. Knob X = bank (icons first, words last: shapes / music-hits / symbols /
   words / emphasis), Knob Y (+ CV In 2) = the set of four, Main (+ CV In 1) = CHAOS (jitter/
   swap placement, grow + vary size, and past ~50% a rising chance of a per-hit random VFX from
   the full set incl. flip-180). Hold Switch DOWN for a momentary held VFX. CV Out 2 = pulse on
   every trigger.
 | 1.2.0<br>Released | C++ (RP2040 Pico SDK) | Andy Jenkinson (uglifruit) |
| 97_alloy | Two audio inputs fused through 15 crossfaded cross-modulation algorithms - ring mods, wavefolder, comparators, bitcrusher, frequency shifter, delay, binaural doppler, and a 20-band vocoder - with a clocked Turing machine driving pitch, CV, and gates underneath.
 | 0.2.0<br>Beta | C++ (RP2040 Pico SDK) | Eric Gao |
| 98_duo_midi | Duophonic MIDI-to-CV card running a Blackbird Lua script. Z up is duophonic voice
allocation, Z middle is mirrored mono, and Z down toggles pulse outputs between trigger and gate
mode. Main sets velocity sensitivity; X and Y set envelope attack and release. Receives MIDI note,
velocity, pitch bend, and mod wheel.
 | 0.1<br>Released | Lua / Blackbird | Dune Desormeaux |
| 99_toolbox | Utility-focused card inspired by the 0-Coast utility section. Includes a mixer/VCA with
attenuverter, secondary VCA, sample-and-hold, clock generation with probability, and selectable
noise types.
 | 0.1.1<br>Released | C++ (ComputerCard) | divmod |
