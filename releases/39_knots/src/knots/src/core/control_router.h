#pragma once

#include <cstdint>

#include "knots/src/core/shared_state.h"

namespace mtws {

class ControlRouter {
 public:
  static uint32_t BasePhaseIncrementFromPitchCode(uint32_t pitch_code);
  static uint32_t ApplySemitoneToPhaseInc(uint32_t base_inc, int semitone);
  static GlobalControlFrame BuildGlobalFrame(const UISnapshot& ui, const MidiState& midi);
};

}  // namespace mtws
