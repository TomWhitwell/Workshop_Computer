# Alien Sitar

A sitar-inspired program card for the Music Thing Modular Workshop Computer.
Pluck a melody string with jawari bridge buzz and an evolving decay, while twelve
sympathetic strings bloom and continue ringing between notes.

## First sound

Connect **Audio1 OUT** to your mixer and set switch Z to **Middle**. Set the big
Jawari knob and left Swell knob near **65%**, the right String knob near **80%**,
then tap **Down** to pluck. With pitch unpatched, the starting note is **D3**.

## Knobs and switch

| Knob | Normal playing | Hold Down and deliberately move this knob |
|---|---|---|
| **Big / Main** | **Jawari** — bridge contact and buzz | **Sympathy** — sympathetic halo level |
| **Left / X** | **Swell** — decay and secondary amplitude rise | **Octave** — seven steps, −3 to +3; centre = 0 |
| **Right / Y** | **String** — string tone and brightness | **Pick** — soft release / original at centre / sharp release |

| Switch Z | Function |
|---|---|
| **Middle** | Plucked instrument; audio inputs provide timbre modulation |
| **Up** | External excitation: incoming audio makes the string and halo resonate |
| **Down, momentary** | One manual pluck on entry; hold and move knobs for secondary settings |

Holding Down does not repeat the pluck or change a secondary setting unless its
knob moves deliberately. Each knob edits independently. Normal timbre settings
stay frozen during the hold. After release, return an edited knob through its
previous normal position to pick up normal control again.

Secondary settings persist until reboot: **Sympathy full**, **octave −1** and
**Pick centre**. Pick changes the next pluck; Sympathy changes the audible halo
without clearing the ringing strings. Pitch changes bend the melody string while
the sympathetic bank keeps its fixed tuning.

## Inputs

| Input | Middle / Down | Up |
|---|---|---|
| **CV1** | Melody pitch, **1 V/oct** | Same |
| **CV2** | Meend / pitch bend: **±5 V = ±2 semitones** | Same |
| **Audio1** | Jawari CV, added to big knob | Excite the melody string |
| **Audio2** | Swell CV, added to left knob | Excite the sympathetic bank |
| **Pulse1** | Pluck on rising edge | Internal pluck remains available |
| **Pulse2** | Smoothly damp the instrument on rising edge | Same |

At the default octave −1, **CV1 IN at 0 V plays C3**, **+1 V plays C4** and
**unpatched plays D3**. Choose octave 0 for 0 V = C4. Octave edits transpose both
patched and unpatched pitch. Changing pitch does not trigger a new pluck. The
melody range is approximately 40–2000 Hz.

In Middle/Down, Audio1/2 accept nominal **±5 V for ±one full knob range**, added
to the knob and clamped. Patched 0 V and empty sockets add zero. Cable insertion
settles and ramps in; parameter changes are smoothed. These are slow modulation
controls rather than audio-rate FM inputs. In Up, use audio instead; attenuate
loud or continuous excitation sources upstream.

## Outputs

| Output | Signal |
|---|---|
| **Audio1** | Complete mono instrument, including the halo — main listening output |
| **Audio2** | Sympathetic halo alone, for separate effects or mixing |
| **CV1** | Played pitch, **1 V/oct, 0 V = C4**; includes octave, bend and pitch slew |
| **CV2** | Audible halo loudness follower, nominal **0–5 V** |
| **Pulse1** | **5 ms** pulse when a manual or external pluck is accepted |
| **Pulse2** | **5 ms** pulse on a sustained rise in the audible halo |

**CV1 OUT always uses C4 at 0 V.** Default unpatched D3 gives **−0.833 V** out;
patched CV1 IN at 0 V with octave −1 gives **−1 V** out. Tune another oscillator
to C4 at 0 V to follow the melody. Output range is approximately −2.71…+2.93 V.
This follows nominal string pitch, so bridge buzz can introduce small audible
tuning differences. Voltage accuracy depends on input and CV-output calibration.

Pulse2 responds to the combined halo, including external excitation. It is not
a pulse for every note or a fixed delay after a pluck: overlapping swells can
merge. Detection requires a sustained rise, with a minimum 200 ms holdoff and
rearm hysteresis. Damping briefly inhibits detection; muted Sympathy produces
no bloom events. Pulse1 remains the output for following each accepted pluck;
audio follows after a short buffering/filter delay.

Try CV1 OUT to another oscillator's pitch, CV2 OUT to a filter or reverb amount,
and Pulse2 OUT to an accent or sample-and-hold. Audio1 already contains halo;
adding Audio2 raises the halo level rather than removing it from Audio1.

## LEDs

| Row | Left | Right |
|---|---|---|
| **Top** | Pluck | External-excitation mode |
| **Middle** | Melody level | Halo level |
| **Bottom** | Latched timing fault — should stay off while playing | Input or CV-output calibration missing |

## Loading the card

Load [alien_sitar.uf2](alien_sitar.uf2) onto a spare program card using the
[manufacturer's installation procedure](https://www.musicthing.co.uk/workshopsystem/program-cards/install/).
The Computer needs its normal rack/system power; front USB does not power it.
Settings reset on reboot. The card provides no runtime USB MIDI/audio or editor
and writes no settings or calibration data.

MIT licensed; see [LICENSE](LICENSE). Includes Chris Johnson's MIT-licensed
ComputerCard hardware interface.
