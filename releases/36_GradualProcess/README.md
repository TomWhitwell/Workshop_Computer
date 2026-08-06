# GradualProcess

**James Saunders**

GradualProcess is a CV generator that produces sequences in the style of composers Philip Glass, Steve Reich and Arvo Pärt. It uses simple algorithms to generate material that draw on these composers’ techniques. The card can cycle through multiple modes and more will be added over time.

## ABOUT

GradualProcess is designed to create pitch sequences that are governed by composition techniques of a set of composers for whom an aspect is governed by a process. The original version contains modes for Philip Glass, Steve Reich and Arvo Pärt. 

The Glass mode generates phrases that can be extended or reduced by a short melodic unit and can be doubled at a fifth or played in contrary motion. 

The Reich mode initiates two types of phasing: either a gradual or stepped movement. 

The Pärt mode creates a harmonised line using his tintinnabulation technique harmonising pitches strictly using the tonic triad. 

All three modes generate a new pitch sequence each time, drawing on broadly idiomatic types of material appropriate for each composer.

## GENERAL

The card contains three modes which can be selected by turning off the power, moving the main knob to the correct position, and turning it on again:

<img src="https://github.com/thewatchingeye/Workshop_Computer/blob/main/releases/36_GradualProcess/images/maindial.svg" width="200">

All modes have a play (Z-up), stop (Z-mid) and settings function (Z-hold-down). In settings, the X knob controls the clock divide and the Y knob selects the scale. Each mode may have other settings as indicated below.

<img src="https://github.com/thewatchingeye/Workshop_Computer/blob/main/releases/36_GradualProcess/images/leds_XY.svg" width="400">

Everything works a lot better if you <a href="https://www.musicthing.co.uk/Workshop_System_Calibration/">callibrate the Computer</a> so the tuning tracks accurately.

## MODES

### 1.GLASS / ADDITIVE PROCESS

Glass mode plays a pitch sequence grouped in units of two and three beats, mostly generated through scalic patterns with the occasional leap. 

CV1 plays a melodic line (Music in Similar Motion). Try patching it to both oscillators and tuning the second one a fifth higher (Music in Fifths). 

CV2 plays a tonal inversion (Music in Contrary Motion).

In play mode, tempo is controlled by the main knob, modified by the clock divider (Z-down+X). 

The Y knob is used to add or subtract units in play mode. Begin at around 1200. To add a unit, turn it quickly clockwise and return to 1200. To subtract a unit, turn it quickly counterclockwise and return to 1200. Alternatively playing a pulse into Pulse In 2 also adds a unit and a pulse into Pulse In 2 removes a unit. 

The LEDs show how many units are present in the loop (1-12).

### 2.REICH / PHASING

Reich mode outputs two identical voices which move out of phase with each other, either gradually or in step, generated as groups of two or three pitches with a one beat rest to separate them.

CV1 plays a melodic line which is newly generated each time the sequence is played. In settings mode (hold Z down), the main knob controls the length of the phrase from 4-20 beats.CV2 doubles the line initially, but can be shifted against the CV1 voice.

In play mode, tempo is controlled by the main knob, modified by the clock divider (Z-down+X). 

The Y knob is used to shift the second voice by one beat in either direction in play mode. Begin at around 1200. To shift the voice one beat later, turn it quickly clockwise and return to 1200. To shift the voice one beat earlier, turn it quickly counterclockwise and return to 1200. The central position holds the current relationship.

The X knob is used to gradually phase the second voice by one beat in either direction in play mode. Begin at around 1200. To begin phasing the voice one beat later, turn it quickly clockwise and return to 1200. It will begin phasing and reach the next shifted position after c.8-16 repeats. To gradually phase the voice one beat earlier, turn it quickly counterclockwise and return to 1200. It will begin phasing and reach the next shifted position after c.8-16 repeats. The central position holds the current relationship.

The LEDs show how many beats the voice has shifted (1-12).


### 3.PÄRT / TINTINNABULATION

Pärt mode outputs two voices, the melodic voice (m-voice) and the tintinnabulation voice (t-voice) which harmonises it in rhythmic unison. The m-voice moves in step movements with occasional leaps and held notes, and the t-voice adds a harmony pitch from the tonic triad following Pärt’s rules. This mode works best with a slow tempo.

CV1 plays a melodic line which is newly generated each time the sequence is played. CV2 plays the harmonising line. In settings mode (hold Z down), the main knob controls the harmonisation type. From fully counterclockwise and moving in a clockwise direction it selects in order: inferior second position / inferior first position / alternating second position / alternating first position / superior first position / superior second position.

In play mode, tempo is controlled by the main knob, modified by the clock divider (Z-down+X). 

The X and Y knobs only function in settings mode, controlling the clock divider (X) and scale selection (Y) as with other modes. 

Pulse Out 1 and 2 send gates sustained for each note’s duration, with a brief retrigger on every new note.
