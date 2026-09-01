// TRACT8 - cross-core state.
//
// Read/written by:
//   Core 0 - VoderCard::ProcessSample() at 48 kHz, in interrupt context
//   Core 1 - the TinyUSB host task pump, whenever the 8mu sends something
//
// Every field is a single byte or a naturally-aligned 32-bit word, so reads
// and writes are atomic on the Cortex-M0+ and no mutex is needed. `volatile`
// on the global instance forces a re-read on every access so a change made on
// one core becomes visible on the other.
//
// Direction of writes:
//   band_gain, pitch_bend, gate_voiced, gate_noise, freeze
//                     - written by Core 1 (MIDI) and Core 0 (panel), last
//                       writer wins. See the note on that in main.cpp.
//   plosive_count     - incremented by Core 1 only, read by Core 0
//   midi_connected    - written by Core 1 only, read by Core 0 for the LED

#ifndef TRACT8_SHARED_H_
#define TRACT8_SHARED_H_

#include <stdint.h>

struct VoderState {
  // --- the five mapped 8mu faders -------------------------------------
  //
  // Fader 1 (CC 34)  OPENNESS   vowel cube, close <-> open
  // Fader 8 (CC 41)  FRONT      vowel cube, back  <-> front
  // Fader 3 (CC 36)  BREATH     buzz <-> noise
  // Fader 4 (CC 37)  PITCH      F0
  // Fader 5 (CC 38)  BRIGHT     spectral tilt
  //
  // The vowel axes are on the OUTERMOST faders, 1 and 8, so the two
  // controls that get played constantly span the hand rather than needing
  // the same finger twice. Faders 2, 6 and 7 are unassigned on purpose.
  int32_t openness;      // Q15
  int32_t front;         // Q15
  int32_t breath;        // Q15, 0 = all buzz, 32767 = all noise
  int32_t pitch;         // Q15, mapped to F0 in main.cpp
  int32_t bright;        // Q15, 16384 = flat

  // Click (plosive) controls. Level balances the burst against the voice -
  // it was fixed at full and too loud. Decay runs from a short consonant
  // hit to a steady sustained tone at the top.
  int32_t click_level;
  int32_t click_decay;

  // Third vowel axis: lip rounding, from the left/right tilt. 0 = spread,
  // 32767 = rounded. This is what makes OH reachable - see vowels.h.
  int32_t round;

  // One flag per control, set the first time the 8mu sends it. Until then
  // the panel keeps that control. A controller plugged in but untouched
  // must never seize a knob the player has a hand on.
  uint8_t openness_from_midi;
  uint8_t front_from_midi;
  uint8_t breath_from_midi;
  uint8_t pitch_from_midi;
  uint8_t bright_from_midi;
  uint8_t click_level_from_midi;
  uint8_t click_decay_from_midi;
  uint8_t round_from_midi;

  // --- accelerometer ---------------------------------------------------
  // Front/back tilt is VOLUME: the one control that wants to be a gesture
  // rather than a setting. Rests at unity so a controller lying flat is at
  // full volume.
  int32_t volume;        // Q15, 32767 = unity - fader plus tilt

  // The fader's contribution alone, without the tilt. BABBLE uses this so
  // that holding the controller at an angle cannot silence the card in
  // the mode whose assumption is that you want sound.
  int32_t volume_fader;
  uint8_t volume_from_midi;

  // Set while the 8mu is upside down (CC 49). Hard mute - turning the
  // controller over is an unmistakable, deliberate gesture, and having a
  // panic stop that needs no aim is worth a gesture nobody makes by
  // accident.
  uint8_t muted;

  // --- band gains handed to the filter bank ----------------------------
  // Computed in main.cpp from the vowel cube. Nothing else writes it; it
  // lives here so the LEDs can display it.
  int32_t band_gain[8];

  // --- buttons ----------------------------------------------------------
  // Button 1 is a TRIGGER: it fires the click and holds it open while
  // held, so the card can be played as a percussive voice.
  uint8_t click_gate;

  // Button 1 mutes while held. A momentary mute is worth a button on a
  // card that can drone indefinitely, where gating the buzz separately
  // from the noise was not - the breath control already covers that, and
  // did it better.
  uint8_t midi_mute;

  // Button 2 acts exactly as a gate patched into Pulse In 2: it opens the
  // voice, and in BABBLE it drives the syllable chatter. A held button and
  // a held gate are the same thing to everything downstream, which is why
  // this is one flag OR'd into the existing path rather than a parallel
  // mechanism - the alternative would be two ways to gate the card that
  // could disagree.
  uint8_t midi_gate;

  uint8_t freeze;
  uint8_t midi_connected;

  // Buttons A and D add a little breath while held, on top of whatever
  // fader 3 is set to. Both already do something (gate the buzz, latch
  // freeze) and neither used its held state for anything, so this is free
  // expression: a voiced sound with a whisper of noise under it reads as
  // breathy rather than buzzy.
  uint8_t breath_button_a;
  uint8_t breath_button_d;

  // Counted, not flagged - a flag set on Core 1 can be missed entirely or
  // fired twice depending on how the ISR interleaves with it.
  uint32_t plosive_count;
};

extern volatile VoderState g_state;

#endif  // TRACT8_SHARED_H_
