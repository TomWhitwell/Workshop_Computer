#pragma once

#include <cstdint>

namespace GridsResources {

constexpr uint8_t kNumNodes = 25;
constexpr uint8_t kStepsPerPattern = 32;
constexpr uint8_t kNumInstruments = 3;

extern const uint8_t kNodeTable[kNumNodes][kStepsPerPattern * kNumInstruments];

}  // namespace GridsResources

