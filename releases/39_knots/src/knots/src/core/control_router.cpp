#include "knots/src/core/control_router.h"

namespace mtws {

namespace {
constexpr uint32_t kBasePhaseInc10Hz = 894785U;  // 10 * 2^32 / 48k
constexpr uint32_t kMaxPhaseInc10kHz = 894784853U;  // 10000 * 2^32 / 48k
constexpr uint32_t kMaxU12 = 4095U;
constexpr uint32_t kUnityQ12 = 4096U;
constexpr int32_t kPitchCVScaleNumerator = 6;
constexpr int32_t kPitchCVScaleDenominator = 5;
// ComputerCard audio/CV signals span approximately +/-6 V over -2048..2047.
// The modulation and VCA paths should reach full scale by +5 V, so use the
// nearest signed input code for 2047 * 5 / 6 as the practical patch range.
constexpr int32_t kFiveVoltInputCode = 1706;

// 2^(x) for x in [0,1] on 1/128 grid, Q16 format.
constexpr uint32_t kExp2Q16[129] = {
     65536,  65892,  66250,  66609,  66971,  67335,  67700,  68068,  68438,  68809,  69183,  69558,  69936,  70316,  70698,  71082,
     71468,  71856,  72246,  72638,  73032,  73429,  73828,  74229,  74632,  75037,  75444,  75854,  76266,  76680,  77096,  77515,
     77936,  78359,  78785,  79212,  79642,  80075,  80510,  80947,  81386,  81828,  82273,  82719,  83169,  83620,  84074,  84531,
     84990,  85451,  85915,  86382,  86851,  87322,  87796,  88273,  88752,  89234,  89719,  90206,  90696,  91188,  91684,  92181,
     92682,  93185,  93691,  94200,  94711,  95226,  95743,  96263,  96785,  97311,  97839,  98370,  98905,  99442,  99982, 100524,
    101070, 101619, 102171, 102726, 103283, 103844, 104408, 104975, 105545, 106118, 106694, 107274, 107856, 108442, 109031, 109623,
    110218, 110816, 111418, 112023, 112631, 113243, 113858, 114476, 115098, 115723, 116351, 116983, 117618, 118257, 118899, 119544,
    120194, 120846, 121502, 122162, 122825, 123492, 124163, 124837, 125515, 126197, 126882, 127571, 128263, 128960, 129660, 130364,
    131072,
};

// 2^(n/12) for n=0..11 in Q16.
constexpr uint32_t kSemitoneQ16[12] = {
    65536, 69433, 73561, 77936, 82575, 87499, 92729, 98289, 104202, 110496, 117202, 124354,
};

inline uint16_t ClampU12(int32_t v) {
  if (v < 0) return 0;
  if (v > int32_t(kMaxU12)) return kMaxU12;
  return uint16_t(v);
}

// Converts the raw CV1 board code into the pitch-code delta used by Main.
// The card exposes about 12 V over the full CV input span, while the pitch
// control covers 10 octaves over the same 12-bit code range, so we scale the
// raw delta by 6/5. This makes 1 V land on 1 octave instead of the current
// 10-semitone response. A float slope or per-board calibration would be more
// flexible, but this integer ratio is cheaper and keeps the control path simple.
constexpr int32_t MapPitchCVInputToPitchCodeDelta(int32_t input_code) {
  if (input_code >= 0) {
    return (input_code * kPitchCVScaleNumerator + (kPitchCVScaleDenominator / 2)) /
           kPitchCVScaleDenominator;
  }
  return -(((-input_code) * kPitchCVScaleNumerator + (kPitchCVScaleDenominator / 2)) /
           kPitchCVScaleDenominator);
}

static_assert(MapPitchCVInputToPitchCodeDelta(0) == 0, "Pitch CV zero should stay centered.");
static_assert(MapPitchCVInputToPitchCodeDelta(1706) == 2047, "Positive pitch CV scale drifted.");
static_assert(MapPitchCVInputToPitchCodeDelta(-1706) == -2047, "Negative pitch CV scale drifted.");

// Clamps one bipolar board-domain input code into the intended -5 V..+5 V
// modulation range so hotter sources saturate instead of overdriving the macro.
inline int32_t ClampBipolarFiveVoltInput(int32_t input_code) {
  if (input_code < -kFiveVoltInputCode) return -kFiveVoltInputCode;
  if (input_code > kFiveVoltInputCode) return kFiveVoltInputCode;
  return input_code;
}

// Maps a bipolar -5 V..+5 V input code into the unsigned macro domain.
// Endpoints are -5 V -> 0, 0 V -> ~2048, +5 V -> 4095.
inline uint16_t MapBipolarFiveVoltInputToU12(int32_t input_code) {
  const uint32_t shifted_code = uint32_t(ClampBipolarFiveVoltInput(input_code) + kFiveVoltInputCode);
  return uint16_t((shifted_code * kMaxU12 + uint32_t(kFiveVoltInputCode)) / (2U * uint32_t(kFiveVoltInputCode)));
}

// Applies a 0..4095 attenuation amount to a 0..4095 source and rounds to the
// nearest output code so full-scale attenuation preserves full-scale inputs.
inline uint16_t ApplyU12Attenuation(uint16_t source_u12, uint16_t amount_u12) {
  return uint16_t((uint32_t(source_u12) * uint32_t(amount_u12) + (kMaxU12 / 2U)) / kMaxU12);
}

// Maps a unipolar 0..+5 V control input into Q12 gain for the global VCA.
// Inputs at or below 0 V keep the VCA closed; +5 V and above reach unity gain.
inline uint16_t MapPositiveFiveVoltInputToQ12(int32_t input_code) {
  if (input_code < 0) input_code = 0;
  if (input_code > kFiveVoltInputCode) input_code = kFiveVoltInputCode;
  return uint16_t((uint32_t(input_code) * kUnityQ12 + (kFiveVoltInputCode / 2)) / kFiveVoltInputCode);
}

}  // namespace

uint32_t ControlRouter::BasePhaseIncrementFromPitchCode(uint32_t pitch_code) {
  if (pitch_code > 4095U) pitch_code = 4095U;

  uint32_t pos = (pitch_code * 5122U) >> 12;  // 10 octaves, 512 steps/oct
  if (pos > 5120U) pos = 5120U;

  uint32_t octave_int = pos >> 9;
  uint32_t sub = pos & 0x1FFU;

  uint32_t inc = kBasePhaseInc10Hz << octave_int;

  uint32_t idx = sub >> 2;
  uint32_t frac = sub & 0x3U;
  uint32_t m1 = kExp2Q16[idx];
  uint32_t m2 = kExp2Q16[idx + 1U];
  uint32_t mult = m1 + (((m2 - m1) * frac) >> 2);

  return uint32_t((uint64_t(inc) * mult) >> 16);
}

uint32_t ControlRouter::ApplySemitoneToPhaseInc(uint32_t base_inc, int semitone) {
  if (base_inc == 0 || semitone == 0) return base_inc;

  if (semitone > 96) semitone = 96;
  if (semitone < -96) semitone = -96;

  int octave = semitone / 12;
  int rem = semitone % 12;
  if (rem < 0) {
    rem += 12;
    octave -= 1;
  }

  uint64_t scaled = (uint64_t(base_inc) * kSemitoneQ16[rem]) >> 16;

  if (octave > 0) {
    if (octave >= 16) {
      return 0x7FFFFFFFU;
    }
    scaled <<= octave;
  } else if (octave < 0) {
    int down = -octave;
    if (down >= 63) {
      scaled = 0;
    } else {
      scaled >>= down;
    }
  }

  if (scaled > 0x7FFFFFFFULL) return 0x7FFFFFFFU;
  return uint32_t(scaled);
}

GlobalControlFrame ControlRouter::BuildGlobalFrame(const UISnapshot& ui, const MidiState& midi) {
  GlobalControlFrame out{};

  out.selected_slot = (ui.selected_slot < kNumOscillatorSlots) ? ui.selected_slot : 0;

  int32_t pitch_local = int32_t(ui.knob_main) + MapPitchCVInputToPitchCodeDelta(int32_t(ui.cv1));
  uint16_t pitch_code = ClampU12(pitch_local);

  uint32_t base_inc = BasePhaseIncrementFromPitchCode(pitch_code);
  // Hold the most recent MIDI note through note-off so internal pitch matches
  // the CV1 note output during envelope release instead of snapping back to the
  // panel tuning as soon as the gate drops.
  int semitone = int(midi.last_note) - 60;
  out.pitch_inc = ApplySemitoneToPhaseInc(base_inc, semitone);
  if (out.pitch_inc < kBasePhaseInc10Hz) out.pitch_inc = kBasePhaseInc10Hz;
  if (out.pitch_inc > kMaxPhaseInc10kHz) out.pitch_inc = kMaxPhaseInc10kHz;

  uint16_t x_local = ui.knob_x;
  if (ui.audio1_connected) {
    x_local = ApplyU12Attenuation(MapBipolarFiveVoltInputToU12(ui.audio1), ui.knob_x);
  }

  uint16_t y_local = ui.knob_y;
  if (ui.audio2_connected) {
    y_local = ApplyU12Attenuation(MapBipolarFiveVoltInputToU12(ui.audio2), ui.knob_y);
  }

  out.macro_x = x_local;
  out.macro_y = y_local;

  out.mode_alt = ui.pulse1_connected ? ui.pulse1_high : ui.panel_alt_latched;

  if (ui.cv2_connected) {
    out.vca_gain_q12 = MapPositiveFiveVoltInputToQ12(ui.cv2);
  } else {
    out.vca_gain_q12 = kUnityQ12;
  }

  out.midi_note_active = midi.note_active;
  out.current_midi_note = midi.current_note;
  out.last_midi_note = midi.last_note;
  out.midi_cc74_value = midi.cc74_value;
  out.note_on_counter = midi.note_on_counter;
  out.midi_clock_tick_count = midi.clock_tick_count;

  return out;
}

}  // namespace mtws
