#pragma once

#include <stdint.h>

constexpr uint32_t MINIMOOG_WAVE_TABLE_SIZE = 512u;
constexpr uint32_t MINIMOOG_WAVE_BAND_COUNT = 9u;
constexpr uint32_t MINIMOOG_WAVE_COUNT = 6u;

extern const int16_t minimoogWaveLUT[MINIMOOG_WAVE_COUNT]
    [MINIMOOG_WAVE_BAND_COUNT][MINIMOOG_WAVE_TABLE_SIZE];
extern const uint32_t minimoogWaveMaxFrequency[MINIMOOG_WAVE_BAND_COUNT];
