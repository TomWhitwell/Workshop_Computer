# Talker

This is an early proof of concept, which simply babbles random numbers. There is no way yet to control the flow of numbers.

This proof of concept uses
- a (somewhat modified and extended) version of the [TalkiePCM](https://github.com/pschatzmann/TalkiePCM) library, which is a software implementation of Texas Instruments [Linear Predictive Coding](https://en.wikipedia.org/wiki/Linear_predictive_coding) (LPC) speech chips from the 70s/80s.
- The ComputerCard framework from this Workshop_Computer repository (Demonstrations+HelloWorlds/PicoSDK/ComputerCard/)

LPC is a method of compressing audio recordings of speech, by modelling the audio signal as the result of an 'exciter' (a pitched or noise-like sound source) being passed through a bank of bandpass filters. This description allows the signal to be compressed efficiently, because the parameters describing the exciter and filters change relatively slowly (~40Hz) compared to the audio sample rate.

While the original goal of LPC was to reproduce the original recording as accurately as possible (at much reduced data rate), this Talker card is designed for more creative/musical use. As such, the speed and pitch of the speech can be controlled, and jack connections allow the exciter and/or filter bank to be modified (or replaced entirely) with external processing.

### Controls

- Switch:
  - Up = continuous
  - Middle = off
  - Down = single word
- Pitch is controlled by the Main knob + CV in 1 (attenuverted by knob X)
- Speed of babbling: Knob Y + CV in 2

### Output 

- Audio out 1: Speech output
- Audio out 2: the pitched and noise components of the LPC exciter

### Input

- Audio in 1, if plugged in, replaces the pitched part (only) of the LPC exciter
- CV out 1: exciter amplitude output
- CV out 2: exciter pitch output




---
### License

This proof-of-concept is licensed under the GPL-3.0, due to its use of the TalkiePCM library.


---
Author: [Chris Johnson](https://github.com/chrisgjohnson)

TalkiePCM is based on [Talkie](https://github.com/going-digital/Talkie) by Peter Knight 
