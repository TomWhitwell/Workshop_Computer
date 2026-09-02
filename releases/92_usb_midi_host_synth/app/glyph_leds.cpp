#include "glyph_leds.h"

namespace {

constexpr int kGlyphStepSamples = 960; // 20ms @ 48kHz
constexpr uint8_t kGlyphEnd = 0xFF;
constexpr uint8_t kGlyphAll = 0xFE;

const uint8_t kStroke0[] = {kGlyphAll, kGlyphEnd};
const uint8_t kStroke1[] = {1, 3, 5, kGlyphEnd};
const uint8_t kStroke2[] = {0, 1, 3, 2, 4, 5, kGlyphEnd};
const uint8_t kStroke3[] = {0, 1, 3, 2, 5, 4, kGlyphEnd};
const uint8_t kStroke4[] = {0, 2, 3, 1, 5, kGlyphEnd};
const uint8_t kStroke5[] = {1, 0, 2, 3, 5, 4, kGlyphEnd};
const uint8_t kStroke6[] = {1, 0, 2, 4, 5, 3, kGlyphEnd};
const uint8_t kStroke7[] = {0, 1, 3, 5, kGlyphEnd};
const uint8_t kStroke8[] = {3, 0, 1, 2, 4, 5, 2, kGlyphEnd};
const uint8_t kStroke9[] = {4, 3, 0, 1, 5, kGlyphEnd};

const uint8_t *kStrokes[10] = {
    kStroke0, kStroke1, kStroke2, kStroke3, kStroke4,
    kStroke5, kStroke6, kStroke7, kStroke8, kStroke9};

} // namespace

GlyphPlayer g_glyph;

void GlyphPlayer::playDigit(uint8_t d)
{
    if (d > 9)
        d = 9;
    stroke = kStrokes[d];
    index = 0;
    timer = kGlyphStepSamples;
    blankTimer = 0;
    active = true;
    updateMask();
}

void GlyphPlayer::playNumber(int value)
{
    if (value < 0)
        value = 0;
    pendingCount = 0;
    if (value >= 10)
    {
        pendingDigits[pendingCount++] = (uint8_t)(value / 10);
        value %= 10;
    }
    pendingDigits[pendingCount++] = (uint8_t)value;
    pendingIndex = 0;
    playDigit(pendingDigits[0]);
}

void GlyphPlayer::tick()
{
    if (!active)
        return;

    if (blankTimer > 0)
    {
        ledMask = 0;
        if (--blankTimer == 0 && pendingIndex < pendingCount)
            playDigit(pendingDigits[pendingIndex]);
        return;
    }

    if (!stroke)
    {
        active = false;
        ledMask = 0;
        return;
    }

    if (--timer > 0)
        return;

    ++index;
    if (stroke[index] == kGlyphEnd)
    {
        ++pendingIndex;
        if (pendingIndex < pendingCount)
        {
            stroke = nullptr;
            blankTimer = 1920;
            ledMask = 0;
            return;
        }
        active = false;
        pendingCount = 0;
        stroke = nullptr;
        ledMask = 0;
        return;
    }
    timer = kGlyphStepSamples;
    updateMask();
}

void GlyphPlayer::updateMask()
{
    ledMask = 0;
    if (!stroke)
        return;
    uint8_t step = stroke[index];
    if (step == kGlyphAll)
        ledMask = 0x3F;
    else if (step < 6)
        ledMask = (uint8_t)(1u << step);
}
