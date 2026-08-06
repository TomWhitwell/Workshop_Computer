#include "knots/src/engines/floatable_engine.h"
#include "pico.h"

#include "knots/src/wavetables/floatable_bank_1_16x256.h"
#include "knots/src/wavetables/floatable_bank_4_16x256.h"

namespace mtws {

namespace {
// Clamps the audio sample to the signed 12-bit ComputerCard output range.
inline int32_t __not_in_flash_func(ClampAudio12)(int32_t sample) {
  if (sample > 2047) return 2047;
  if (sample < -2048) return -2048;
  return sample;
}
}  // namespace

// Initializes the oscillator with a cleared phase accumulator.
FloatableEngine::FloatableEngine() : phase_(0) {}

void FloatableEngine::OnSelected() {
  // Keep phase continuity for smooth slot changes.
}

// Interpolates between `a` and `b` using a Q12 fraction. `4096` is used instead
// of `4095` so the endpoint can reach the last stored sample exactly.
int32_t __not_in_flash_func(FloatableEngine::LerpQ12)(int32_t a, int32_t b, uint32_t t_q12) {
  if (t_q12 > 4096U) t_q12 = 4096U;
  return a + ((b - a) * int32_t(t_q12) >> 12);
}

// Converts one axis code into a source-wave selection pair.
// Inputs:
// - axis_code: control-domain morph coordinate in 0..4095.
// Outputs:
// - wave_index: base source wave in 0..14.
// - wave_frac_q12: Q12 fraction in 0..4096 toward wave_index + 1.
void FloatableEngine::ComputeWaveSelection(uint32_t axis_code,
                                           uint8_t& wave_index,
                                           uint16_t& wave_frac_q12) {
  if (axis_code > 4095U) axis_code = 4095U;

  uint32_t wave_pos_q12 = axis_code * kInterpolationCells;
  uint32_t index = wave_pos_q12 >> 12;
  if (index > (kNumSourceWaves - 2U)) index = kNumSourceWaves - 2U;

  uint32_t frac_q12 = wave_pos_q12 & 0x0FFFU;
  if (axis_code == 4095U) frac_q12 = 4096U;

  wave_index = static_cast<uint8_t>(index);
  wave_frac_q12 = static_cast<uint16_t>(frac_q12);
}

// Renders one output sample from a source bank using two interpolation stages.
// Inputs:
// - source_waves: selected bank set with shape [16][256].
// - wave_index/wave_frac_q12: morph position between adjacent source waves.
// - sample_index/sample_next/sample_frac_q12: oscillator phase components.
// Output:
// - one signed 12-bit clamped sample.
int32_t __not_in_flash_func(FloatableEngine::RenderMorphedBankSample)(const int16_t (*source_waves)[kSourceTableSize],
                                                 uint32_t wave_index,
                                                 uint32_t wave_frac_q12,
                                                 uint32_t sample_index,
                                                 uint32_t sample_next,
                                                 uint32_t sample_frac_q12) {
  if (wave_index > (kNumSourceWaves - 2U)) wave_index = kNumSourceWaves - 2U;

  const int16_t* wave_a = source_waves[wave_index];
  const int16_t* wave_b = source_waves[wave_index + 1U];

  int32_t sample_a = LerpQ12(wave_a[sample_index], wave_a[sample_next], sample_frac_q12);
  int32_t sample_b = LerpQ12(wave_b[sample_index], wave_b[sample_next], sample_frac_q12);
  int32_t morphed_sample = LerpQ12(sample_a, sample_b, wave_frac_q12);
  return ClampAudio12(morphed_sample >> 4);
}

// Publishes compact floatable state at control rate.
// Inputs:
// - global control frame (pitch, alt flag, and axis controls).
// Output:
// - floatable engine frame with compact morph parameters only.
void FloatableEngine::ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) {
  frame.floatable.phase_inc = global.pitch_inc;
  frame.floatable.use_alt_banks = global.mode_alt ? 1U : 0U;
  ComputeWaveSelection(global.macro_x, frame.floatable.out1_wave_index, frame.floatable.out1_wave_frac_q12);
  ComputeWaveSelection(global.macro_y, frame.floatable.out2_wave_index, frame.floatable.out2_wave_frac_q12);
}

// Renders floatable audio directly from source waves at audio rate.
// Inputs:
// - compact floatable control data from the active double-buffered frame.
// Outputs:
// - out1/out2 signed 12-bit samples.
void __not_in_flash_func(FloatableEngine::RenderSample)(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) {
  const FloatableControlFrame& w = frame.floatable;

  phase_ += w.phase_inc;

  uint32_t sample_index = phase_ >> kSourceTableIndexShift;
  uint32_t sample_next = (sample_index + 1U) & kSourceTableMask;
  uint32_t sample_frac_q12 = (phase_ >> kSourceTableFracShift) & 0x0FFFU;

  // Single bank for both outputs. Normal = bank 1, alt = bank 4.
  // X and Y select different morph positions within the same timbral space,
  // halving the flash working set and improving XIP cache hit rate.
  const int16_t (*source)[kSourceTableSize] =
      (w.use_alt_banks != 0U) ? floatable_bank_4_16x256 : floatable_bank_1_16x256;

  out1 = RenderMorphedBankSample(source,
                                 w.out1_wave_index,
                                 w.out1_wave_frac_q12,
                                 sample_index,
                                 sample_next,
                                 sample_frac_q12);
  out2 = RenderMorphedBankSample(source,
                                 w.out2_wave_index,
                                 w.out2_wave_frac_q12,
                                 sample_index,
                                 sample_next,
                                 sample_frac_q12);
}

}  // namespace mtws
