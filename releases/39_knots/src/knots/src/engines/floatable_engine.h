#pragma once

#include "knots/src/engines/engine_interface.h"

namespace mtws {

class FloatableEngine : public EngineInterface {
 public:
  FloatableEngine();

  void OnSelected() override;
  void ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) override;
  void RenderSample(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) override;

 private:
  static constexpr uint32_t kNumSourceWaves = 16U;
  static constexpr uint32_t kInterpolationCells = kNumSourceWaves - 1U;
  static constexpr uint32_t kSourceTableSize = 256U;
  static constexpr uint32_t kSourceTableMask = kSourceTableSize - 1U;
  static constexpr uint32_t kSourceTableIndexShift = 24U;
  static constexpr uint32_t kSourceTableFracShift = 12U;

  // Q12 interpolation helper where `t_q12 = 0` returns `a` and `4096` returns `b`.
  static int32_t LerpQ12(int32_t a, int32_t b, uint32_t t_q12);
  // Converts a 0..4095 axis code into a base wave index (0..14) and Q12
  // fraction (0..4096) between that index and index + 1.
  // Inputs: axis_code in knob-domain units.
  // Outputs: wave_index and wave_frac_q12 passed by reference.
  static void ComputeWaveSelection(uint32_t axis_code, uint8_t& wave_index, uint16_t& wave_frac_q12);
  // Renders one output sample from a source bank using phase interpolation
  // within each source wave and a second interpolation across adjacent waves.
  // Inputs: source bank, wave selection, and current phase components.
  // Output: signed 12-bit clamped sample ready for DAC output.
  static int32_t RenderMorphedBankSample(const int16_t (*source_waves)[kSourceTableSize],
                                         uint32_t wave_index,
                                         uint32_t wave_frac_q12,
                                         uint32_t sample_index,
                                         uint32_t sample_next,
                                         uint32_t sample_frac_q12);

  uint32_t phase_;
};

}  // namespace mtws
