#pragma once

#include "knots/src/dsp/sine_lut.h"
#include "knots/src/engines/engine_interface.h"

namespace mtws {

// LosengeEngine is a pitched formant-oscillator bank inspired by the Braids
// VOWL model. One hidden carrier defines the note period and hard-resets two
// independent 3-formant output banks once per cycle.
class LosengeEngine : public EngineInterface {
 public:
  // Creates the engine with the shared sine LUT used by the first two formants.
  explicit LosengeEngine(SineLUT* lut);

  // Resets the carrier and both output formant banks.
  void Init() override;
  // Keeps oscillator continuity when the slot is selected.
  void OnSelected() override;
  // Converts global controls into per-block formant frequencies and gains.
  void ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) override;
  // Renders one stereo sample in signed 12-bit output domain.
  void RenderSample(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) override;

 private:
  // Clamps to the signed 12-bit ComputerCard output range.
  static int32_t Clamp12(int32_t v);
  // Converts formant frequency in Hz to a 0.32 phase increment at 48kHz.
  static uint32_t HzToPhaseIncrement(uint32_t hz);
  // Applies a Q12 tract-size shift to a base formant frequency in Hz.
  static uint32_t ApplyShiftQ12(uint16_t hz, uint16_t shift_q12);
  // Generates the square third-formant oscillator in the board sample domain.
  static int32_t SquareQ12(uint32_t phase);
  // Mixes one output bank of three formants with Q12 amplitude and gain controls.
  static int32_t MixFormantsQ12(int32_t f1,
                                int32_t f2,
                                int32_t f3,
                                const uint16_t (&amplitudes_q12)[3],
                                uint16_t envelope_q12,
                                uint16_t voice_gain_q12);

  // Shared sine lookup table used by the sine formants.
  SineLUT* lut_;
  // Carrier phase accumulator in 0.32. It defines pitch and reset timing.
  uint32_t phase_;
  // Per-output formant oscillator phases in 0.32, reset on carrier wrap.
  uint32_t formant_phase_[2][3];
};

}  // namespace mtws
