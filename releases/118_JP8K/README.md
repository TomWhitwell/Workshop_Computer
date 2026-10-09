# JP8k-sandsquall

JP8k-sandsquall is a JP-inspired Supersaw voice for the Music Thing Workshop
Computer, a wide animated stack of saw oscillators.

The card includes a built-in sequencer which is accessible through the webUI. The default sequence may be familiar...

## Initial Setup: Supersaw Lead

For the intended first patch, start with a Sandstorm-inspired supersaw lead:

| Control | Starting position |
| --- | --- |
| Z | Up: gated CV/MIDI supersaw synth |
| Main | 12 o'clock, then trim by ear |
| X | 10:30 to 11 o'clock for tight animated detune |
| Y | 2 to 3 o'clock for bright saw edge |

Patch **Pulse In 1** from a fast gate or envelope rhythm, and patch **Audio In
1** from 4 Voltages, a sequencer, or another pitch source. On Computers with
input calibration stored in EEPROM, Audio In 1 uses the ComputerCard 0.4.0
calibration data directly: 1000 mV is one octave. On an uncalibrated Computer
it falls back to the documented 341-counts-per-volt audio-input scale. Use
**CV In 1** for bipolar filter modulation, added to Y. Take **Audio Out 1** as
mono, or use both audio outs for the wide version. A filter,
VCA/envelope, and short delay after the card will get much closer to the
classic trance lead shape than the raw oscillator alone.

Patch **Pulse In 2** from an accent rhythm if you want extra bite on selected
steps. Its rising edge adds a short attack snap, and it gives a small level
lift while high.

For the sequencer, flip **Z Middle**. It selects and arms the 32-step
Sandstorm-sting pattern extracted from `sand stings.mid`, but remains silent
until started. With no clock source connected, briefly move **Z Down** and
back to Middle to start or stop the internal clock; Main then sets its tempo.
Pulse In 2 clocks the sequence from rising edges whenever patched. Clock and
reset edges are captured at the 48 kHz audio rate, so short Keystep clock
pulses are not missed. Pulse In 1 resets the sequence.

The sequencer also follows USB MIDI Clock when Pulse In 2 is unpatched. It
steps once per six MIDI Clock ticks (sixteenth notes at the standard 24 PPQN),
but timing clocks alone do not start it. MIDI Start resets and starts the
phrase, Continue resumes it, and Stop closes the sequence gate. Pulse In 2
takes priority whenever it is patched. LED 4 stays bright while incoming MIDI
Clock keeps the USB MIDI connection active.

In **Z Up**, Pulse In 1 is a sustained gate: high opens the lead envelope,
low closes it. When the envelope reaches zero, the audio path is hard-muted.

## Controls

| Control | Function |
| --- | --- |
| Main | Tune / transpose in synth mode; tempo in sequencer mode; ignored while MIDI notes are active |
| X | Supersaw spread / detune; fully counter-clockwise switches to a single saw for tuning |
| Y | Brightness, from warm pad to raw bright saw |
| Z Up | Synth mode: CV/gate and USB MIDI lead |
| Z Middle | Sequencer mode: select and arm the sting pattern |
| Z Down | From sequencer mode with no external clock, internal sequencer start/stop; neutral in synth mode |

## Patch Points

| Jack | Function |
| --- | --- |
| Audio In 1 | Calibrated pitch CV, 1V/oct when input calibration is present |
| CV In 1 | Bipolar filter-cutoff modulation added to Y brightness |
| CV In 2 | Spread modulation |
| Pulse In 1 | Sustained lead gate in synth mode; sequencer reset in sequencer mode |
| Pulse In 2 | Rising edge triggers an accent snap and high holds a level lift in synth mode; sequencer clock in sequencer mode |
| Audio Out 1 | Left / mono output |
| Audio Out 2 | Right output |
| CV Out 1 | Approximate 1V/oct monitor of the internal pitch |
| CV Out 2 | Filter-coefficient monitor, following Y, CV In 1, and the sequencer brightness contour |
| Pulse Out 1 | Gate monitor; falls at note-off before the short audio release tail |
| Pulse Out 2 | Square pulse from the centre oscillator while the voice is active |

## Notes

The audio engine uses seven integer saw oscillators, fixed-point detune, a
neutral two-pole low-pass tone filter, and an attack/release envelope. Control
calculations run every 32 samples so the 48 kHz audio interrupt stays light.

The supersaw mix is centre-weighted with a bright attack transient and an
audible spread curve, so X around 10:30-11:30 aims at the classic bright trance
lead while higher settings clearly widen and detune the swarm.

USB MIDI works in device mode when patched to a computer, and in host mode when
the Workshop Computer is powering a class-compliant USB MIDI controller. MIDI
uses channel 1. Note on/off messages play the same voice and act like another
sustained gate source; pitch bend is +/-2 semitones. While a MIDI note is held,
the MIDI note supersedes Main and sets the pitch. Audio In 1 is still added on
top as patchable 1V/oct pitch modulation.

