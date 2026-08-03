
# Backyard Rain Soundscape

A port of the [Backyard Rain Soundscape](https://briandorsey.itch.io/backyard-rain-soundscape) Playdate app to the Music Thing Modular Workshop System Computer. 

Nature soundscape audio. A cozy rain ambience mix for background listening. You control the intensity. This card plays rain ambience which was recorded in my backyard. 

* Use the main knob to adjust rain intensity. (it cross fades between three recordings)
* Never hear the loops: detailed natural recordings of different lengths and slowly crossfaded playback mix. (LFO mixed with main knob.) 
* In synth terms, you could think of it as a noise oscillator sourced from nature. 

*"It's such a cozy little app."* -- my brother

## Reviews, demos, music

* [Mylar Melodies - Music Thing Workshop System modular synth demo.
](https://youtu.be/ABbWmZOtmig?si=bKNxzY5MFJ0kZ6UB&t=1772) (link to the Backyard Rain section of the video)
* [DivKid - Rain as fantastic textures and how to use noise sources](https://youtu.be/D0H_VsJ15go?t=4819&si=7J1yqLwJx2xuIX9x) - several examples of using noise to enhance patches. Decimation, vocoding, resonator & modulation. (link to the Backyard Rain section)
* [johaneklund.io - Playing with the Backyard Rain card on the @musicthingmodular Workshop System](https://www.instagram.com/reel/DMKkotPsItQ/?utm_source=ig_web_copy_link) together with feedback and distortion from a Korg NTS-1. 
* [djr brunstein - Patch Memory #4: Workshop System Lucky Dip (Music Thing Modular)](https://www.youtube.com/watch?v=VFnUbPqJ7lY&t=65s) - exploratory patching video (link to the Backyard Rain section)

## Installation

Download the firmware which matches the size card you have (most are 2 MB cards). Unzip the `uf2` file. Then follow the "How do I write a blank program card?" instructions from the [Computer and Program Card Guide](https://www.musicthing.co.uk/Computer_Program_Cards/). 

> Writing a card takes longer than you might expect. A 16 MB card takes about 4.5 minutes on my computer. Since Backyard Rain uses nearly all the space on both the 2 MB and 16 MB cards, it will likely be longer than other cards you may have written previously.

## Changes

Previous versions are available on the [Releases](https://codeberg.org/briandorsey/mtmws_cards/releases) tab in the repository for this card.

### V1 - initial release

### V2 - Stereo and more

* Stereo! Rain loops are half as long, but they're in stereo now. Patch both outputs and pan to taste -fully wide to match original recordings. After listening for a while, I like these a lot better, and I can't personally hear the loops (especially since we're hearing two loops mixed unless intensity is set to min, center or max). You can still get mono by patching just one audio output. If you want the longer loops back as well, let me know, I can build them too.
* Added three thunder recordings (but only on 16MB cards, sorry!). Triggered by Z switch or Pulse 1.
* Moved intensity CV input from Audio input 1 to CV input 1 where it belongs.
* Fixed LED visualization bug at lowest and highest intensities. Middle LED now goes dark at these intensities and each LED better represents that loop's contribution to the mix (Max brightness at 100%, and dark at 0%).
* Slightly improved audio quality (Raised chip clock speed from 125Mhz to 200MHz (rp2040 official specs were updated). This reduces task switching pressure, reducing sample timing jitter.)
* When powered up or reset, start with intensity at main knob position. No more weird shift after reset if knob position is far from center. (At startup, adjust internal variables to physical knobs more quickly where possible and delay output until data is available where needed.)
* Intensity LFO now starts at zero (no offset) instead of lowest point. 
* No longer displaying LFO level on LED 4. It wasn't very useful, since the value changes very slowly and within a relatively small range. Also... I'm hoping to use the right three LEDs for something fun in the future. 
* Invisible (hopefully) developer changes:
  * Added CPU utilization debug output for both cores.
  * Various optimizations to reduce CPU load, including: pass samples between cores in a block rather than a pair at a time, zerocopy data sharing between cores.
  * Many underlying module updates (embassy framework and other dependencies.)

## Recording info:

* LOM Uši omni microphones, separated by about 1.5m (5ft)
* Cinela Leonard wind protection
* Sony A10 recorder & a small Anker USB power supply
* recorded overnight June 2023, Seattle, WA, USA



