# Tape Bias

Tape Bias gives your audio the drive, hiss and changing tone of a 1960s-style
reel-to-reel recorder. Explore rough under-biased sounds, softer over-biased
treble, magnetic saturation, tape formulations, transport speeds and replay EQ.

Compared with Tapegrade's cassette warble and damage, Tape Bias focuses on a
steadier record/replay sound.

## Controls

| Control | Function |
| --- | --- |
| Main | Record level and magnetic drive |
| X | Bias calibration, from under-biased through nominal to over-biased |
| Y | Tape formulation, from low-output ferric to higher-output tape |
| Switch Up | 3.75 ips: reduced bandwidth and stronger head loss |
| Switch Middle | 7.5 ips: standard transport setting |
| Switch Down | 15 ips: wider bandwidth and less head loss |
| CV In 1 | Bias modulation |
| CV In 2 | Tape-formulation modulation |
| Pulse In 1 | Erase tape state, reset magnetic memory, and create a brief replay dropout |

Audio In 1 is a mono input. Audio Out 1 and Audio Out 2 carry the replay signal.
CV Out 1 reports the effective bias offset and CV Out 2 reports slow magnetic flux.

## Bias Behaviour

At nominal bias, the model gives the most even replay response. Turning X below
centre produces a brighter but rougher record characteristic with more nonlinear
artefacts. Turning it above centre progressively softens the replay high end.

The firmware does not generate a literal 100 kHz bias oscillator: at the Workshop
Computer's 48 kHz audio rate that would require substantial oversampling. Instead,
it uses an averaged magnetic transfer curve whose saturation, high-frequency loss,
and hiss vary with the virtual bias calibration.

## Release notes

### 0.1.1 and testing

Version 0.1.1 is the current stable fallback build, hardware-tested by one person
with sine, square, bass-loop, bias-CV, tape-type, and erase-input checks. It remains
Beta rather than a calibrated emulation of a particular recorder or tape formulation.

## Firmware distribution notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency
copyright and license texts. Keep the accompanying `NOTICE.txt` with
each UF2 when redistributing it.
