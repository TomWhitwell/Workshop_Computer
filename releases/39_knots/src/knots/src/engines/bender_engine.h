#pragma once

#include "knots/src/dsp/sine_lut.h"
#include "knots/src/engines/engine_interface.h"

namespace mtws {

// BenderEngine runs one sine oscillator through Utility-Pair-style folding and
// crushing paths. Normal mode exposes the current fold-vs-crush A/B patch, and
// alt mode compares the two nonlinear stage orderings.
class BenderEngine : public EngineInterface {
 public:
  // Creates the engine with the shared sine lookup table.
  explicit BenderEngine(SineLUT* lut);

  // Resets oscillator phase and the fold antialiasing history.
  void Init() override;
  // Keeps the current oscillator phase when the slot is reselected.
  void OnSelected() override;
  // Builds one lightweight control frame for the audio renderer.
  void ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) override;
  // Renders one stereo sample in the signed 12-bit output domain.
  void RenderSample(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) override;

 private:
  // Clamps to the signed 12-bit ComputerCard audio range.
  static int32_t Clamp12(int32_t v);
  // Utility-Pair wavefolder transfer and its antiderivative.
  static int32_t FoldFunction(int32_t x);
  static int32_t FoldIntegral(int32_t x);
  // Differentiated antiderivative form used for fold antialiasing.
  static int32_t AntiAliasedFold(int32_t x, int32_t& last_integral, int32_t& last_input);
  // Utility-Pair bitcrusher transfer.
  static int32_t CrushFunction(int32_t x, int32_t amount);
  // Q12 interpolation helper used for the dry/wet fold mix.
  static int32_t LerpQ12(int32_t a, int32_t b, uint32_t t_q12);

  // Shared sine lookup table owned by the host app.
  SineLUT* lut_;
  // Oscillator phase accumulator in 0.32 phase units.
  uint32_t phase_;
  // Antiderivative state for the direct sine -> fold path.
  int32_t last_fold_integral_pre_;
  int32_t last_fold_input_pre_;
  // Antiderivative state for the crush -> fold alt path.
  int32_t last_fold_integral_post_;
  int32_t last_fold_input_post_;
  // Tracks the previously rendered mode so history can be reset on changes.
  bool last_alt_;
};

}  // namespace mtws
