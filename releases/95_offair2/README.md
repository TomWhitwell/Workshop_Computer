# OffAir

AM/Shortwave/Longwave radio simulator for the Workshop Computer.

OffAir lets you tune across a virtual band the way you tune a real shortwave
receiver. As you approach a station you hear the signature heterodyne whistle slide
in pitch, the audio pull into tune, and the background static duck away. Tune past it
and the audio garbles, the whistle rises again, and it fades back into the noise.

Simulating the modulating and demodulating an RF carrier, OffAir gives the sound of detuning each station directly from how far off-tune you are:

- a **heterodyne whistle** whose pitch slides with detune — the signature SW sound;
- on **SW/LW**, the audio is **single-sideband frequency-shifted** by the detune
  amount, so harmonics break and voices go "wrong pitch" / metallic — exactly the
  near-tuned SSB sound (this is genuine product-detector behaviour: a detuned local
  oscillator shifts the recovered audio);
- on **AM**, the audio stays at correct pitch (envelope detection) but distorts as
  you detune;
- the station fades out as you move off, and at zero-beat you get clean audio with no
  whistle.

There are two tunable **Stations** (Station 1 and Station 2 — fed by the live audio
inputs, or by baked recordings in baked-in audio mode) plus three **interference**
signals (morse / data / numbers). They're scattered across the dial and
**re-randomised on every band change** — so the layout is different each time and a
Station and an interferer sometimes overlap, like a crowded band. A separate bank of
short one-shot events ("**Insta-ference**") can be fired in on demand.

See `offair_overlay.jpg` for the panel layout.

## Bands

Tap the switch **Down** to cycle **AM → SW → LW**. The band sets both the noise
character and the demodulation behaviour:

| Band | Demod near a station | Noise character |
|------|----------------------|-----------------|
| AM | Correct-pitch audio, distorts off-tune (envelope detection) | Mellow mid hiss, steady |
| SW | Directional pitch-shift (SSB) | Bright, busy, crackly wash that swishes |
| LW | Directional pitch-shift (SSB) | Deep slow rumble, barely swishes, heavy fade |

The dial layout (Station and interference positions, and which clips play) is
re-randomised each time you change band.

## Inputs

| Jack | Panel | Function |
|------|-------|----------|
| Audio In 1 | Station 1 In | Station 1 source (live audio, in audio-input mode) |
| Audio In 2 | Station 2 In | Station 2 source (live audio, in audio-input mode) |
| CV In 1 | Tuner | Tuning offset (1:1, added to the Main knob; ±5V ≈ ±half the dial). Patch an LFO/sequencer to scan, or CV Out 2 to home on Station 1 |
| CV In 2 | Noise | Noise level — adds to Knob Y (voltage-controlled static) |
| Pulse In 1 | Shuffle Signals | Rising edge — re-randomise the Station / interference layout. (In audio-input mode with Switch Up it becomes the Morse In key instead — see Controls) |
| Pulse In 2 | Insta-ference | Rising edge — fire a one-shot event from the curated Insta-ference bank |

## Outputs

| Jack | Panel | Function |
|------|-------|----------|
| Audio Out 1 | Output | Full mix — tuned audio, whistles, static, Insta-ference |
| Audio Out 2 | Just Noise | Noise / static only |
| CV Out 1 | Signal Strength | An envelope that rises as you tune onto a Station |
| CV Out 2 | Station 1 CV Offset | Station 1's offset from the main knob |
| Pulse Out 1 | Station 1 Tuned Gate | Gate HIGH while tuned to Station 1 |
| Pulse Out 2 | Station 2 Tuned Gate | Gate HIGH while tuned to Station 2 |

## Controls

**Main Knob — Tuning.** Scans across the dial. Sums with CV In 1 (Tuner).

**Knob X — Brightness (IF bandwidth).** Sets both how wide a Station's capture window
is and the audio brightness. Fully CCW = narrow, muffled and selective (fiddly to
tune); fully CW = wide, bright and easy to find.

**Knob Y — Noise Level.** Silent fully CCW; raises the static floor toward full CW.
The static slowly swells and swishes on its own (random-walk level and filter
sweeps), heaviest on LW. Noise ducks away when you tune onto a Station — fully on AM,
strongly on SW/LW. CV In 2 adds to this.

