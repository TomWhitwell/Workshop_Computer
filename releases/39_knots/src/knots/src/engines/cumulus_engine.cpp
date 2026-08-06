#include "knots/src/engines/cumulus_engine.h"
#include "pico.h"

namespace mtws {

// Creates the additive engine and initializes the per-partial phase and
// smoothing state used by both control-rate and audio-rate code.
CumulusEngine::CumulusEngine(SineLUT* lut) : lut_(lut) {
  Init();
}

// Resets all partial phases and smoothing memory to the default harmonic stack.
void CumulusEngine::Init() {
  for (int i = 0; i < kNumCumulusVoices; ++i) {
    phases_[i] = 0;
    smoothed_ratio_q16_[i] = HarmonicRatioQ16(i);
    smoothed_gain_q12_[i] = int32_t(kUnityQ12 / uint32_t(i + 1));
  }
}

void CumulusEngine::OnSelected() {
  // Keep phase continuity to avoid clicks when re-entering this engine.
}

// Broad centroid weighting where gain falls as 1 / (1 + distance) across the
// partial index domain.
int32_t CumulusEngine::CentroidBaseGainQ12(int partial_index, uint32_t centroid_q12) {
  uint32_t pos_q12 = uint32_t(partial_index) << 12;
  uint32_t d_q12 = (pos_q12 >= centroid_q12) ? (pos_q12 - centroid_q12) : (centroid_q12 - pos_q12);
  uint32_t denom_q12 = kUnityQ12 + d_q12;
  uint64_t numer = uint64_t(kUnityQ12) * kUnityQ12;
  return int32_t((numer + (denom_q12 >> 1)) / denom_q12);
}

// Narrow centroid weighting where only the nearest neighboring partials remain
// active, giving a pointier spectrum at lower Y positions.
int32_t CumulusEngine::CentroidNarrowGainQ12(int partial_index, uint32_t centroid_q12) {
  uint32_t pos_q12 = uint32_t(partial_index) << 12;
  uint32_t d_q12 = (pos_q12 >= centroid_q12) ? (pos_q12 - centroid_q12) : (centroid_q12 - pos_q12);
  if (d_q12 >= kUnityQ12) return 0;
  return int32_t(kUnityQ12 - d_q12);
}

// Fixed-point linear interpolation helper where `t_q12 = 0` returns `a` and
// `t_q12 = 4096` returns `b`.
int32_t CumulusEngine::LerpQ12(int32_t a, int32_t b, uint32_t t_q12) {
  if (t_q12 > kUnityQ12) t_q12 = kUnityQ12;
  return int32_t((int64_t(a) * int32_t(kUnityQ12 - t_q12) + int64_t(b) * int32_t(t_q12)) >> 12);
}

// Integer one-pole smoother used to keep ratio and gain changes free of zipper
// noise without adding per-partial buffers.
int32_t CumulusEngine::SmoothToward(int32_t current, int32_t target, int shift) {
  if (shift <= 0) return target;
  int32_t delta = target - current;
  int32_t step = delta >> shift;
  if (step == 0 && delta != 0) {
    step = (delta > 0) ? 1 : -1;
  }
  return current + step;
}

// Reflect-folds a warped ratio into the legal Q16 ratio range so the alt-mode
// attract/repel motion stays continuous instead of hard-clipping at the edges.
int32_t CumulusEngine::FoldReflectQ16(int32_t x, int32_t lo, int32_t hi) {
  int64_t span = int64_t(hi) - int64_t(lo);
  if (span <= 0) return lo;
  int64_t period = span << 1;
  int64_t t = (int64_t(x) - int64_t(lo)) % period;
  if (t < 0) t += period;
  if (t > span) t = period - t;
  return int32_t(int64_t(lo) + t);
}

// Blends the narrow centroid shape, broad centroid shape, and a flat spectrum
// as Y moves from fully counter-clockwise to fully clockwise.
int32_t CumulusEngine::ShapedCentroidGainQ12(int partial_index, uint32_t centroid_q12, uint32_t knob_y) {
  if (knob_y > 4095U) knob_y = 4095U;

  int32_t g_base = CentroidBaseGainQ12(partial_index, centroid_q12);
  int32_t g_narrow = CentroidNarrowGainQ12(partial_index, centroid_q12);

  if (knob_y <= 2048U) {
    uint32_t t_q12 = knob_y << 1;
    return LerpQ12(g_narrow, g_base, t_q12);
  }

  uint32_t t_q12 = (knob_y - 2048U) << 1;
  if (knob_y >= 4095U) t_q12 = kUnityQ12;
  return LerpQ12(g_base, int32_t(kUnityQ12), t_q12);
}

// Builds one additive control frame. X moves the spectral centroid, Y controls
// shape in normal mode and ratio attract/repel in alt mode, and the control
// path also computes a conservative bus normalization gain for the next block.
void CumulusEngine::ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) {
  CumulusControlFrame& out = frame.cumulus;

  uint32_t knob_x = global.macro_x;
  uint32_t knob_y = global.macro_y;
  uint32_t base_inc = global.pitch_inc;
  bool alt_mode = global.mode_alt;

  if (knob_x > 4095U) knob_x = 4095U;
  uint32_t centroid_max_q12 = uint32_t(kNumCumulusVoices - 1) << 12;
  uint32_t centroid_q12 = uint32_t((uint64_t(knob_x) * centroid_max_q12 + 2048U) >> 12);
  int32_t centroid_ratio_q16 = int32_t((1U << 16) + (centroid_q12 << 4));

  int32_t max_ratio_q16 = kMaxRatioQ16;

  uint32_t odd_gain_sum_q12 = 0;
  uint32_t even_gain_sum_q12 = 0;
  for (int i = 0; i < kNumCumulusVoices; ++i) {
    int32_t target_ratio_q16 = HarmonicRatioQ16(i);

    if (alt_mode) {
      if (i == 0 && knob_y > 2048U) {
        target_ratio_q16 = HarmonicRatioQ16(0);
      } else {
        if (knob_y < 2048U) {
          uint32_t warp_q12 = (2048U - knob_y) << 1;
          target_ratio_q16 = LerpQ12(target_ratio_q16, centroid_ratio_q16, warp_q12);
        } else if (knob_y > 2048U) {
          uint32_t warp_q12 = (knob_y - 2048U) << 1;
          if (knob_y >= 4095U) warp_q12 = kUnityQ12;
          int32_t repel_ratio_q16 = (target_ratio_q16 << 1) - centroid_ratio_q16;
          target_ratio_q16 = LerpQ12(target_ratio_q16, repel_ratio_q16, warp_q12);
        }

        target_ratio_q16 = FoldReflectQ16(target_ratio_q16, HarmonicRatioQ16(0), max_ratio_q16);
      }

      if (target_ratio_q16 < kMinRatioQ16) target_ratio_q16 = kMinRatioQ16;
      if (target_ratio_q16 > max_ratio_q16) target_ratio_q16 = max_ratio_q16;
    }

    if (alt_mode) {
      smoothed_ratio_q16_[i] = SmoothToward(smoothed_ratio_q16_[i], target_ratio_q16, kRatioSmoothShift);
      if (smoothed_ratio_q16_[i] < kMinRatioQ16) smoothed_ratio_q16_[i] = kMinRatioQ16;
      if (smoothed_ratio_q16_[i] > max_ratio_q16) smoothed_ratio_q16_[i] = max_ratio_q16;
    } else {
      smoothed_ratio_q16_[i] = target_ratio_q16;
    }

    uint64_t voice_inc_64 = (uint64_t(base_inc) * uint64_t(uint32_t(smoothed_ratio_q16_[i]))) >> 16;
    bool voice_above_guard = (voice_inc_64 > uint64_t(kPartialMaxPhaseInc));
    if (voice_inc_64 > uint64_t(kNyquistPhaseInc)) voice_inc_64 = uint64_t(kNyquistPhaseInc);
    out.voice_phase_increment[i] = uint32_t(voice_inc_64);

    if (voice_above_guard) {
      // Hard mute above cutoff for diagnostic clarity.
      smoothed_gain_q12_[i] = 0;
      out.voice_gain_q12[i] = 0;
    } else {
      int32_t target_gain_q12 = ShapedCentroidGainQ12(i, centroid_q12, knob_y);
      if (target_gain_q12 < 0) target_gain_q12 = 0;
      if (target_gain_q12 > int32_t(kUnityQ12)) target_gain_q12 = int32_t(kUnityQ12);

      smoothed_gain_q12_[i] = SmoothToward(smoothed_gain_q12_[i], target_gain_q12, kGainSmoothShift);
      if (smoothed_gain_q12_[i] < 0) smoothed_gain_q12_[i] = 0;
      if (smoothed_gain_q12_[i] > int32_t(kUnityQ12)) smoothed_gain_q12_[i] = int32_t(kUnityQ12);

      int32_t scaled_gain_q12 =
          int32_t((int64_t(smoothed_gain_q12_[i]) * int32_t(kUniformPartialGainQ12) + 2048) >> 12);
      if (scaled_gain_q12 < 0) scaled_gain_q12 = 0;
      out.voice_gain_q12[i] = scaled_gain_q12;
    }
    uint32_t g_q12 = uint32_t(out.voice_gain_q12[i]);
    if ((i & 1) == 0) {
      odd_gain_sum_q12 += g_q12;
    } else {
      even_gain_sum_q12 += g_q12;
    }
  }

  uint32_t max_bus_gain_sum_q12 = (odd_gain_sum_q12 > even_gain_sum_q12) ? odd_gain_sum_q12 : even_gain_sum_q12;
  if (max_bus_gain_sum_q12 == 0U) {
    out.mix_norm_q12 = int32_t(kCumulusMasterGainQ12);
    return;
  }

  // Conservative peak estimate for one bus:
  // bus_peak ~= (2047 * sum(gains_q12) / 4096) / (1<<kRenderSumShift).
  uint32_t bus_peak_pre_mix = uint32_t((uint64_t(2047U) * max_bus_gain_sum_q12) >> 12);
  bus_peak_pre_mix >>= kRenderSumShift;
  if (bus_peak_pre_mix == 0U) bus_peak_pre_mix = 1U;

  // Max safe Q12 multiplier so post-mix peak stays <= 2047.
  uint32_t safe_mix_norm_q12 =
      uint32_t(((uint64_t(2047U) << 12) + (bus_peak_pre_mix >> 1)) / uint64_t(bus_peak_pre_mix));
  if (safe_mix_norm_q12 > kCumulusMasterGainQ12) safe_mix_norm_q12 = kCumulusMasterGainQ12;
  out.mix_norm_q12 = int32_t(safe_mix_norm_q12);
}

