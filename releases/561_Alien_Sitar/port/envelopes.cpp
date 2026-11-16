/**
 * @file envelopes.cpp
 * @brief Asymmetric follower arithmetic behind the auxiliary level outputs.
 *
 * Full-scale attack needs 128 frames (~5.3 ms at 24 kHz) and full-scale
 * release 4096 frames (~171 ms); the one-unit minimum step keeps quiet tails
 * moving. These levels feed LED brightness, CV2 and the bloom detector's
 * input averaging, never the audio path.
 *
 *
 * @date 2026-11-16
 */
#include "envelopes.hpp"
namespace computer {
int32_t Envelopes::follow(int32_t x, int32_t goal) {
    // Faster rising (shift 7) than falling (shift 12): snappy attack, slow decay.
    return mtm::approach(x, goal, goal > x ? 7 : 12);
}

void Envelopes::next(int32_t direct, int32_t wet) {
    // Magnitudes only: polarity of the audio is irrelevant to level telemetry.
    dry = follow(dry, mtm::abs32(direct));
    halo = follow(halo, mtm::abs32(wet));
}

int32_t Envelopes::haloMv() const {
    // 600 Q15 represents the follower's nominal full-scale 5 V mapping.
    return mtm::clip(halo * 5000 / 600, 0, 5000);
}
} // namespace computer