**Switch Down tap — Cycle band (Radio).** AM → SW → LW, re-randomising the layout
each time.

**Switch Up hold — mode-dependent.** What it does depends on how you booted:

- **Baked-in audio mode:** mutes Station 1 & 2. The interference signals carry on, and
  the Stations still "exist" — they keep driving CV Out 1/2 and Pulse Out 1/2 at their
  dial positions — they just make no sound. Release to bring them back.
- **Audio-input mode:** Station 2's audio is replaced by a ~600 Hz morse tone keyed by
  **Pulse In 1 (Morse In)** — feed a rhythm or gate pattern into PU1 and it becomes an
  audible keyed signal at Station 2's position (heard when tuned to it, pitch-shifting
  off-tune like any Station). While the switch is held up, Pulse In 1 no longer
  shuffles the layout.

## Insta-ference (one-shot bank)

Pulse In 2 (Insta-ference) fires a short event from a curated bank — clicks, dropouts,
crashes, single morse bursts: things that make sense heard in isolation. Each trigger
plays a random clip once through and stops; re-triggering restarts it. The
Insta-ference bank is separate from the looping interference clips, so the two can be
curated independently (see *Making your own version* below). If the bank is empty the
firmware falls back to the looping interference clips.

## Boot modes

**Audio-input mode** — power on with the switch in any position other than Down (or
release it within the first ~200 ms). The two live audio inputs become Station 1 & 2.

**Baked-in audio mode** — hold the switch Down at power-on until all six LEDs flash.
The two live inputs are replaced by baked recordings, turning the module into a
self-contained radio. Everything else works identically.

## LEDs

| LED | Function |
|-----|----------|
| 0 | Station 1 signal strength |
| 1 | Station 2 signal strength |
| 2 / 3 | Band — both off = AM, LED 2 = SW, LED 3 = LW |
| 5 | Tuning position |


## Patch ideas

- **Auto-tune to a Station.** Patch CV Out 2 (Station 1 CV Offset) through a slew into
  CV In 1 and the dial tunes onto Station 1, whatever the knob is set to. Trigger
  Pulse In 1 to re-randomise and it slowly hunts to the new position — a self-playing
  radio that keeps finding the station (cleaner on AM band, LW and SW get 'near').

- **Fine-tune to a Station.** Self-patch CV Out 2 (Station 1 CV Offset) straight into
  CV In 1 with no slew. The dial snaps to Station 1, and shuffling the layout (Pulse In
  1) or changing band keeps tuning locked near Station 1 — so the Main knob becomes a
  fine-tune around it.

- **Rhythmic morse instrument.** Boot in audio-input mode, hold Switch Up, and clock
  Pulse In 1 (Morse In) from a sequencer or trigger pattern. Station 2 becomes a keyed
  ~600 Hz tone in time with your rhythm — tune onto it for a clean beep, or off it for
  pitch-shifted, whistling morse. A signal that's also a musical part.

- **Radio as an envelope/gate source.** CV Out 1 (Signal Strength) rises as you tune
  in — patch it to a VCA or filter so other voices swell as you find a station. Use
  Pulse Out 1 / 2 (Station 1 / 2 Tuned Gate) to clock or gate envelopes only while a
  Station is locked.

- **Hands-free band scanning.** Patch a slow LFO to CV In 1 (Tuner) to drift across
  the dial on its own — stations and interference drift past, whistling in and out.
  Add Pulse Out triggers to fire events each time a Station passes.

- **Tuned drum trigger.** Sweep slowly and take Pulse Out 1/2 as triggers: each pass
  through a Station fires a hit — the dial layout (re-randomised by Pulse In 1) sets
  the rhythm.

- **Stab punctuation.** Clock Pulse In 2 (Insta-ference) from an off-beat to drop in
  short clicks/dropouts/morse bursts as percussion under the tuned audio.

- **Process external audio.** In audio-input mode, feed your own sources into Audio In
  1 / 2 (Station 1 / 2) and use the radio as a tuning-based degrade/pitch-shift/gate
  effect — tune in for clean, off for the SSB-mangled version.

- **Dual-output split.** Take the full mix from Audio Out 1 and pure static from Audio
  Out 2 (Just Noise) into separate channels — process the static independently
  (reverb, gate) for an atmospheric bed under the stations.



## Technical notes

