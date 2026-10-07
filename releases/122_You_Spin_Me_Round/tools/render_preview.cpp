// Copyright (c) 2026 soveda. SPDX-License-Identifier: MIT
// Host-only audition generator. Floating-point synthesis is not firmware DSP.
#include "dsp/rotary.h"
#include <cmath>
#include <cstdio>
#include <cstdint>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    FILE* output = std::fopen(argv[1], "wb");
    if (!output) return 2;
    spin::Rotary outside, inside;
    spin::Parameters p;
    p.intensity = 3200;
    p.drive = 700;
    spin::Parameters ip = p;
    ip.perspective = spin::Perspective::Inside;
    constexpr double pi = 3.14159265358979323846;
    // Three stereo pairs: dry, outside, inside, all at the same gain.
    for (int n = 0; n < 48'000 * 18; ++n) {
        double t = n / 48000.0;
        const double f = t < 6 ? 110 : (t < 12 ? 146.83238396 : 130.81278265);
        const double local = t < 6 ? t : (t < 12 ? t - 6 : t - 12);
        const double env = local < .01 ? local * 100 : 1.0;
        double organ = 0;
        constexpr double levels[] = {1.0, .55, .7, .3, .2, .1, .06, .04};
        for (int h = 1; h <= 8; ++h) organ += levels[h - 1] * std::sin(2 * pi * f * h * local);
        const int32_t input = static_cast<int32_t>(organ * 450 * env);
        if (n % 48 == 0) {
            p.fast = t >= 4;
            p.brake = t >= 12;
            ip.fast = p.fast;
            ip.brake = p.brake;
            outside.SetParameters(p); inside.SetParameters(ip);
        }
        const auto o = outside.Process(input), i = inside.Process(input);
        const int16_t channels[] = {
            static_cast<int16_t>(input * 8), static_cast<int16_t>(input * 8),
            static_cast<int16_t>(o.left * 8), static_cast<int16_t>(o.right * 8),
            static_cast<int16_t>(i.left * 8), static_cast<int16_t>(i.right * 8)};
        if (std::fwrite(channels, sizeof(int16_t), 6, output) != 6) return 3;
    }
    return std::fclose(output) == 0 ? 0 : 4;
}
