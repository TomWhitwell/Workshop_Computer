#include "reverb.h"
#include <cstring>

constexpr int CardReverb::kCombLen[];
constexpr int CardReverb::kAllpassLen[];

static constexpr int32_t kFeedback = 27525;
static constexpr int32_t kDamp1 = 6554;
static constexpr int32_t kDamp2 = 32768 - kDamp1;
static constexpr int32_t kApFeed = 16384;

void CardReverb::Reset()
{
    for (int c = 0; c < kNumCombs; c++)
    {
        memset(comb_[c], 0, sizeof(comb_[c]));
        combStore_[c] = 0;
        combIdx_[c] = 0;
    }
    for (int a = 0; a < kNumAllpass; a++)
    {
        memset(allpass_[a], 0, sizeof(allpass_[a]));
        apIdx_[a] = 0;
    }
}

int16_t CardReverb::Process(int16_t in)
{
    int32_t input = in >> 2;
    int32_t out = 0;

    for (int c = 0; c < kNumCombs; c++)
    {
        int len = kCombLen[c];
        int32_t y = comb_[c][combIdx_[c]];
        out += y;
        combStore_[c] = (y * kDamp2 + combStore_[c] * kDamp1) >> 15;
        int32_t v = input + ((combStore_[c] * kFeedback) >> 15);
        if (v > 8191)
            v = 8191;
        else if (v < -8192)
            v = -8192;
        comb_[c][combIdx_[c]] = (int16_t)v;
        if (++combIdx_[c] >= len)
            combIdx_[c] = 0;
    }
    out >>= 2;

    for (int a = 0; a < kNumAllpass; a++)
    {
        int len = kAllpassLen[a];
        int32_t buf = allpass_[a][apIdx_[a]];
        int32_t y = buf - out;
        int32_t v = out + ((buf * kApFeed) >> 15);
        if (v > 8191)
            v = 8191;
        else if (v < -8192)
            v = -8192;
        allpass_[a][apIdx_[a]] = (int16_t)v;
        if (++apIdx_[a] >= len)
            apIdx_[a] = 0;
        out = y;
    }

    out >>= 1;
    if (out > 2047)
        out = 2047;
    else if (out < -2048)
        out = -2048;
    return (int16_t)out;
}
