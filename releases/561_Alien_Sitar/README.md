# Alien Sitar 

A sitar-inspired program card with jawari buzz, evolving plucks and twelve sympathetic strings that bloom and ring between notes.

Currently this is a beta version, not sure if the swell is too much, any feedback gratefully received. I need to tidy up the 
source code (add more comments, refactor a few bits to make it easier to follow) so that it can then be open sourced.


# Quick start

Connect Audio1 OUT to your mixer and set switch Z to Middle. Set Jawari and Swell near 65%, String near 80%, then tap Down to pluck. With pitch unpatched, the starting note is D3.

# Controls

Knob	Normal	Hold Down and deliberately move knob
Big / Main	Jawari — bridge buzz	Sympathy — halo level
Left / X	Swell — decay and secondary rise	Octave offset, −3 to +3
Right / Y	String — string tone	Pick — soft / original / sharp
Middle: plucked instrument. Up: external-excitation resonator. Down: one pluck on entry; holding never repeats.

Held edits affect only knobs you move. After release, return an edited knob through its previous normal position to pick up control again. Secondary settings reset on reboot: Sympathy full, octave −1, Pick centre.

# Inputs and outputs

Socket	Input in Middle / Down	Input in Up	Output
Audio1	Jawari CV	Excite melody string	Full instrument
Audio2	Swell CV	Excite sympathetic bank	Halo alone
CV1	Pitch, 1 V/oct	Same	Melody loudness follower
CV2	Bend, ±5 V = ±2 semitones	Same	Halo loudness follower
Pulse1	Pluck on rising edge	Same	5 ms pluck-event pulse
Pulse2	Smooth damping on rising edge	Same	Halo-active gate
Default pitch is 0 V = C3, +1 V = C4; choose octave 0 for 0 V = C4. Pitch changes do not pluck or retune the sympathetic bank. Modulation inputs use ±5 V for ±one full knob range, added and clamped; unpatched inputs add zero. CV outputs are nominally 0–5 V.

