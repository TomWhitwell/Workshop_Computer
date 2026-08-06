#pragma once

#include "knots/src/dsp/sine_lut.h"
#include "knots/src/engines/bender_engine.h"
#include "knots/src/engines/cumulus_engine.h"
#include "knots/src/engines/din_sum_engine.h"
#include "knots/src/engines/floatable_engine.h"
#include "knots/src/engines/losenge_engine.h"
#include "knots/src/engines/sawsome_engine.h"

namespace mtws {

class EngineRegistry {
 public:
  explicit EngineRegistry(SineLUT* lut);

  void Init();
  EngineInterface* Get(uint8_t slot);

 private:
  SawsomeEngine sawsome_;
  BenderEngine bender_;
  FloatableEngine floatable_;
  CumulusEngine cumulus_;
  LosengeEngine losenge_;
  DinSumEngine din_sum_;

  EngineInterface* slots_[kNumOscillatorSlots];
};

}  // namespace mtws
