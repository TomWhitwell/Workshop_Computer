#pragma once

#include "knots/src/dsp/sine_lut.h"
#include "knots/src/engines/engine_interface.h"

namespace mtws {

// DinSumEngine is a bounded sine-to-saw morph voice. In normal mode, both
// outputs choose a random interpolation point inside the interval between the
// shared sine and their saw targets, with Y crossfading the `p` noise source
// between smooth low-passed and nervous high-passed variants. Out1 uses the
// local ramp-up saw, while Out2 uses the same ramp-up saw evaluated 90 degrees
// later. In alt mode, the oscillator becomes cycle-locked and probabilistic:
// each phase wrap chooses either sine or saw for the whole cycle, with saw
// probability set by X and decision stickiness set by Y.
class DinSumEngine : public EngineInterface {
 public:
  // Creates the engine with the shared sine LUT dependency.
  explicit DinSumEngine(SineLUT* lut);

  // Resets phase, PRNG, and control/audio interpolation state.
  void Init() override;
  // Keeps the current oscillator state when the slot is reselected.
  void OnSelected() override;
  // Builds one control-rate frame from the latest global control snapshot.
  void ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) override;
  // Renders one stereo sample in signed 12-bit output range.
  void RenderSample(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) override;

 private:
  // Clamps to signed 12-bit board audio range.
  static int32_t Clamp12(int32_t v);
  // Interpolates between two signed audio samples with a Q12 morph amount.
  static int32_t LerpQ12(int32_t a, int32_t b, uint16_t t_q12);
  // Generates a band-limited saw from the shared phase accumulator.
  static int32_t PolyBlepSawQ12(uint32_t phase, uint32_t phase_inc, uint32_t recip_q24);
  // Advances the control-rate PRNG and returns a bipolar 12-bit random value.
  static int16_t Rand12Bipolar(uint32_t& state);
  // Advances the audio-rate PRNG and returns a unipolar 12-bit random value.
  static uint16_t Rand12Unipolar(uint32_t& state);

  // Shared sine lookup table owned by the MTWS app.
  SineLUT* lut_;
  // Xorshift PRNG state used by the control-rate random-target generator.
  uint32_t random_state_;
  // Xorshift PRNG state used by the alt-mode cycle-choice generator.
  uint32_t alt_random_state_;
  // Shared phase accumulator used for both source waveforms.
  uint32_t phase_;
  // Most recently generated low-passed random target shared by both outputs.
  int16_t control_random_lp_next_;
  // Most recently generated high-passed random target shared by both outputs.
  int16_t control_random_hp_next_;
  // Monotonic counter written into each frame so audio-rate interpolation can
  // reset whenever a fresh control block arrives.
  uint32_t frame_epoch_counter_;
  // The last frame sequence observed on the audio core.
  uint32_t active_frame_epoch_;
  // Audio-rate interpolation step inside the current 16-sample control block.
  uint8_t random_interp_step_;
  // Tracks whether the audio path is currently rendering alt-mode cycles.
  bool active_alt_mode_;
  // Stores the waveform family selected for Out1 on the current alt-mode cycle.
  bool alt_cycle_is_saw_out1_;
  // Stores the waveform family selected for Out2 on the current alt-mode cycle.
  bool alt_cycle_is_saw_out2_;
  // Counts how many wraps have elapsed since the current Out1 alt-cycle choice
  // was last refreshed. This implements Y-controlled stickiness.
  uint8_t alt_cycle_hold_out1_;
  // Counts how many wraps have elapsed since the current Out2 alt-cycle choice
  // was last refreshed. This implements Y-controlled stickiness.
  uint8_t alt_cycle_hold_out2_;
};

}  // namespace mtws