// Renders the 16 sine partials, routes even and odd indices to separate
// stereo buses, then applies the per-frame normalization gain and 12-bit clamp.
void __not_in_flash_func(CumulusEngine::RenderSample)(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) {
  const CumulusControlFrame& in = frame.cumulus;

  int32_t sum_odd = 0;
  int32_t sum_even = 0;
  for (int i = 0; i < kNumCumulusVoices; ++i) {
    phases_[i] += in.voice_phase_increment[i];
    int32_t s = lut_->LookupLinear(phases_[i]);
    int32_t voice = (s * in.voice_gain_q12[i]) >> 12;
    if ((i & 1) == 0) {
      sum_odd += voice;
    } else {
      sum_even += voice;
    }
  }

  int32_t odd_bus = sum_odd >> kRenderSumShift;
  int32_t even_bus = sum_even >> kRenderSumShift;

  int32_t odd_out = int32_t((int64_t(odd_bus) * in.mix_norm_q12) >> 12);
  int32_t even_out = int32_t((int64_t(even_bus) * in.mix_norm_q12) >> 12);

  if (odd_out > 2047) odd_out = 2047;
  if (odd_out < -2048) odd_out = -2048;
  if (even_out > 2047) even_out = 2047;
  if (even_out < -2048) even_out = -2048;

  out1 = odd_out;
  out2 = even_out;
}

}  // namespace mtws
