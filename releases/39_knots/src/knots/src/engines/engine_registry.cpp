#include "knots/src/engines/engine_registry.h"
#include "pico.h"

namespace mtws {

EngineRegistry::EngineRegistry(SineLUT* lut)
    : sawsome_(),
      bender_(lut),
      floatable_(),
      cumulus_(lut),
      losenge_(lut),
      din_sum_(lut),
      slots_{
          &sawsome_,
          &bender_,
          &floatable_,
          &cumulus_,
          &losenge_,
          &din_sum_,
      } {}

void EngineRegistry::Init() {
  for (uint8_t i = 0; i < kNumOscillatorSlots; ++i) {
    slots_[i]->Init();
  }
}

EngineInterface* __not_in_flash_func(EngineRegistry::Get)(uint8_t slot) {
  if (slot >= kNumOscillatorSlots) {
    slot = 0;
  }
  return slots_[slot];
}

}  // namespace mtws
