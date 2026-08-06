#pragma once

#include "knots/src/engines/engine_interface.h"

namespace mtws {

class SawsomeEngine : public EngineInterface {
 public:
  SawsomeEngine();

  void Init() override;
  void OnSelected() override;
  void ControlTick(const GlobalControlFrame& global, EngineControlFrame& frame) override;
  void RenderSample(const EngineControlFrame& frame, int32_t& out1, int32_t& out2) override;

 private:
  static constexpr int32_t kUnityQ12 = 4096;

  static int32_t Clamp12(int32_t v);
  static int32_t PolyBlepSawQ12(uint32_t phase, uint32_t phase_inc, uint32_t recip_q24);
  int32_t PolyBlepTriangleQ12(int voice_index, uint32_t phase, uint32_t phase_inc, uint32_t recip_q24);

  uint32_t phases_[kNumSawsomeVoices];
  int32_t tri_state_[kNumSawsomeVoices];
};

}  // namespace mtws
