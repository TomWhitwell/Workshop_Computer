// defines taken from define.h and adjusted for COMPUTERCARD
#pragma once
#include <cstdint>

constexpr int SAMPLINGRATE = 48000;
constexpr int SAMPLINGRATEDIV2 = 24000;
constexpr int KRATE = 48;

constexpr uint32_t FULLPHASEL = UINT32_MAX; // (1ULL<<32)-1
constexpr float FULLPHASE = (float)(FULLPHASEL);
constexpr uint32_t HALFPHASE = FULLPHASEL>>1; 
constexpr float FULLPHASE_SAMPLINGRATE = (float)(FULLPHASE)/(float)(SAMPLINGRATE);

// guards used by polyblep
constexpr double FNUM_BASE = 89478.485333333;
constexpr uint32_t FNUM_LO = (uint32_t)(FNUM_BASE*10u);     // 10 hz
constexpr uint32_t FNUM_HI = (uint32_t)(FNUM_BASE*12500u);  // 1.25 khz

#define TURBO // enables polyblep and more waveforms

