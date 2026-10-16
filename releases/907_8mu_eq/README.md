# 8mu EQ

*8mu EQ* is a stereo eight-band graphic equaliser, controlled by the faders of a [Music Thing Modular 8mu](https://www.musicthing.co.uk/8mu_page/).

Each audio channel is split by eight bandpass filters, an octave apart, at 60, 120, 240, 480, 960, 1920, 3840 and 7680Hz. Each 8mu fader sets the level of one band, from silent at the bottom of its travel to full level at the top, and the bands are summed back together to give the output. The 8mu's own LEDs show how much signal there is in each band.

With nothing patched into the audio inputs, the card filters its own noise source, so it also works as a playable stereo noise generator.

This card is a standalone version of the *8mu EQ* example patch for the Patcher+ card.

### Hardware requirements

* Workshop System Computer Rev 1.1 or later, which can act as a USB host.
* A Music Thing Modular 8mu, plugged into the Computer's USB socket. A USB hub between the two also works.

Without an 8mu, all the faders read zero, so the outputs are silent.

### Controls

* **8mu faders 1-8:** level of the 60, 120, 240, 480, 960, 1920, 3840 and 7680Hz bands. The gain is the square of the fader position, so there is plenty of resolution at low levels, and a fader at the bottom removes its band completely.
* **Main knob:** filter Q (resonance), from 0.5 fully anticlockwise to 3.5 fully clockwise. Low Q gives broad, overlapping bands and a smooth response. High Q gives narrow, ringing bands, each of them louder at its centre frequency.
* **Knob Y:** fader slew. Fader movements are rate-limited, from about 10 seconds for the full fader travel with the knob fully anticlockwise, to about 50ms fully clockwise. Turn it down for slow filter sweeps from quick fader moves.
* **Knob X:** unused.
* **Switch:** colour of the internal noise source, used when no jack is plugged into an audio input.
    * Up: pink-ish noise
    * Middle or down: white noise

### Inputs and outputs

* **Audio in 1 and 2:** left and right inputs.
    * A jack in Audio in 1 only is treated as a mono input, and feeds both channels.
    * A jack in Audio in 2 only feeds the right channel, with the internal noise source on the left.
    * With neither patched, both channels use the internal noise source, an independent one for each channel.
* **Audio out 1 and 2:** left and right outputs.

### LEDs

* **Top row:** left and right input level.
* **Bottom row:** left and right output level.
* **8mu LEDs 1-8:** signal level in each band of the left channel, with a peak hold that decays over a fraction of a second.
* **All six LEDs flashing at 2Hz:** no 8mu has been detected.

### Tips

* The total level depends on the input spectrum, the Q and the number of faders that are up. With several faders up and high Q, a loud input can clip the outputs.
* Pulling all the faders down and pushing one up isolates a single band, which with high Q and the noise source makes a pitched noise tone.
* The slew applies to every fader together, so with Knob Y anticlockwise, a quick gesture across several faders becomes a slow, smooth change in tone.

### Building

The card is built with the Pico SDK (2.1 or later) and [ComputerCard](https://github.com/TomWhitwell/Workshop_Computer/tree/main/Demonstrations%2BHelloWorlds/PicoSDK/ComputerCard), with `EightMU.h` providing the 8mu USB host support.

```
mkdir build
cd build
cmake ..
make
```

Then copy `build/8mu_eq.uf2` to the Computer.
