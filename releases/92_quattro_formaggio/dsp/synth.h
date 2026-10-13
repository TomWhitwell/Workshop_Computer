// Oscillators, LUTs, pitch increment, per-voice character FX.
#pragma once

#include <cstdint>

struct PolyVoice;

void initLuts();
int16_t phaseSin(uint32_t phase); // 256-point LUT, ±2047
int32_t clamp12(int32_t x);
int32_t softClip12(int32_t x);
uint32_t noteBendIncrement(uint8_t note, int16_t bend14, uint8_t bendSemitones);
int16_t oscSample(uint32_t phase, uint32_t inc, uint8_t voiceType);
int32_t renderVoiceSample(uint32_t &phase, uint32_t &phase2, uint32_t inc,
                          uint8_t voiceType);

// Oscillator + voice-local character FX (filter / chorus / glide / sync / FM).
// voiceId 0–120 → row/col matrix (VOICE_MATRIX.md).
void renderVoiceMatrix(PolyVoice &v, uint32_t targetInc, uint8_t row,
                       uint8_t col, int32_t &outL, int32_t &outR);
void renderPolyVoiceAudio(PolyVoice &v, uint32_t targetInc, uint8_t voiceId,
                          int32_t &outL, int32_t &outR);

// Legacy one-pole (unused by famous engines; kept for reference/tools).
int32_t applyCutoff(int32_t x, int32_t &state);
