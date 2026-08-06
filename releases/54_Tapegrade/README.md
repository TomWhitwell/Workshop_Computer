# Tapegrade

Tapegrade is a cassette-style warble and degradation effect for the Workshop Computer platform.

It turns a mono input signal into a wide stereo tape texture using wow, flutter, saturation, hiss, and crackle.

Designed for everything from subtle tape movement to heavily damaged cassette sounds.

---

# Features

* Mono input → stereo output
* Cassette wow and flutter
* Tape-style saturation
* Hiss and crackle degradation
* Continuous tape wear morphing
* CV modulation support
* Stereo tape movement
* Pulse-controlled tape damage bursts
* Rhythmic crackle injection
* CV passthrough attenuation outputs

---

# Inputs

## Audio In 1

Main mono audio input.

Tapegrade is designed for mono input only.

---

## Audio In 2

Tape-condition modulation input.

Use audio or CV signals to continuously morph between:

* clean tape
* old tape
* damaged tape

---

## CV1

Modulates tape depth.

Controls the amount of wow and pitch movement.

Audio rate around C3 encouraged

Also routed to CV Out 1 through the X knob attenuator.

---

## CV2

Modulates instability.

Controls flutter speed and transport agitation.

Audio rate around C3 encouraged

Also routed to CV Out 2 through the Y knob attenuator.

---

## Pulse In 1

Tape damage burst trigger.

Each rising edge briefly forces the tape into a heavily degraded state.

Recommend high rate triggers!

Could be useful for rhythmic tape-drop effects and transient degradation bursts.

---

## Pulse In 2

Crackle gate input.

While high, Pulse In 2 forces strong crackle generation regardless of tape condition.



---

# Outputs

## Audio Out 1 / 2

Stereo cassette-processed output.

Stereo width is generated internally using decorrelated wow/flutter modulation.

---

## CV Out 1

CV1 passthrough output scaled by the X knob.

Functions as a CV attenuator output.

---

## CV Out 2

CV2 passthrough output scaled by the Y knob.

Functions as a CV attenuator output.

---

# Controls

## Main

Wet/dry mix.

---

## X

Tape depth.

Higher settings increase cassette wobble and pitch instability.

Also controls attenuation amount for CV Out 1.

---

## Y

Instability amount.

Higher settings create rougher and faster tape movement.

Also controls attenuation amount for CV Out 2.

---

# Switch Modes

## Up — Clean

Bright and stable cassette response.

---

## Middle — Old

Darker tone with moderate hiss and instability.

---

## Down — Damaged

Heavy degradation with strong hiss, crackle, and unstable tape behaviour.

---

# Notes

* Mono input only
* Stereo image is generated internally
* Audio-rate modulation supported on Audio In 2
* Pulse inputs can be clocked or triggered rhythmically
* CV outputs operate independently from the audio path
* Optimised for Workshop Computer hardware
