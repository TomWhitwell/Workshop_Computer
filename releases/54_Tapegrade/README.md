# Tapegrade

Give your audio a gentle cassette wobble or push it into worn-out tape sounds.
Tapegrade turns a mono input into stereo using wow, flutter, saturation, hiss
and crackle on the Workshop Computer.

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

# Inputs

## Audio In 1

Patch your mono audio source here.

---

## Audio In 2

Patch audio or CV here to move continuously between:

* clean tape
* old tape
* damaged tape

---

## CV1

Modulates tape depth.

Controls the amount of wow and pitch movement.

Try audio-rate modulation around C3.

Also routed to CV Out 1 through the X knob attenuator.

---

## CV2

Modulates instability.

Controls flutter speed and transport agitation.

Try audio-rate modulation around C3.

Also routed to CV Out 2 through the Y knob attenuator.

---

## Pulse In 1

Each rising edge triggers a short burst of heavy tape damage.

Try fast triggers for rhythmic tape dropouts and short bursts of damage.

---

## Pulse In 2

Hold this input high to add strong crackle, whatever tape condition is selected.

---

# Outputs

## Audio Out 1 / 2

Stereo cassette-processed output.

Stereo width is generated internally using decorrelated wow/flutter modulation.

---

## CV Out 1

Passes CV1 through an attenuator controlled by X.

---

## CV Out 2

Passes CV2 through an attenuator controlled by Y.

---

# Notes

* Mono input only
* Stereo image is generated internally
* Audio-rate modulation supported on Audio In 2
* Pulse inputs can be clocked or triggered rhythmically
* CV outputs operate independently from the audio path
* Optimised for Workshop Computer hardware

## Attribution

Tapegrade uses the ComputerCard hardware framework by Chris Johnson. The
framework's original notice is retained in `ComputerCard.h`.

## Firmware distribution notices

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for dependency
copyright and license texts. Keep the accompanying `NOTICE.txt` with
each UF2 when redistributing it.
