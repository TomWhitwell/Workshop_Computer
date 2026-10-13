// 11×11 voice matrix — decode CC voiceId into row/col (see VOICE_MATRIX.md).
#pragma once

#include "protocol.h"

#include <cstdint>

struct VoiceMatrixCoord
{
    uint8_t row;
    uint8_t col;
};

uint8_t migrateLegacyEngine(uint8_t legacyEngine);

inline uint8_t clampVoiceId(uint8_t voiceId)
{
    if (voiceId > kVoiceMatrixMax)
        return kVoiceMatrixMax;
    return voiceId;
}

inline VoiceMatrixCoord decodeVoiceMatrix(uint8_t voiceId)
{
    voiceId = clampVoiceId(voiceId);
    return { (uint8_t)(voiceId / kVoiceMatrixCols),
             (uint8_t)(voiceId % kVoiceMatrixCols) };
}

inline uint8_t encodeVoiceMatrix(uint8_t row, uint8_t col)
{
    if (row >= kVoiceMatrixRows)
        row = (uint8_t)(kVoiceMatrixRows - 1);
    if (col >= kVoiceMatrixCols)
        col = (uint8_t)(kVoiceMatrixCols - 1);
    return (uint8_t)(row * kVoiceMatrixCols + col);
}

// CC value for Audio engine slot (direct map, not scaled).
inline uint8_t mapCcToVoiceId(uint8_t cc)
{
    return clampVoiceId(cc);
}
