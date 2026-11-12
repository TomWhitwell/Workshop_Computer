#include "lut.h"
#include <cstdint>

int16_t reciprocal_lut[1024];
uint8_t clz_lut16[CLZ16_LUT_SIZE];

void reciprocal_init()
{
  for (unsigned i = 0; i < 1024; i++)
  {
    // Midpoint of [1 + i/1024, 1 + (i+1)12024]
    float m = 1.0f + ((float)i + 0.5f) / 1024.0f;

    // Q15 reciprocal
    reciprocal_lut[i] = (int16_t)(32768.0f / m + 0.5f);
  }
}

void init_clz_lut()
{
    clz_lut16[0] = 16;
    
    for (uint32_t i = 1; i < CLZ16_LUT_SIZE; i++)
    {
        clz_lut16[i] = __builtin_clz(i) - 16;
    }
}
