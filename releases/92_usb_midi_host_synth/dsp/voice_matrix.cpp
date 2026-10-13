#include "voice_matrix.h"

uint8_t migrateLegacyEngine(uint8_t legacyEngine)
{
    // Matches VOICE_MATRIX.md §9 legacy table.
    static const uint8_t kMap[13] = {
        0,   // 0 Square/pulse → R0C0
        22,  // 1 Sine → R2C0
        33,  // 2 Saw → R3C0
        44,  // 3 Triangle → R4C0
        36,  // 4 Dual saw → R3C3
        4,   // 5 Pulse+sub → R0C4
        25,  // 6 Dual sine → R2C3
        37,  // 7 Saw+sub → R3C4
        40,  // 8 Moogish → R3C7
        8,   // 9 Junoish → R0C8
        43,  // 10 Sync → R3C10
        42,  // 11 Acid → R3C9
        88,  // 12 FM bell → R8C0
    };
    if (legacyEngine <= 12)
        return kMap[legacyEngine];
    return clampVoiceId(legacyEngine);
}