- Behavioural demodulation: per-Station detune drives a sliding heterodyne whistle, an
  SSB frequency-shifter (IIR Hilbert quadrature pair, SW/LW) or envelope detection
  with off-tune distortion (AM), and a strength fade. All processing stays in the
  audio band, so there is no aliasing.
- Integer-only DSP, runs on core 1; an RF PWM path runs on core 0.
- Interference / Insta-ference clips: 8 kHz unsigned 8-bit mono.
- Station clips: 11025 Hz 12-bit packed signed (2 samples per 3 bytes).
- Looping interference clips are trimmed to ≤12 s to leave flash headroom for the
  Insta-ference bank; Station clips run ~32–34 s.
- 200 ms startup holdoff before audio begins, with a short linear fade-in.

## Using the prebuilt firmware

Download **`offair.uf2`** from the
[releases page](https://github.com/uglifruit/Workshop_Computer/releases) (or grab it
from this folder), hold BOOTSEL on the Computer, and drag the file across. No build
needed; the baked-in audio (`clips.h`) is already compiled in the firmware.

## Making your own version with custom sounds

All the radio audio is baked into `clips.h`, generated from source audio files by
`convert_clips.py`. The audio sources themselves are **not** included in this repo —
supply your own. To build a version with your own Stations / interference / events:

**1. Install the Python tools** (one-off): `pip install miniaudio numpy`

**2. Prepare your audio.** Any format/sample rate works (WAV, MP3, AIFF, FLAC, mono
or stereo, any rate — the script resamples and downmixes). There are three pools:

- **Stations** — the two main signals you tune in (played in baked-in audio mode).
- **Looping interference** — continuous beds (numbers, morse, data) that sit at
  dial positions and loop forever.
- **Insta-ference** — short isolated events (clicks, dropouts, crashes, single bursts)
  fired by Pulse In 2. Curate these to make sense heard alone.

**3. Point the script at your files.** Edit the top of `convert_clips.py`:

- `FOLDER` — the directory holding your Station and looping-interference files.
- The `CLIPS` table — list each Station / interference file (filename, start
  second, max length). `LOOP_MAX_SEC` caps interference loop length;
  `MAX_BCAST_SEC` caps Station length. Keep 2 Station + 6 interference entries
  unless you also change the counts in `main.cpp`.
- `ONESHOT_FOLDER` — a folder of Insta-ference files; **all** files in it are
  auto-discovered (any count). `ONESHOT_MAX_SEC` caps each one's length.

**4. Generate and build:**

```
python convert_clips.py          # regenerates clips.h, prints sizes + flash budget
```
Then build with the Pico SDK (CMake / Ninja) from this folder and flash the new
`offair.uf2`. The script warns if your audio exceeds the ~2 MB flash budget — trim
clip lengths or drop Insta-ference if so.

## Credits

By Andy Jenkinson ([uglifruit](https://github.com/uglifruit)), developed with
[Claude Code](https://claude.ai/code).

Built on the [Workshop Computer](https://github.com/TomWhitwell/Workshop_Computer)
platform by Tom Whitwell (Music Thing Modular), using the ComputerCard framework.

The RF PWM transmission path — generating an AM carrier on the DEBUG_1 pin receivable
by a nearby MW radio — is directly inspired by and based on
[AM Coupler](https://github.com/TomWhitwell/Workshop_Computer/tree/main/releases/12_am_coupler)
by Chris Johnson.

Inspired by [RadioMusic](https://github.com/TomWhitwell/RadioMusic) by Tom Whitwell
and the [Music Thing Radio Music workshop](https://dyski.co/Tom-Whitwell-Radio-Music).

Interference recordings sourced from
[Numbers & Oddities](https://www.numbersoddities.nl/files.html), a shortwave
monitoring archive. Individual recordings are in the public domain.

The baked-in Station recordings are *Protect and Survive* (UK Government, 1970s civil
defence broadcast) and the *Shipping Forecast* (BBC Radio 4). These are included for
personal and educational use only. *Protect and Survive* material is Crown Copyright;
*Shipping Forecast* is © BBC. Neither is licensed for commercial use or
redistribution. If you build a derivative work for release, replace these recordings
with material you have the rights to use.

Licensed under
[Creative Commons Attribution-ShareAlike 4.0](https://creativecommons.org/licenses/by-sa/4.0/)
— except where third-party copyright applies as noted above.
