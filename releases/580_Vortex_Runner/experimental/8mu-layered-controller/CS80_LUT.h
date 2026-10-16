#pragma once

#include <stdint.h>

constexpr int Cs80SineLutSize = 64;
constexpr int Cs80CurveLutSize = 16;

extern const int16_t cs80SineLUT[Cs80SineLutSize];
extern const uint32_t cs80SemitoneRatioQ16[13];
extern const uint16_t cs80FilterAlphaQ12[Cs80CurveLutSize];
extern const uint16_t cs80EnvelopeRateQ12[Cs80CurveLutSize];
