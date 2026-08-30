// TRACT8 - Music Thing 8mu dispatch. See midi8mu.h for the mapping table.

#include "midi8mu.h"
#include "shared.h"

namespace tract8 {


// A 7-bit CC scaled to Q15. 127 gives 32512, which is 0.07 dB shy of full
// scale - close enough to read as fully open, and it avoids a branch to
// special-case the top value.
static inline int32_t CcToQ15(uint8_t v) { return (int32_t)v << 8; }

// Volume and rounding from the accelerometer.
//
// THE LESSON THAT COST TWO HARDWARE ROUNDS: the 8mu's accelerometer CCs
// are GESTURE MAGNITUDES, not a bipolar axis with a known resting value.
// "Lift front" and "lift back" are two independent controllers that each
// report how much of that gesture is happening, and a controller lying
// flat does not necessarily send 0 on either of them.
//
// The first version computed
//
//     volume = 32767 - lift_back + lift_front
//
// which assumed lift_back rests at 0. If it rests anywhere above zero the
// card is quiet or silent from the moment it boots, and - this is the part
// that made it look broken rather than wrong - the volume is already
// clamped at the bottom, so moving the controller appears to do nothing at
// all. Reported from hardware as "tilt isn't doing volume now".
//
// The fix is to assume nothing about resting values and CALIBRATE. The
// first message on each axis establishes that axis's rest point; from then
// on only the DEVIATION from rest is used. That works whatever the 8mu
// happens to send when level, and it needs no configuration.
// The accelerometer gestures are LIFTS: each reads 0 when the device is
// level and rises as that side is lifted. A level 8mu sends 0 on all four.
//
// So each physical axis is the DIFFERENCE of its pair, -127..+127, zero
// when level. Both halves are read, because here each carries real
// information - unlike a complementary pair, where one is redundant.
static int32_t s_lift_front = 0;
static int32_t s_lift_back = 0;
static int32_t s_lift_left = 0;
static int32_t s_lift_right = 0;

// Volume from fader 7, before the tilt is added. Held separately so the
// two contributions can be recombined whenever either changes.
static int32_t s_vol_fader = 32767;

// Signed tilt, Q15, from a pair of lift gestures. Squared to keep the
// response gentle around level, where the wrist naturally sits, and to put
// the expressive part of the travel at a definite lift.
static int32_t TiltSigned(int32_t a, int32_t b) {
  const int32_t d = a - b;                       // -127 .. +127
  const int32_t mag = d < 0 ? -d : d;
  int32_t q = (mag * mag * 32767) / (127 * 127);  // 0 .. 32767, squared
  return d < 0 ? -q : q;
}

// Volume is the fader plus the front/back tilt, which swings either side
// of it. The fader sets where the wrist's neutral position sits, and the
// tilt is worth a full scale in each direction from there - so a fader at
// half gives a full swell above and a full duck below, while a fader at
// the top is loud until the back is lifted.
//
// This replaces a centre-detent design that assumed the axis rested at 64.
// It does not: it rests at 0, so full volume happened only at a specific
// half-lifted angle and the level position was silent.
static void UpdateVolume() {
  // BACK minus FRONT, not the other way round: lifting the back swells
  // and lifting the front ducks. Inverted here rather than by swapping the
  // CC constants, so the names keep telling the truth about which physical
  // gesture is which.
  int32_t v = s_vol_fader +
              ((TiltSigned(s_lift_back, s_lift_front) * kTiltVolumeRange) >>
               15);
  if (v < 0) v = 0;
  if (v > 32767) v = 32767;
  g_state.volume = v;
  g_state.volume_from_midi = 1;
}

// Rounding, the vowel cube third axis. Bipolar about level: lifting either
// side moves toward the rounded (OO) face of the cube, so direction does
// not matter, only how far.
static void UpdateRound() {
  const int32_t t = TiltSigned(s_lift_left, s_lift_right);
  g_state.round = t < 0 ? -t : t;
  g_state.round_from_midi = 1;
}

static void HandleCc(uint8_t cc, uint8_t v) {
  switch (cc) {
    case kCcOpenness:
      // Frozen formants ignore the vowel controls; that is what freeze is.
      if (!g_state.freeze) {
        g_state.openness = CcToQ15(v);
        g_state.openness_from_midi = 1;
      }
      return;

    case kCcFront:
      if (!g_state.freeze) {
        g_state.front = CcToQ15(v);
        g_state.front_from_midi = 1;
      }
      return;

    case kCcBreath:
      // Linear, unlike nothing else here: breath is a balance, so the
      // middle of the fader should be half buzz and half noise.
      g_state.breath = CcToQ15(v);
      g_state.breath_from_midi = 1;
      return;

    case kCcPitch:
      g_state.pitch = CcToQ15(v);
      g_state.pitch_from_midi = 1;
      return;

    case kCcClickDecay:
      g_state.click_decay = CcToQ15(v);
      g_state.click_decay_from_midi = 1;
      return;

    case kCcClickLevel:
      g_state.click_level = CcToQ15(v);
      g_state.click_level_from_midi = 1;
      return;

    case kCcBright:
      g_state.bright = CcToQ15(v);
      g_state.bright_from_midi = 1;
      return;

    case kCcVolume:
      // Fader 7 sets the base volume; the tilt swings around it. The
      // fader's own value is published separately so BABBLE can use it
      // without the tilt - see volume_fader in shared.h.
      s_vol_fader = CcToQ15(v);
      g_state.volume_fader = s_vol_fader;
      UpdateVolume();
      return;

    case kCcLiftFront:
      s_lift_front = v;
      UpdateVolume();
      return;

    case kCcLiftBack:
      s_lift_back = v;
      UpdateVolume();
      return;

    case kCcLiftLeft:
      if (g_state.freeze) return;
      s_lift_left = v;
      UpdateRound();
      return;

    case kCcLiftRight:
      if (g_state.freeze) return;
      s_lift_right = v;
      UpdateRound();
      return;

    case kCcInverted:
    case kCcNotInverted:
      // Mute disabled for now, at the player's request - it was confusing
      // the diagnosis of the tilt behaviour. See midi8mu.h.
      return;

    default:
      // Faders 2 and 6, the rotate gestures, everything else: dropped.
      return;
  }
}

static void HandleNoteOff(uint8_t note) {
  if (note == kNoteMute) {
    g_state.midi_mute = 0;
    g_state.breath_button_a = 0;
  } else if (note == kNoteGate) {
    g_state.midi_gate = 0;
  } else if (note == kNoteFreeze) {
    g_state.freeze = 0;
    g_state.breath_button_d = 0;
  }
}

static void HandleNoteOn(uint8_t note, uint8_t vel) {
  // A note-on with velocity 0 is the running-status note-off.
  if (vel == 0) {
    HandleNoteOff(note);
    return;
  }

  switch (note) {
    case kNoteMute:
      // Button 1 MUTES while held.
      //
      // It used to gate the voiced buzz, which was useless: the breath
      // control already sets the buzz/noise balance and does it better,
      // so the button duplicated a knob nobody needed duplicated. A
      // momentary mute is worth a button on a card that drones
      // indefinitely - it is the one thing you cannot do with a knob
      // without losing your place.
      g_state.midi_mute = 1;
      g_state.breath_button_a = 1;
      break;

    case kNoteGate:
      // Button 2 is a GATE, indistinguishable from one patched into
      // Pulse In 2.
      //
      // It briefly drew a random voice instead, which was removed after
      // it jumped pitch by up to 1.6 octaves per press and the 8mu was
      // reported hanging while it was in use. A gate is the better use of
      // the button anyway: it is the one thing that makes the card speak
      // on demand without a cable, and in BABBLE it starts the chatter.
      g_state.midi_gate = 1;
      break;

    case kNotePlosive:
      g_state.plosive_count++;
      break;

    case kNoteFreeze:
      // Button 4 freezes the formants WHILE HELD, and it used to latch.
      //
      // The latch was reported as the card locking up, and that is a fair
      // reading of what it did: freeze blocks openness, front, rounding
      // and the panel morph all at once, so a frozen card looks exactly
      // like a card that has stopped working. With nothing on the panel
      // saying otherwise there was no way to tell the difference.
      //
      // Momentary makes cause and effect obvious - frozen while your
      // finger is down, not otherwise - and every LED now goes to half
      // brightness while it is held, so the state is unmistakable even if
      // the button sticks. Anything worth holding indefinitely can be
      // held.
      g_state.freeze = 1;
      g_state.breath_button_d = 1;
      break;

    default:
      break;
  }
}

void Midi8muMessage(uint8_t status, uint8_t d1, uint8_t d2) {
  // Mask off the channel: the 8mu can be set to any of them, and a card
  // that only listened to channel 1 would look broken to anyone who had
  // changed it.
  switch (status & 0xF0) {
    case 0xB0:
      HandleCc(d1 & 0x7F, d2 & 0x7F);
      break;
    case 0x90:
      HandleNoteOn(d1 & 0x7F, d2 & 0x7F);
      break;
    case 0x80:
      HandleNoteOff(d1 & 0x7F);
      break;
    default:
      // Pitch bend, aftertouch, program change, sysex, clock: dropped.
      break;
  }
}

}  // namespace tract8
