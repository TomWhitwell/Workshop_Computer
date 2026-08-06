#pragma once

#include "knots/src/dsp/sine_lut.h"
#include "knots/src/engines/engine_interface.h"

namespace mtws {

// CumulusEngine is a 16-partial additive voice with centroid-driven spectral
// shaping. Control-rate code prepares per-partial increments and gains, while
// audio-rate code only performs LUT reads and fixed-point accumulation.
class CumulusEngine : public EngineInterface {
 public:
  // Creates the engine with the shared sine LUT dependency.
  explicit CumulusEngine(SineLUT* lut);

  // Resets all partial phases and smoothed control state.
  void Init() override;
  // Keeps phase continuity when the slot is reselected.
  void OnSelected() override;
  // Builds one control frame containing per-partial frequencies and gains.
  void ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) override;
  // Renders one stereo sample in the signed 12-bit output domain.
  void RenderSample(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) override;

 private:
  static constexpr uint32_t kUnityQ12 = 4096U;
  static constexpr int32_t kMinRatioQ16 = 6554;      // 0.1x
  static constexpr int32_t kMaxRatioQ16 = 1048576;   // 16x
  static constexpr uint32_t kNyquistPhaseInc = 0x7FFFFFFFU;
  static constexpr uint32_t kPartialMaxPhaseInc = 0x7F000000U;  // ~23.8kHz @ 48kHz (just below Nyquist)
  static constexpr int kRatioSmoothShift = 3;
  static constexpr int kGainSmoothShift = 3;
  static constexpr int kRenderSumShift = 4;  // Fixed headroom for odd/even bus sums.
  static constexpr uint32_t kCumulusMasterGainQ12 = 32768U;  // Master makeup target, auto-capped per frame.
  static constexpr uint32_t kUniformPartialGainQ12 = 8192U;   // Uniform per-partial gain (2x).

  // Harmonic ratio helper: partial `i` maps to `(i + 1)` in Q16.
  inline int32_t HarmonicRatioQ16(int i) const { return int32_t((i + 1) << 16); }

  // Centroid shape helpers and smoothing utilities used during ControlTick.
  static int32_t CentroidBaseGainQ12(int partial_index, uint32_t centroid_q12);
  static int32_t CentroidNarrowGainQ12(int partial_index, uint32_t centroid_q12);
  static int32_t LerpQ12(int32_t a, int32_t b, uint32_t t_q12);
  static int32_t SmoothToward(int32_t current, int32_t target, int shift);
  static int32_t FoldReflectQ16(int32_t x, int32_t lo, int32_t hi);
  static int32_t ShapedCentroidGainQ12(int partial_index, uint32_t centroid_q12, uint32_t knob_y);

  // Shared sine lookup table owned by the host app.
  SineLUT* lut_;
  // Per-partial oscillator phases in 0.32 units.
  uint32_t phases_[kNumCumulusVoices];
  // Smoothed per-partial ratio and gain state published across control frames.
  int32_t smoothed_ratio_q16_[kNumCumulusVoices];
  int32_t smoothed_gain_q12_[kNumCumulusVoices];
};

}  // namespace mtws
