# Releases  
| Folder Name | Description | Version | Language | Creator |
| ----------- | ----------- | ------- | -------- | ------- |
| 00_Simple_MIDI | Takes USB midi, sends it to pulse and CV outputs, also sends knob positions and CV inputs back to the computer as CC values. | 0.6.6<br>Working but simple | Arduino-Pico | Tom Whitwell |
| 02_comingsoon | Reserved for upcoming project | 0.0<br>None | None | None |
| 03_Turing_Machine | Turing Machine with tap tempo clock, 2 x pulse outputs, 4 x CV outputs<br>[Web editor](https://www.musicthing.co.uk/web_config/turing.html) | 1.5.3<br>Working but Simple | C++ (ComputerCard) | Tom Whitwell |
| 04_BYO_Benjolin | Rungler, Chaotic VCO, Noise Source, Turing Machine, Quantizer | 1.1<br>Released | Pico SDK | Dune Desormeaux |
| 05_chord_blimey | Generates CV/Pulse arpeggios | 0.9<br>Mostly complete (for now) | C (RPi Pico SDK) | Tom Waters |
| 06_usb_audio | 6-Channel USB Audio & MIDI firmware with CV/Gate support<br>[Web editor](https://vincentmaurer.de/usb-audio/midi_config.html) | 1.0<br>Release | C++ (RPi Pico SDK) | Vincent Maurer (vincentmaurer.de) |
| 07_bumpers | 'Bouncing ball' style delay and trigger generators | 1.0<br>Released | C++ (ComputerCard) | Chris Johnson |
| 08_bytebeat | Generates and mangles bytebeats | 0.1<br>Functional but WIP | C++/Arduino-Pico | Matt Kuebrich |
| 09_DivCom | Comparator and VC clock divider, inspired by Serge NCOM | 0.1<br>Released | C++ (RPi Pico SDK) compat. cmake. | divmod |
| 10_twists | A port of Mutable Instruments Braids with a web editor | 0.1<br>Functional but WIP | C (RPi Pico SDK) | Random Works |
| 11_goldfish | Weird delay/looper for audio and CV | 2.0<br>Released | Pico SDK | Dune Desormeaux |
| 12_am_coupler | AM radio transmitter / coupler | 1.0<br>Released | C++ (ComputerCard) | Chris Johnson |
| 13_noisebox | 13-algorithm noise synth with CV modulation, sample-and-hold, and crusher mode | 1.0<br>Released | C++ (ComputerCard) | Eric Gao |
| 14_cvmod | Quad CV delay inspired by Make Noise Multimod | 1.0<br>Released | C++ (ComputerCard) | Chris Johnson |
| 15_MLRws | A remix of monome's classic MLR sample cutting platform (grid controller encouraged but optional). Diagrams here are for Gridless mode.<br>[Web editor](https://dessertplanet.github.io/MLRws-web/) | 1.1.5<br>Released | Pico SDK | Dune Desormeaux |
| 16_the_bells | Bellringing methods sequencer — 12 change-ringing methods with tempo and jitter, quantised to six scales | 1.0.0<br>Released | C++ (ComputerCard) | James Saunders |
| 18_chord_organ | Chord Organ-ish - 16 chords, 8 voices, 1V/oct root. Inspired by Music Thing Chord Organ. | 0.1<br>Working | Pico SDK (C++), ComputerCard | jkeyworth |
| 20_reverb | Reverb effect, plus pulse/CV generators and MIDI-to-CV, configurable using web interface.<br>[Web editor](https://www.musicthing.co.uk/web_config/reverb.html) | 1.5<br>Released | C (RPi Pico SDK) | Chris Johnson |
| 21_resonator | Karplus-Strong based sympathetic resonator. Can be used for resonant droning as well as plucking sounds.<br>[Web editor](https://johaneklund.io/resonator) | 1.1.1<br>Released | C++ (ComputerCard) | Johan Eklund |
| 22_sheep | A time-stretching and pitch-shifting granular processor and digital degradation playground with 2 fidelity options. | 1.2<br>Released | Pico SDK | Dune Desormeaux |
| 23_SlowMod | Chaotic quad-LFO with VCAs | 0.1<br>Released | C++ (RPi Pico SDK) compat. w/ cmake and Arduino IDE. | divmod |
| 24_crafted_volts | Manually set control voltages (CV) with the input knobs and switch. It also attenuverts (attenuates and inverts) incoming voltages. | (see source repo)<br>Released | Rust (Embassy framework) | Brian Dorsey |
| 25_utility_pair | 25 small utilities, which can be combined in pairs | 1.0<br>Released | C++ (ComputerCard) | Chris Johnson |
| 26_clockwork | 6-channel polyrhythmic clock, gate, and LFO/envelope generator inspired by Pamela's Workout.<br>[Web editor](https://vincentmaurer.de/clockwork/index.html) | 1.0.0<br>Released | C++ (RP2040 Pico SDK) | Vincent Maurer |
| 27_Siren | Multi-algorithm drone oscillator. Inspired by the Forge TME Vhikk X. | 0.2<br>Functional but WIP | C++ (ComputerCard / Pico SDK) | Moses Hoyt |
| 28_eighties_bass | Bass-oriented complete monosynth voice consisting of five detuned saw wave oscillators with mixable white noise and adjustable resonant filter. | 0.1<br>Functional but WIP | Arduino (arduino-pico) with Mozzi 2 | Tod Kurt (@todbot) |
| 30_cirpy_wavetable | Wavetable oscillator that using wavetables from Plaits, Braids, and Microwave, | 0.1<br>Functional but WIP | CircuitPython | Tod Kurt (@todbot) |
| 31_esp | A MS-20-style External Signal Processor that includes a preamp, bandpass filter, envelope follower, gate, and 1v/oct pitch outs. | 1.0<br>Released | C++ (ComputerCard) | Ben Regnier |
| 32_vink | Dual delay loops with sigmoid saturation for Jaap Vink / Roland Kayn style feedback patching | 1.1<br>Functional | C++ (ComputerCard) | Ben Regnier |
| 33_drumdrum | DFAM-style 8-step sequencer<br>[Web editor](https://mohoyt.com/drumdrum.html) | 1.2.0<br>Functional but WIP | C++ (ComputerCard / Pico SDK) | Moses Hoyt |
| 34_dual_quant | Dual quantised granular pitch shifter with calibrated 1V/oct CV outputs | 1<br>Beta | C++ (ComputerCard / Pico SDK) | Adrian Vos |
| 35_FreqShift | Dual Input Frequency Shifter for Feedback Experimentation | 1.1<br>Functional | C++ (ComputerCard) | Ben Regnier |
| 36_GradualProcess | Three generative composition processes (Glass, Reich, Tintinnabuli) in one card, chosen by Main's position at power-on | 1.0.0<br>Released | C++ (ComputerCard) | James Saunders |
| 37_compulidean | Generative Euclidean drum + sample player. | (see source repo)<br>Functional, but WIP | C++/Arduino, with vscode+platformio. | Tristan Rowley |
| 38_od | Loopable chaotic Lorenz attractor trajectories and zero-crossings as CV and pulses, with sensitivity to initial conditions. | 1.0<br>Released | MicroPython | M. John Mills |
| 39_knots | Six-engine oscillator firmware for the Music Thing Workshop System | 0.2<br>Released | C++ (RPi Pico SDK / ComputerCard) | Jeff Fletcher |
| 41_blackbird | A scriptable, live-codable, USB-serial-to-CV device implementing monome crow's protocol<br>[Web editor](https://dessertplanet.github.io/web-druid/) | 1.1<br>Released | Pico SDK + Lua | Dune Desormeaux |
| 42_backyard_rain | Nature soundscape audio. A cozy rain ambience mix for background listening. You control the intensity. This card plays rain ambience which was recorded in my backyard. | (see source repo)<br>Released | Rust (Embassy framework) | Brian Dorsey |
| 43_Castle_Process | Fort Processor-inspired harsh noise processor with chopped external audio and a bass pulse voice | 1.0<br>Release Candidate | C++ | Adrian Vos |
| 43_clockwork | 6-channel polyrhythmic clock, gate, and LFO/envelope generator inspired by Pamela's Workout.<br>[Web editor](https://vincentmaurer.de/clockwork/index.html) | 1.0.0<br>Released | C++ (RP2040 Pico SDK) | Vincent Maurer |
| 44_Birds | Two birds sing to each other controlled by a Turing-style shift register sequencer with clock in and CV/pulse out. | 0.5.0<br>Beta | C++ (Pico SDK / ComputerCard) | Tom Whitwell |
| 47_NZT | Grain Noise and Noise Tools | 1.0.0<br>Released | C++ (ComputerCard) | @kjnilsson |
| 50_flux | Effects, Synthesizer and Utility<br>[Web editor](https://vincentmaurer.de/flux/flux_manager.html) | 1.0<br>Released | C++ (RP2040 Pico SDK) | Vincent Maurer |
| 51_grains | Granular Sampler and Effect<br>[Web editor](https://vincentmaurer.de/grains/grains_manager.html) | 1.0<br>Released | C++ (RP2040 Pico SDK) | Vincent Maurer |
| 53_glitter | Granular Looping Sampler | 0.1.2-beta<br>Beta | C++ (Pico SDK 2.1.1, UF2 release) | Steve Jones |
| 54_Tapegrade | Mono-input stereo cassette warble processor with wow, flutter, hiss, crackle, and tape wear morphing. | 0.8.0<br>WIP (functional) | C++ (RP2040 Pico SDK) | Adrian Vos |
| 55_fifths | A quantizer/sequencer that can create harmony and nimbly traverse the circle of fifths in attempts to make jazz | 1.1<br>Released | C++ (RP2040 Pico SDK) | Dune Desormeaux |
| 56_Krell | Krell | 1.0<br>Mostly complete | Blackbird Lua | Benjamin Reily |
| 57_glitch | Clock-synced beat-repeater with ratcheting, reversal and audio degradation | 1.4.0<br>Released | C++ (RP2040 Pico SDK) | Andy Jenkinson (uglifruit) |
| 58_LoChoVibes | Stereo chorus and vibrato effect featuring triangle, sine, and slow drift LFO modes, modulation-based delay movement, and tape-style saturation. | 0.1.0<br>released | C++ | Adrian Vos |
| 59_BitPhase | Resonant 4-stage phaser for the Workshop Computer with deep notch filtering, wide modulation sweeps, controllable resonance, tremolo blending, and digital Burst-mode degradation. | 0.1.0<br>Beta | C++ (Pico SDK) | Adrian Vos |
| 60_markov | Dual generative Markov chain module — evolving melody (MarkoV) left side, rhythmic percussion patterns (MarkovPerc) right side, with internal synth voice | 1.0.0<br>Released | C++ (Pico SDK) | Andy Jenkinson (uglifruit) |
| 64_voices_of_sid | Dual MOS 6581 SID emulation (reSID engine) with CV/gate control, stereo output, waveform selection, and randomize | 1.1<br>Released | C++ (Pico SDK) | Joep Vermaat |
| 66_stretchcore | A card for playing and manipulating samples with tempo control, timestretch with browser-based audio loading (infinitedigits.com/stretchcore/)<br>[Web editor](https://infinitedigits.co/stretchcore/) | 1.0<br>Ready% | C++ (Pico SDK) | Infinite Digits |
| 69_trace | Oscillograph stereo oscillator | 0.1<br>Functional but WIP | C++ (ComputerCard) | Ruiyang Wang |
| 71_degenerator | Degenerator — Disintegrating Looper. Capture audio loops and apply irreversible degradation with 6 algorithms (Saturation, Filter Drift, Tape Hiss, Oxide Shedding, Bit Crush, Bit Rot) via preview/apply workflow. Inspired by William Basinski's The Disintegration Loops.<br>[Web editor](https://degenerator-web.netlify.app/) | 1.3<br>Released | C++ (Pico SDK) | Joep Vermaat |
| 72_motorik | Motorik drum machine — kick/snare/hihat with bass and melody CV, classic Krautrock grooves | 1.0<br>Released | C++ (Pico SDK) | Joep Vermaat |
| 74_Wild_Pebble | MIDI-clockable generative rhythm and melody organism inspired by Pet Rock | 1<br>Beta. USB MIDI clock input and sequencer note output tested | C++ | Adrian Vos with Vibecode support |
| 76_hot_fuzz | A stereo fuzz/distortion + resonant wah effects processor with manual and auto-wah modes, built on fixed-point DSP. | 1.0<br>Released | C++ (ComputerCard) | Joep Vermaat |
| 77_Placeholder | Reserved for secret project | 0.0<br>None | None | None |
| 78_Talker | Proof of concept speech synthesizer, based on TalkiePCM, inspired by 1970s LPC speech synths. | 0.1<br>Proof of concept | C++ (ComputerCard) | Chris Johnson |
| 81_West_Coast_LPG | Dual vactrol-emulating low-pass gate (combined VCA + low-pass filter) with fast-attack/slow-decay 'plong', self-pinging percussion, and per-channel VCA/VCF/LPG modes. | 0.1<br>Working | C++ (ComputerCard) | Jason Moore |
| 82_Computer_Grids | Grids-inspired trigger sequencer with Web MIDI SysEx configuration.<br>[Web editor](https://tomwhitwell.github.io/Workshop_Computer/programs/82-computer-grids/web/index.html) | 0.1.0<br>Released | C++ | Phil Miller |
| 83_Origami | Dual oversampled wavefolder — triangle / sine / hard-clip folding with bias (even-harmonic) control and CV over fold depth, band-limited via 4x oversampling. | 0.1<br>Working | C++ (ComputerCard) | Jason Moore |
| 84_CosmikC1zzl3 | Stable phase-distortion synthesiser and Turing machine firmware with Web MIDI envelope readback, PD, detune, eight waveform families, hosted CZ patch import, USB MIDI device/host operation, and optional Turing MIDI output.<br>[Web editor](https://soveda.github.io/CozmikC1zzl3/web-midi/editor/) | 1.1<br>Released | C++ (Pico SDK) | Adrian Vos |
| 86_tesserae | Tesserae — Variable-voice (2-8) arpeggiated chord generator with 5 patterns, 10 scales, tap tempo, CV/audio transpose inputs, and dual CV + audio pitch outputs. Inspired by Laurie Spiegel's Music Mouse and Patchwork. | 1.0<br>released | C++ (Pico SDK) | Joep Vermaat |
| 87_fr330hfr33 | Hardware-tested acid bass synthesiser with selectable saw or square oscillator, switchable 18 or 24 dB diode-style filtering, accent and glide, distortion, USB MIDI device/host operation, and a persistent editable sequencer.<br>[Web editor](https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/87_fr330hfr33/web_config/Fr330hfr33.html) | 0.9.3<br>Stable | Pico SDK | Adrian Vos |
| 88_Blank | Reserved for blank 88 cards | 0<br>None | None | Tom Whitwell |
| 90_Pantograph | Trace and record CV — record knob movements, loop them at bipolar speed | 1.0<br>Ready | Pico SDK | Kenny Shen |
| 91_chorgan | Chorgan — 6-voice chord synthesizer with morphing timbre, chord extension presets, and built-in chord sequencer. Two modes: normal (detune/chorus) and slew (portamento chord changes). Inspired by the Music Thing Modular Chord Organ. | 1.1.0<br>released | C++ (Pico SDK / ComputerCard) | Andy Jenkinson (uglifruit) |
| 93_Turing_Matrix | Turing Machine sequencer with a switchable mixer layer inspired by the Music Thing Modular Turing Machine and Vactrol Mix combination<br>[Web editor](https://soveda.github.io/Turing_Matrix_Editor/web) | 0.1.0-beta<br>Beta release candidate | C++ (ComputerCard) | Adrian Vos from initial code by Tom Whitwell / Music Thing Modular / Chris Johnson |
| 95_offair2 | OffAir — AM/Shortwave/Longwave radio simulator. Tune between two Stations and interference with authentic heterodyne whistles, SSB pitch-shift detuning, AM envelope detection, swelling per-band static, and triggerable Insta-ference one-shots. Baked recordings or live audio inputs become the Stations. | 1.0.1<br>released | C++ (Pico SDK / ComputerCard) | Andy Jenkinson (uglifruit) |
| 96_cathode | Composite video synthesiser (PAL + NTSC builds) — oscilloscope, etch-a-sketch & spectrum analyser, greyscale via dithering, performance effects, 8 alt-boot screensaver/game modes | 1.2.0<br>Released | C++ (RP2040 Pico SDK) | Andy Jenkinson (uglifruit) |
| 98_duo_midi | A duophonic midi device/host interface | 0.1<br>Released | Lua / Blackbird | Dune Desormeaux |
| 99_toolbox | Mixer, VCA, noise, S&H, clock generator, etc. | 0.1.1<br>Released | C++ (ComputerCard) | divmod |
