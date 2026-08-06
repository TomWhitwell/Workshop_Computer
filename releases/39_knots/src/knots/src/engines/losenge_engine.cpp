#include "knots/src/engines/losenge_engine.h"
#include "pico.h"

namespace mtws {

namespace {
constexpr uint8_t kNumOutputs = 2;
constexpr uint8_t kNumFormants = 3;
constexpr uint8_t kNumVowelAnchors = 5;

// Converts knob domain 0..4095 into interpolation-friendly Q12 0..4096.
inline uint16_t ToU12Q12(uint16_t v) { return (v >= 4095U) ? 4096U : v; }

// Holds the first three formants for one vowel plus their relative amplitudes.
// These anchors mirror the solo losenge implementation so standalone and mtws
// share the same vocal geometry.
struct VowelAnchor {
  uint16_t formant_hz[kNumFormants];
  uint16_t formant_gain_q12[kNumFormants];
};

// Male anchor path derived from the supplied vocal-formant reference image.
constexpr VowelAnchor kMaleAnchors[kNumVowelAnchors] = {
    {{609U, 1000U, 2450U}, {4096U, 2052U, 1028U}},  // A
    {{400U, 1700U, 2300U}, {4096U, 1454U, 1630U}},  // E
    {{238U, 1741U, 2450U}, {4096U, 410U, 648U}},    // IY
    {{325U, 700U, 2550U}, {4096U, 1028U, 205U}},    // O
    {{415U, 1400U, 2200U}, {4096U, 1028U, 648U}},   // U
};

// Alt-mode anchor path using the male F2/F3/F4 formants instead of F1/F2/F3.
// Dropping F1 removes the low-frequency chest of the vowel, producing a
// brighter, thinner, more metallic character. F4 values are ~3300-3500 Hz
// (typical adult male range from published phonetics data). The vowel identity
// is preserved because F2/F3 ratios -- the primary cue -- are unchanged.
// F4 gains are reduced since the fourth formant is naturally quieter.
constexpr VowelAnchor kUpperAnchors[kNumVowelAnchors] = {
    {{1000U, 2450U, 3300U}, {4096U, 2052U, 1028U}},  // A
    {{1700U, 2300U, 3500U}, {4096U, 1454U, 1028U}},  // E
    {{1741U, 2450U, 3300U}, {4096U, 410U, 410U}},    // IY
    {{700U, 2550U, 3400U}, {4096U, 1028U, 205U}},    // O
    {{1400U, 2200U, 3300U}, {4096U, 1028U, 410U}},   // U
};

// Interpolates across the vowel path so X moves through named vowels instead of
// arbitrary formant sweeps. Endpoint handling forces 4096 to the last anchor.
VowelAnchor InterpolateVowelPath(const VowelAnchor (&anchors)[kNumVowelAnchors], uint16_t x_q12) {
  uint32_t scaled = uint32_t(x_q12) * uint32_t(kNumVowelAnchors - 1U);
  uint32_t index = scaled >> 12;
  uint16_t frac = uint16_t(scaled & 0x0FFFU);

  if (index >= uint32_t(kNumVowelAnchors - 1U)) {
    index = kNumVowelAnchors - 2U;
    frac = 4096U;
  }

  VowelAnchor out{};
  const VowelAnchor& a = anchors[index];
  const VowelAnchor& b = anchors[index + 1U];
  for (uint8_t i = 0; i < kNumFormants; ++i) {
    out.formant_hz[i] = uint16_t(uint32_t(a.formant_hz[i]) +
                                 ((int32_t(b.formant_hz[i]) - int32_t(a.formant_hz[i])) * int32_t(frac) >> 12));
    out.formant_gain_q12[i] =
        uint16_t(uint32_t(a.formant_gain_q12[i]) +
                 ((int32_t(b.formant_gain_q12[i]) - int32_t(a.formant_gain_q12[i])) * int32_t(frac) >> 12));
  }
  return out;
}
}  // namespace

// Initializes the engine and clears its phase state.
LosengeEngine::LosengeEngine(SineLUT* lut) : lut_(lut), phase_(0), formant_phase_{{0, 0, 0}, {0, 0, 0}} {
  Init();
}

// Resets the carrier and both output formant banks to the start of a cycle.
void LosengeEngine::Init() {
  phase_ = 0;
  for (uint8_t output = 0; output < kNumOutputs; ++output) {
    for (uint8_t formant = 0; formant < kNumFormants; ++formant) {
      formant_phase_[output][formant] = 0;
    }
  }
}

// Keeps the current oscillator state so slot changes crossfade smoothly.
void LosengeEngine::OnSelected() {
  // Keep oscillator continuity.
}

// Clamps the sample to the signed 12-bit ComputerCard output range.
int32_t __not_in_flash_func(LosengeEngine::Clamp12)(int32_t v) {
  if (v > 2047) return 2047;
  if (v < -2048) return -2048;
  return v;
}

// Converts Hz into a 0.32 phase increment for a 48kHz sample rate.
// Uses a constant reciprocal multiply instead of a 64-bit division by 48000.
// 2^32 / 48000 = 89478.485..., so hz * 89478 gives the phase increment with
// sub-Hz accuracy across the formant range (40-6000 Hz). This saves ~80 cycles
// per call vs the 64-bit division, and is called 6 times per control tick.
uint32_t LosengeEngine::HzToPhaseIncrement(uint32_t hz) {
  return uint32_t(uint64_t(hz) * 89478ULL);
}

// Applies a Q12 tract-size shift to a formant frequency in Hz.
// `shift_q12 = 4096` is neutral, values below it darken/lengthen the tract, and
// values above it brighten/shorten the tract. The endpoint range is 0.5x..1.5x.
uint32_t LosengeEngine::ApplyShiftQ12(uint16_t hz, uint16_t shift_q12) {
  uint32_t shifted_hz = (uint32_t(hz) * uint32_t(shift_q12) + 2048U) >> 12;
  if (shifted_hz < 40U) shifted_hz = 40U;
  if (shifted_hz > 6000U) shifted_hz = 6000U;
  return shifted_hz;
}

// Generates a bipolar square in the signed 12-bit board domain.
int32_t __not_in_flash_func(LosengeEngine::SquareQ12)(uint32_t phase) {
  return (phase < 0x80000000U) ? 2047 : -2048;
}

// Mixes the three formant oscillators, applies the glottal envelope, and scales
// the result by the output gain. The envelope and voice gain are both Q12.
int32_t __not_in_flash_func(LosengeEngine::MixFormantsQ12)(int32_t f1,
                                      int32_t f2,
                                      int32_t f3,
                                      const uint16_t (&amplitudes_q12)[3],
                                      uint16_t envelope_q12,
                                      uint16_t voice_gain_q12) {
  int64_t sum = int64_t(f1) * amplitudes_q12[0] + int64_t(f2) * amplitudes_q12[1] + int64_t(f3) * amplitudes_q12[2];
  int32_t weighted = int32_t((sum + 2048) >> 12);
  int32_t enveloped = int32_t((int64_t(weighted) * envelope_q12 + 2048) >> 12);
  return int32_t((int64_t(enveloped) * voice_gain_q12 + 2048) >> 12);
}

// Converts global controls into a Braids-style vocal-oscillator frame.
// Main sets pitch, X navigates named vowel anchors for Out1, Out2 uses the
// inverse path position so the channels move oppositely without wrapping, Y
// applies a shared formant shift, and alt switches from the standard F1/F2/F3
// formant set to the upper F2/F3/F4 set for a brighter, more metallic timbre.
void LosengeEngine::ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) {
  LosengeControlFrame& out = frame.losenge;
  out.phase_increment = global.pitch_inc;

  const uint16_t x_q12 = ToU12Q12(global.macro_x);
  const uint16_t out2_x_q12 = uint16_t(4096U - x_q12);
  const VowelAnchor out1_vowel = global.mode_alt ? InterpolateVowelPath(kUpperAnchors, x_q12)
                                                 : InterpolateVowelPath(kMaleAnchors, x_q12);
  const VowelAnchor out2_vowel = global.mode_alt ? InterpolateVowelPath(kUpperAnchors, out2_x_q12)
                                                 : InterpolateVowelPath(kMaleAnchors, out2_x_q12);

  // Y is centered at 1.0x shift so the middle position sounds neutral. Full CCW
  // is a larger/darker tract at 0.5x, and full CW is a smaller/brighter tract at
  // about 1.5x. The formula is linear so the endpoints are predictable.
  const int32_t shift_delta_q12 = ((int32_t(global.macro_y) - 2048) * 2048) >> 11;
  const uint16_t shift_q12 = uint16_t(4096 + shift_delta_q12);

  const VowelAnchor vowels[kNumOutputs] = {out1_vowel, out2_vowel};
  for (uint8_t output = 0; output < kNumOutputs; ++output) {
    for (uint8_t i = 0; i < kNumFormants; ++i) {
      const uint32_t shifted_hz = ApplyShiftQ12(vowels[output].formant_hz[i], shift_q12);
      out.formant_increment[output][i] = HzToPhaseIncrement(shifted_hz);
      out.formant_amplitude_q12[output][i] = vowels[output].formant_gain_q12[i];
    }
  }

  // A fixed gain keeps the formant sum strong without hard-clipping every vowel.
  out.voice_gain_q12 = 4608U;
}

