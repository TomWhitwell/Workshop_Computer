# Bibesque

Bibesque is a Workshop Computer adaptation of the **Bib** stereo dub processor
by Plinky Synth. It recreates Bib-inspired drive, wavefold, stereo tape delay,
clock quantisation, rhythmic multi-tap delay, modulated reverb, shimmer and
dub freeze using the Workshop's three pots, switch, CV inputs and six LEDs.

## Install

Flash `uf2/Bibesque_1.0.0_rc1.uf2` to a Workshop Computer program card, then
reset the module. This is a hardware-tested release candidate.

## Firmware verification

`Bibesque_1.0.0_rc1.uf2` SHA-256:
`b5a17ea961525608482eb7649a371fd8593a5a6879cbdf0d73d777516156d1d9`

## Controls

Main selects Drive, Delay, Reverb or Mix in four regions. LEDs 0–1 show that
page as binary: off/off, on/off, off/on, on/on.

- **Drive:** X = drive; Y = bipolar delay send. Clockwise Y is normal stereo
  delay; anticlockwise is asymmetric ping-pong. Tap Z to toggle overdrive and
  wavefold.
- **Delay:** X = delay time; Y = feedback. Tap Z once to begin a multi-tap
  phrase, then tap up to eight relative repeat heads. Move X substantially to
  clear the phrase and return to one even repeat. Pulse In 1 clock-quantises
  X to Bib-style 3/4, straight and dotted divisions across octaves.
- **Reverb:** X = reverb send; Y = decay. With Z Up, X sets persistent shimmer.
- **Mix:** X = wet/dry mix; Y = output level. Hold Z Down for dub freeze.

With Z Up on the Delay page, X controls tape speed (stop through 2x) and Y
controls wobble. Long delays and long multi-tap phrases use automatic tape
slowdown, extending the 32k-sample tape to roughly two seconds.

CV In 1 and CV In 2 are bipolar modulation inputs for the active page's X and
Y parameters. CV is added after physical-pot pickup.

## LEDs

LEDs 2–5 change with the active page. LEDs 2 and 3 show effective X/Y values,
including CV. LEDs 4–5 show relevant state: wavefold/ping-pong, clock-lock/
multi-tap count, shimmer/wet mix, or freeze/limiter reduction.

## Attribution and licence

This is an independent adaptation, not original Bib hardware or panel artwork.
It directly adapts MIT-licensed Bib DSP ideas and reverb code from the
[Buddies public source repository](https://github.com/plinkysynth/buddies_public),
and includes the MIT-licensed [ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard)
framework by Chris Johnson. Bibesque adaptation and integration by Adrian Vos.
The included `LICENSE` contains the MIT licence text.
