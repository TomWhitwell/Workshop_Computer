**The Bells / Program Card V1**

A bellringing methods sequencer by James Saunders

**What is The Bells card?** 

The Bells is a sequencer that provides 12 bell ringing methods with tempo and jitter controls, quantised to six different scales.

It sends a v/oct pitch sequence and synchronised pulse to control the oscillators or other inputs that could respond to permutation patterns, and optionally plays the methods using bell samples.

Its parameters are controllable during playback and in a setup mode, accessible by holding Z down while turning a knob. Z switches between play, stop and edit modes. 

**About**

This is a port from a prototype Eurorack module I’ve been working on over the last year. It has some additional functionality (8 gates to cue a sampler, more methods, pitch selection), but otherwise the Workshop System version is very similar. I originally made the module as my first attempt at hardware development, having been ringing bells for the past two years, and also having an obsession with permutation patterns. 

I would love to hear what anybody does with this, so please send me anything you make. Also, new feature suggestions are always welcome, as are any information about errors. 

More information on bell ringing: [change ringing](https://en.wikipedia.org/wiki/Change_ringing) / [methods diagrams](https://fortran.orpheusweb.co.uk/Bells/Diagrams/IndPDF.htm)

For questions or support, email [jamessaundersalloneword@gmail.com](mailto:jamessaundersalloneword@gmail.com) or visit [www.james-saunders.com](http://www.james-saunders.com)

**CONTROLS**

| Z | UP: play MIDDLE: stop DOWN (hold): edit  |  |  |  |
| :---- | :---- | :---- | :---- | ----- |
|  | PLAY/STOP MODES |  | EDIT MODE   |  |
| MAIN | Changes raw tempo from 5-300 BPM |  | Select method  |  |
| X | Sets jitter between 0-40% of  |  | Select clock divide amount from /8 to x8  |  |
| Y | Turns covering tenor off (CCW-12) or on (12-CW) |  | Select scale |  |
|   **LEDs**  |  |  |  |  |

<img src="https://github.com/TomWhitwell/Workshop_Computer/blob/main/releases/16_the_bells/images/leds.svg" width="600">

**UPDATES**

v1.1 (30.06.26)
- First main update adds sampled bells from my local tower, recorded last night wiht thanks to Will Rogers. Patch audio out 1 and 2 to the mixer (either channels 1 and 2 pr use a stackable to send them to the same channel), or via processing. Select the method in the normal way and click Z up to play the chosen methd with sampled bells. The tempo, jitter and covering tenor settings all work, but the scale has no effect (samples, not CV). Note it only currently works with six bells as our twoer is only a ring of six - I'll record a ring of eight when I get a moment so all the methods will work.
- Corrected some errors in the methods.

Some sound recordings are now on discord here: https://discord.com/channels/1210238368898879569/1520571591711391754

After playing part of a sequence and then stopping it, flipping the Z switch up to start it will continue from where it left off. To restart from the beginning (the descending scale) flip it down to the edit mode and release, and then it will reset.

After editing and then playing the sequence, it will use the current knob positions. So for example if you chose pattern 12 (CCW) the tempo will be fast, and if you altered the clock divider it may use some jitter. Just move the knobs where you want them before pressing start so it does what you want. One slightly confusing thing is with the tempo/pattern knob - if you turn it full CCW to select pattern 1 then play, it will sound like it has frozen, but it's just moving at a VERY slow tempo (possibly down to 0.6 BPM!) Just whack up the tempo knob and restart to get it moving.