// Renders one sample of the vocal oscillator with separate vowel positions per
// output. The carrier phase defines pitch and resets both formant banks at each
// glottal pulse, which is the main behavior borrowed from Braids/Twists VOWL.
void __not_in_flash_func(LosengeEngine::RenderSample)(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) {
  const LosengeControlFrame& in = frame.losenge;
  const uint32_t previous_phase = phase_;
  phase_ += in.phase_increment;
  if (phase_ < previous_phase) {
    for (uint8_t output = 0; output < kNumOutputs; ++output) {
      for (uint8_t formant = 0; formant < kNumFormants; ++formant) {
        formant_phase_[output][formant] = 0;
      }
    }
  }

  const uint16_t envelope_q12 = uint16_t(4096U - ((phase_ >> 20) & 0x0FFFU));
  int32_t outputs[kNumOutputs] = {0, 0};
  for (uint8_t output = 0; output < kNumOutputs; ++output) {
    formant_phase_[output][0] += in.formant_increment[output][0];
    formant_phase_[output][1] += in.formant_increment[output][1];
    formant_phase_[output][2] += in.formant_increment[output][2];

    const int32_t formant1 = lut_->LookupLinear(formant_phase_[output][0]);
    const int32_t formant2 = lut_->LookupLinear(formant_phase_[output][1]);
    const int32_t formant3 = SquareQ12(formant_phase_[output][2]);
    outputs[output] = MixFormantsQ12(
        formant1, formant2, formant3, in.formant_amplitude_q12[output], envelope_q12, in.voice_gain_q12);
  }

  out1 = Clamp12(outputs[0]);
  out2 = Clamp12(outputs[1]);
}

}  // namespace mtws