MIDI CCs:

| CC | Function |
| --- | --- |
| 1 or 20 | Spread, replacing X after the first received CC |
| 21 or 74 | Brightness in USB host mode; USB device mode keeps physical Y active |
| 7 | Volume, scaling the output level after the first received CC |

LED 4 shows the USB MIDI setting: dimmer for device mode, brighter for host
mode, and full-bright on MIDI activity. LED 5 follows the envelope, but stays
bright while a MIDI note is held.

## Sequencer

Z middle runs a 32-step pattern seeded from a familiar sequence
file:

```text
B B B B B - B B  B B B B B - E E
E E E E E - D D  D D D D D - A A
```
 Main
sets the internal tempo from about 60-240 BPM when Pulse In 2 is unpatched.
With neither Pulse In 2 nor active USB MIDI Clock present, dip Z down and back
to middle to start or stop the internal clock. Patch a clock to Pulse In 2 to
step the sequence externally; Pulse In 1 resets back to step 1. The factory
phrase uses shorter inner-note gates, longer phrase endings, and a rising
brightness contour added to Y for a more played, less static supersaw line.
The Web MIDI editor can still replace its notes, gates, and accents. Sending a
pattern saves it to the card, so it is restored after a power cycle.

The browser editor in `web/index.html` can send a replacement 32-step pattern,
tempo, accents, gates, and basic performance controls over Web MIDI SysEx.

The card runs the RP2040 at 192 MHz by default. A `JP8K_SANDSQUALL_OVERCLOCK_240` build
define is provided for later testing if the voice grows heavier, but the first
version should not need it.

Version 0.1.14 updates the card to ComputerCard 0.4.0. Its corrected input bounds
and full-travel knob scaling are used directly by this card; its existing
0-4095 control mappings therefore keep their intended endpoint behaviour.

Version 0.1.15 initially added calibrated pitch tracking using the CV-input
calibration data already loaded by ComputerCard 0.4.0. It fell back to the
previous hardware-tested raw scale when a Computer had no input calibration.

Version 0.1.16 captures sequencer clock and reset edges at audio rate, rather
than polling them from the slower USB/control core. This makes Pulse In 2
reliable with short external-clock pulses, including the Keystep clock output.

Version 0.1.17 adds USB MIDI Clock, Start, Continue, and Stop transport for
the sequencer. MIDI Clock runs the pattern when Pulse In 2 is unpatched.

Version 0.1.18 gives the factory Sandstorm-sting sequence shaped gate lengths,
clearer phrase accents, and a step-based brightness contour layered on top of
the physical Y control.

Version 0.1.19 changes sequencer transport. Z middle now selects a quiet,
armed sequencer rather than starting it immediately. With no active clock
source, dip Z down and return to middle to start or stop internal playback;
the previous Z-down forced-gate accent is removed. USB MIDI Clock now requires
MIDI Start or Continue before it advances; Stop silences the pattern, while
bare timing clocks merely establish sync.

Version 0.1.20 replaces the brightness stage with two neutral fixed-point
low-pass poles in series. Y and MIDI brightness keep their existing mapping;
the extra pole adds more useful warmth at low settings without adding
resonance, filter modulation, or a new voice mode.

The Audio In 1 pitch CV input is calibrated when the Workshop Computer has
valid input calibration in EEPROM. On Computers without that data, the card uses
the documented raw audio-input fallback, so it remains musically playable but
should be trimmed and checked by ear.

For pitch testing, set **Z Up** with a gate patched,
set **Main** to 12 o'clock, set **X** fully counter-clockwise, leave **CV In
1** and **CV In 2** unpatched, and set **Y** high enough to hear a bright saw.
Patch the pitch source to **Audio In 1**. In that position the card disables
the detuned side oscillators and outputs only the centre saw, so 1V/oct
tracking can be checked without the supersaw beating confusing the tuner.
Bring X up after tuning to restore the JP-style spread.

Version 0.1.21 moves calibrated 1V/oct pitch control to Audio In 1 and assigns
CV In 1 to bipolar low-pass cutoff modulation. CV In 2 remains Supersaw spread
modulation.

Version 0.1.22 finalises the card identity as JP8k-sandsquall. Its USB MIDI
name, browser editor, SysEx tag, firmware target, and canonical UF2 now use
the final name.

Version 0.1.23 saves a Web MIDI editor pattern in the card's final flash
sector. A valid saved pattern replaces the factory pattern on boot; changed
MIDI performance controls are intentionally not saved.

The versioned `UF2/JP8K_SANDSQUALL_0.1.23.uf2` is the sole release firmware.
Earlier test builds are kept locally in `UF2/archive/` and are intentionally
excluded from release commits.

## Firmware distribution notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency
copyright and license texts. Keep the accompanying `NOTICE.txt` with
each UF2 when redistributing it, or distribute the ZIP produced by
`license_tools/package_uf2.py` from the releases directory.
