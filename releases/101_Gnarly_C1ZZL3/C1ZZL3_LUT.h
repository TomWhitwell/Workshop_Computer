//
//  C1ZZl3_LUT.h
//  
//
//  Created by Adrian Vos on 08/06/2026.
//


// C1ZZL3_LUT.h
#pragma once

#include <stdint.h>

constexpr int SINE_SIZE = 1024;
constexpr int PD_WAVE_COUNT = 8;
constexpr int PD_WAVE_SIZE = 4096;

extern const int16_t sineLUT[SINE_SIZE];
extern const int16_t phaseWarpLUT[SINE_SIZE];
extern const int16_t pdWaveLUT[PD_WAVE_COUNT][PD_WAVE_SIZE];
