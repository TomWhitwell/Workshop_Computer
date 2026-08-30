// TRACT8 - Music Thing 8mu dispatch.
//
// Decodes the 8mu factory MIDI mapping. Nothing needs configuring in the
// 8mu web editor: plug it into the front USB-C jack and play.
//
// The factory mapping, confirmed against musicthing.co.uk/8mu.html:
//
//   CC 34-41   faders 1-8, left to right with the cable on the right
//   CC 42-49   accelerometer gestures:
//                42 lift front   43 lift back
//                44 lift left    45 lift right
//                46 rotate CW    47 rotate CCW
//                48 not inverted 49 inverted
//   Notes      the four buttons, C2/C3/C4/C5 (36/48/60/72) out of the box
//
// Mapped here as:
//
//   CC 34  fader 1   OPENNESS   vowel cube, close <-> open
//   CC 41  fader 8   FRONT      vowel cube, back  <-> front
//   CC 36  fader 3   BREATH     buzz <-> noise
//   CC 37  fader 4   PITCH      F0, 50..500 Hz
//   CC 38  fader 5   BRIGHT     spectral tilt
//   CC 35  fader 2   CLICK DECAY  short hit -> steady tone
//   CC 39  fader 6   CLICK LEVEL
//   CC 40  fader 7   VOLUME     base level
//   CC 42/43         VOLUME TRIM  lift front/back, bipolar, adds to it
//   CC 44/45         ROUND      lift left/right, bipolar
//
// THE ACCELEROMETER CCs ARE GESTURE MAGNITUDES, NOT BIPOLAR AXES. Each
// direction is its own controller reporting how much of that gesture is
// happening, and a controller lying flat does not necessarily send 0 on
// any of them. Nothing here may assume a resting value: the volume and
// round axes calibrate themselves from the first message they see. Two
// hardware rounds were lost to assuming otherwise - see midi8mu.cpp.
//   CC 49            MUTE       LOW value = turned over = muted
//   CC 46, 47        unassigned
//   Note 36 (C2)     MUTE while held - silences the card
//   Note 48 (C3)     unvoiced noise gate
//   Note 60 (C4)     plosive burst
//   Note 72 (C5)     formant freeze, and adds breath
//
// FADERS 1 AND 8 CARRY THE VOWEL, not 1 and 2. They are the outermost
// faders, so the two axes that get played constantly sit under a thumb and
// a little finger with the whole hand spanning the device. Adjacent faders
// need the same finger twice or a wrist move. This was reported from
// playing, and it is the kind of thing only playing tells you.
//
// No fader is a single filter band. Five controls that each mean something
// beat eight that must be operated as a chord - the version that put one
// band on each fader was unplayable, and that verdict came from playing it.
//
// Everything else - other CCs, other notes, pitch bend, sysex, program
// change, clock - is silently dropped. Channel-agnostic.

#ifndef TRACT8_MIDI8MU_H_
#define TRACT8_MIDI8MU_H_

#include <stdint.h>

namespace tract8 {

// Faders. 1 and 8 are the vowel axes - the outermost pair, for the reason
// in the header comment above.
static constexpr uint8_t kCcOpenness = 34;   // fader 1
static constexpr uint8_t kCcFront = 41;      // fader 8
static constexpr uint8_t kCcBreath = 36;     // fader 3
static constexpr uint8_t kCcPitch = 37;      // fader 4
static constexpr uint8_t kCcClickDecay = 35;  // fader 2
static constexpr uint8_t kCcBright = 38;      // fader 5
static constexpr uint8_t kCcClickLevel = 39;  // fader 6
static constexpr uint8_t kCcVolume = 40;      // fader 7

// Accelerometer.
//
// THESE ARE *LIFT* GESTURES. Each one reads 0 when the device is level and
// rises as that side is lifted, so a level 8mu sends 0 on ALL FOUR of
// them. They are NOT a complementary pair adding to 127, and there is no
// centre detent at 64.
//
// Getting this wrong is what made the volume feel broken: treating 64 as
// the neutral point meant full volume happened only at some specific
// half-lifted angle, with the level position silent. Reported as "it feels
// like only a very specific angle has volume", which is exactly what the
// arithmetic predicts.
//
// The real axis is the DIFFERENCE of a pair:
//
//     axis = lift_one - lift_other        range -127 .. +127
//
// which is 0 when level, positive lifting one way and negative the other.
// That is genuinely bipolar, and both halves must be read to get it -
// unlike a complementary pair, here each half carries real information.
// These name the PHYSICAL gesture, matching the 8mu documentation. The
// volume sense is inverted where the axis is computed, not by lying about
// which CC is which - see UpdateVolume() in midi8mu.cpp. Lifting the BACK
// swells and lifting the FRONT ducks, which is the way round it was asked
// for after playing: the wrist drops the front of the device to pull a
// phrase back.
static constexpr uint8_t kCcLiftFront = 42;
static constexpr uint8_t kCcLiftBack = 43;
static constexpr uint8_t kCcLiftLeft = 44;
static constexpr uint8_t kCcLiftRight = 45;

// Mute is DISABLED for now, at the player's request: it was confusing the
// diagnosis of the tilt behaviour. The CC numbers are kept so it is
// obvious what to re-enable, and the polarity note is kept with them
// because it took four attempts to establish.
//
// CC 49 reads HIGH while the device is the RIGHT WAY UP and falls when it
// is turned over, despite being labelled "inverted" in the 8mu docs, so
// the mute triggered on a LOW value. CC 48 is its partner.
static constexpr uint8_t kCcNotInverted = 48;
static constexpr uint8_t kCcInverted = 49;

// How much the tilt can move the volume either side of the fader setting,
// Q15. A full lift adds or subtracts this much, so the fader sets the
// centre of a window the wrist can swing within.
static constexpr int32_t kTiltVolumeRange = 32767;

// Button notes.
static constexpr uint8_t kNoteMute = 36;     // C2, button 1
static constexpr uint8_t kNoteGate = 48;     // C3, button 2
static constexpr uint8_t kNotePlosive = 60;  // C4
static constexpr uint8_t kNoteFreeze = 72;   // C5

// How much breath buttons A and D add while held, Q15. A whisper of noise
// under a voiced sound is what makes it breathy rather than buzzy; enough
// to hear, not enough to swamp the buzz.
// 4500 of 32768, so one button adds about 14% noise to whatever the
// breath fader is set to and both together add 27%.
//
// It was 9000 and reported as too loud - meaning too HISSY, not louder:
// adding noise actually drops the level about 2.4 dB, because the
// crossfade takes buzz away faster than noise replaces it through a
// bandpass bank. At 27% for a single button the hiss stopped reading as
// breath on a voice and started reading as a separate noise source
// mixed in over the top.
static constexpr int32_t kButtonBreath = 4500;

// Feed one decoded MIDI channel-voice message in. Status is the full byte
// including channel; the channel is ignored.
void Midi8muMessage(uint8_t status, uint8_t d1, uint8_t d2);

}  // namespace tract8

#endif  // TRACT8_MIDI8MU_H_
