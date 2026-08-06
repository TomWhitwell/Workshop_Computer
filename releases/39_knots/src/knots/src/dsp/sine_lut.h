#pragma once

#include <cstdint>
#include <cmath>
#include "pico.h"

namespace mtws {

class SineLUT {
 public:
  static constexpr uint32_t kTableSize = 512;
  static constexpr uint32_t kTableMask = kTableSize - 1;

  SineLUT() {
    for (uint32_t i = 0; i < kTableSize; ++i) {
      table_[i] = int16_t(32000.0 * std::sin(2.0 * M_PI * double(i) / double(kTableSize)));
    }
  }

  // Linearly interpolates the sine table using a single multiply instead of two.
  // Fraction reduced from Q16 to Q12 so (s2-s1)*frac fits 32 bits without
  // overflow (max diff ~64000, max frac 4095, product ~262M < INT32_MAX).
  // Saves one multiply per call; for cumulus (16 calls/sample) this is
  // 16 fewer cycles per sample at 48kHz.
  inline int32_t __not_in_flash_func(LookupLinear)(uint32_t phase) const {
    uint32_t index = phase >> 23;
    int32_t frac_q12 = int32_t((phase >> 11) & 0xFFFU);
    int32_t s1 = table_[index];
    int32_t s2 = table_[(index + 1U) & kTableMask];
    return (s1 >> 4) + (((s2 - s1) * frac_q12) >> 16);
  }

 private:
  int16_t table_[kTableSize];
};

}  // namespace mtws
