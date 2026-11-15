#pragma once
#include "samplerate.h"

constexpr uint32_t CLZ16_LUT_SIZE = (FNUM_HI >> 16) + 1;

extern int16_t reciprocal_lut[1024];
extern uint8_t clz_lut16[CLZ16_LUT_SIZE];

void reciprocal_init();
void init_clz_lut();

