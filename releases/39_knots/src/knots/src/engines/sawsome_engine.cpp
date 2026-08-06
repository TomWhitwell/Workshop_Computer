#include "knots/src/engines/sawsome_engine.h"
#include "pico.h"

namespace mtws {

namespace {
constexpr int32_t kUnityQ12 = 4096;
constexpr int32_t kDetuneOffsetQ16[kNumSawsomeVoices] = {
    -3920, -2370, -910, 0, 1040, 2610, 3550,
};
constexpr int32_t kPanQ12[kNumSawsomeVoices] = {
    -3686, -2458, -1229, 0, 1229, 2458, 3686,
};
constexpr int32_t kGainQ12[kNumSawsomeVoices] = {
    2048, 2867, 3482, 4096, 3482, 2867, 2048,
};
constexpr uint8_t kCenterVoice = 3;
// Sum of squared full-spread voice gains for the 7-voice table above.
// This reference lets the Y-control makeup gain keep level movement moderate
// as side voices fade in from center-only to the full 7-voice spread.
constexpr uint32_t kFullSpreadPowerQ24 = 65853850U;
constexpr uint32_t kMaxMakeupQ12 = 8192U;

inline uint32_t U12ToQ12(uint16_t v) {
  if (v >= 4095U) return 4096U;
  return uint32_t(v);
}

// Integer square root for 64-bit inputs. Used at control rate to derive a
// bounded makeup gain while keeping the engine path on integer math.
uint32_t IntegerSqrt64(uint64_t v) {
  uint64_t bit = 1ULL << 62;
  uint64_t result = 0;

  while (bit > v) {
    bit >>= 2;
  }

  while (bit != 0) {
    if (v >= result + bit) {
      v -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }

  return uint32_t(result);
}
}  // namespace

SawsomeEngine::SawsomeEngine() {
  Init();
}

void SawsomeEngine::Init() {
  for (uint8_t i = 0; i < kNumSawsomeVoices; ++i) {
    phases_[i] = uint32_t(i) * 0x1f123bb5U;
    tri_state_[i] = 0;
  }
}

void SawsomeEngine::OnSelected() {
  // Keep phase continuity.
}

void SawsomeEngine::ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) {
  SawsomeControlFrame& out = frame.sawsome;

  const uint32_t width_q12 = U12ToQ12(global.macro_x);
  const uint32_t detune_q12 = U12ToQ12(global.macro_y);
  uint32_t active_gain_q12[kNumSawsomeVoices];
  out.alt = global.mode_alt;

  // Fade side voices in with detune so full CCW collapses toward the center
  // oscillator while full CW restores the full 7-voice spread.
  uint32_t active_power_q24 = 0;
  for (uint8_t i = 0; i < kNumSawsomeVoices; ++i) {
    uint32_t voice_gain_q12 = uint32_t(kGainQ12[i]);
    if (i != kCenterVoice) {
      voice_gain_q12 = uint32_t((uint64_t(voice_gain_q12) * detune_q12 + 2048U) >> 12);
    }
    active_gain_q12[i] = voice_gain_q12;
    active_power_q24 += voice_gain_q12 * voice_gain_q12;
  }

  // Approximate constant-energy compensation based on the active voice power.
  // This keeps Y movement from sounding like a large level jump while leaving
  // headroom protected by a fixed makeup ceiling.
  uint32_t makeup_q12 = 4096U;
  if (active_power_q24 > 0U) {
    uint64_t makeup_squared_q24 = (uint64_t(kFullSpreadPowerQ24) << 24) / active_power_q24;
    makeup_q12 = IntegerSqrt64(makeup_squared_q24);
    if (makeup_q12 > kMaxMakeupQ12) makeup_q12 = kMaxMakeupQ12;
  }

  for (uint8_t i = 0; i < kNumSawsomeVoices; ++i) {
    // Ratio = 1.0 + (voice detune offset * detune amount).
    int32_t ratio_q16 = 65536 + int32_t((int64_t(kDetuneOffsetQ16[i]) * int32_t(detune_q12)) >> 12);
    if (ratio_q16 < 32768) ratio_q16 = 32768;

    uint64_t inc = (uint64_t(global.pitch_inc) * uint32_t(ratio_q16)) >> 16;
    if (inc > 0x7FFFFFFFULL) inc = 0x7FFFFFFFULL;
    out.voice_phase_increment[i] = uint32_t(inc);
    out.voice_phase_inc_recip_q24[i] = (inc > 0) ? uint32_t((1ULL << 36) / inc) : 0U;

    // Width scales the shared 7-voice pan table before it is converted into
    // left/right gains, so the standalone and integrated engines keep the same
    // stereo image.
    int32_t pan_q12 = int32_t((int64_t(kPanQ12[i]) * int32_t(width_q12)) >> 12);
    int32_t voice_gain_q12 = int32_t((uint64_t(active_gain_q12[i]) * makeup_q12 + 2048U) >> 12);
    int32_t gain_l_q12 = int32_t((int64_t(voice_gain_q12) * (kUnityQ12 - pan_q12)) >> 13);
    int32_t gain_r_q12 = int32_t((int64_t(voice_gain_q12) * (kUnityQ12 + pan_q12)) >> 13);

    if (gain_l_q12 < 0) gain_l_q12 = 0;
    if (gain_r_q12 < 0) gain_r_q12 = 0;
    out.voice_gain_l_q12[i] = int16_t(gain_l_q12);
    out.voice_gain_r_q12[i] = int16_t(gain_r_q12);
  }
}

int32_t __not_in_flash_func(SawsomeEngine::Clamp12)(int32_t v) {
  if (v > 2047) return 2047;
  if (v < -2048) return -2048;
  return v;
}

// Uses the precomputed reciprocal (recip_q24 = (1<<36)/phase_inc) to replace
// audio-rate 64-bit divisions with 64-bit multiplies, saving ~60 cycles per
// BLEP correction on Cortex-M0+.
int32_t __not_in_flash_func(SawsomeEngine::PolyBlepSawQ12)(uint32_t phase, uint32_t phase_inc, uint32_t recip_q24) {
  int32_t saw = int32_t((phase >> 20) & 0x0FFFU) - 2048;
  if (phase_inc == 0) return saw;

  if (phase < phase_inc) {
    uint32_t t_q12 = uint32_t((uint64_t(phase) * recip_q24) >> 24);
    int32_t x = int32_t(t_q12);
    int32_t corr = x + x - int32_t((int64_t(x) * x) >> 12) - 4096;
    saw -= corr >> 1;
  }

  uint32_t tail = 0xFFFFFFFFU - phase;
  if (tail < phase_inc) {
    uint32_t t_q12 = uint32_t((uint64_t(tail) * recip_q24) >> 24);
    int32_t x = int32_t(t_q12);
    int32_t corr = int32_t((int64_t(x) * x) >> 12);
    saw += corr >> 1;
  }

  return Clamp12(saw);
}

int32_t __not_in_flash_func(SawsomeEngine::PolyBlepTriangleQ12)(int voice_index, uint32_t phase, uint32_t phase_inc, uint32_t recip_q24) {
  int32_t saw = PolyBlepSawQ12(phase, phase_inc, recip_q24);
  tri_state_[voice_index] += saw;
  tri_state_[voice_index] -= tri_state_[voice_index] >> 11;
  return Clamp12(tri_state_[voice_index] >> 5);
}

void __not_in_flash_func(SawsomeEngine::RenderSample)(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) {
  const SawsomeControlFrame& in = frame.sawsome;

  int32_t sum_l = 0;
  int32_t sum_r = 0;

  for (uint8_t i = 0; i < kNumSawsomeVoices; ++i) {
    phases_[i] += in.voice_phase_increment[i];

    int32_t s = in.alt ? PolyBlepTriangleQ12(i, phases_[i], in.voice_phase_increment[i], in.voice_phase_inc_recip_q24[i])
                       : PolyBlepSawQ12(phases_[i], in.voice_phase_increment[i], in.voice_phase_inc_recip_q24[i]);

    sum_l += int32_t((int64_t(s) * in.voice_gain_l_q12[i]) >> 12);
    sum_r += int32_t((int64_t(s) * in.voice_gain_r_q12[i]) >> 12);
  }

  out1 = Clamp12(sum_l >> 1);
  out2 = Clamp12(sum_r >> 1);
}

}  // namespace mtws
