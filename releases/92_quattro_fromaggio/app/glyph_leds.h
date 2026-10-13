// LED stroke glyphs (user LEDs 1-6 → firmware 0-5).
#pragma once

#include <cstdint>

struct GlyphPlayer
{
    const uint8_t *stroke = nullptr;
    int index = 0;
    int timer = 0;
    uint8_t pendingDigits[4];
    int pendingCount = 0;
    int pendingIndex = 0;
    int blankTimer = 0;
    bool active = false;
    uint8_t ledMask = 0;

    void playDigit(uint8_t d);
    void playNumber(int value);
    void tick();
    void updateMask();
};

extern GlyphPlayer g_glyph;
