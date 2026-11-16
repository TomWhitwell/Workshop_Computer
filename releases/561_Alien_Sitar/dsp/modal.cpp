#include "modal.hpp"
#include "tables.hpp"
#include "taraf_table.hpp"
namespace mtm {
ModalBank::ModalBank() {
    for (unsigned i = 0; i < count; ++i) {
        replaceMode(i);
    }
}

void ModalBank::replaceMode(unsigned i) {
    auto &m = modes[i];
    const auto &c = tables::taraf[notes[i / 5] - 36][i];
    const auto *base = &tables::modal[i * 5];
    int32_t slowLoss = (base[1] * inverseQ12 + 2048) >> 12;
    int32_t fastLoss = (base[3] * inverseQ12 + 2048) >> 12;
    m.aSlow = c[0] - int32_t((int64_t(c[0]) * (slowLoss - base[1])) / 33554432);
    m.aFast = c[1] - int32_t((int64_t(c[1]) * (fastLoss - base[3])) / 33554432);
    m.lossSlow = slowLoss;
    m.lossFast = fastLoss;
    static constexpr int weightsQ8[] = {1280, 256, 192, 128, 64};
    m.gain = (c[2] * weightsQ8[i % 5]) >> 8;
    m.s1 = m.s2 = m.f1 = m.f2 = 0;
}

void ModalBank::setNotes(const uint8_t (&value)[12]) {
    bool changed = false;
    for (unsigned i = 0; i < 12; ++i) {
        const uint8_t n = value[i] < 36 ? 36 : value[i] > 84 ? 84 : value[i];
        if (n != pending[i]) {
            pending[i] = n;
            changed = true;
        }
    }
    if (changed) {
        phase = TuningPhase::FadeOut;
    }
}

int32_t ModalBank::feedback(int32_t x, int32_t a) {
    return ((x >> 13) * a + (((x & 8191) * a) >> 13)) >> 4;
}

int32_t ModalBank::loss(int32_t x, int32_t l) {
    return (((x >> 12) * l + (((x & 4095) * l) >> 12)) >> 12);
}

int32_t ModalBank::advance(int32_t &y1, int32_t &y2, int32_t a, int32_t l, int32_t injection) {
    // a = 2r*cos(w), Q17; l = (1-r*r), Q24. Keeping the tiny loss separately
    // prevents long sympathetic decay times rounding to an undamped pole.
    int32_t y = feedback(y1, a) - y2 + loss(y2, l) + injection;
    if (y > stateLimit) {
        y = stateLimit;
        ++clampCount;
    } else if (y < -stateLimit) {
        y = -stateLimit;
        ++clampCount;
    }
    y2 = y1;
    y1 = y;
    return y;
}

void ModalBank::setDurationQ8(int32_t percentQ8) {
    percentQ8 = clip(percentQ8, 25 * 256, 400 * 256);
    if (durationQ8 != percentQ8) {
        durationQ8 = percentQ8;
        inverseQ12 = (100 * 256 * 4096) / durationQ8;
        updatesRemaining = count;
    }
}

int32_t ModalBank::next(int32_t bridge, int32_t dampQ15) {
    // Spread coefficient updates over 60 bank frames; the oscillator histories
    // remain intact. Adjust the radius and its feedback together so that the
    // fixed bank frequencies stay essentially unchanged.
    if (updatesRemaining) {
        unsigned i = updateIndex++;
        if (updateIndex == count) {
            updateIndex = 0;
        }
        auto &updated = modes[i];
        const int32_t *base = &tables::modal[i * 5];
        const auto &tuned = tables::taraf[notes[i / 5] - 36][i];
        int32_t slowLoss = (base[1] * inverseQ12 + 2048) >> 12;
        int32_t fastLoss = (base[3] * inverseQ12 + 2048) >> 12;
        updated.aSlow = tuned[0] - int32_t((int64_t(tuned[0]) * (slowLoss - base[1])) / 33554432);
        updated.aFast = tuned[1] - int32_t((int64_t(tuned[1]) * (fastLoss - base[3])) / 33554432);
        updated.lossSlow = slowLoss;
        updated.lossFast = fastLoss;
        --updatesRemaining;
    }
    if (phase == TuningPhase::FadeOut) {
        tuningFade -= 128;
        if (tuningFade <= 0) {
            tuningFade = 0;
            for (unsigned i = 0; i < 12; ++i) {
                notes[i] = pending[i];
            }
            replaceIndex = 0;
            phase = TuningPhase::Replace;
        }
    } else if (phase == TuningPhase::Replace) {
        replaceMode(replaceIndex++);
        if (replaceIndex == count) {
            phase = TuningPhase::FadeIn;
        }
    } else if (phase == TuningPhase::FadeIn) {
        tuningFade += 128;
        if (tuningFade >= 32768) {
            tuningFade = 32768;
            phase = TuningPhase::Stable;
        }
    }
    bridge = clip(bridge);
    const int32_t extra = mul(clip(dampQ15, 0, 32767), 35);
    int32_t sum = 0;
    for (auto &m : modes) {
        int32_t as = m.aSlow, af = m.aFast;
        if (extra) {
            as -= mul(as, extra);
            af -= mul(af, extra);
        }
        const int32_t injection = (bridge * m.gain) >> 8; // eight extra fractional state bits
        int32_t slow = advance(m.s1, m.s2, as, m.lossSlow + extra * 1024, injection);
        int32_t fast = advance(m.f1, m.f2, af, m.lossFast + extra * 1024, injection);
        sum += (slow - fast) / 1024;
    }
    return clip(mul(clip(sum >> 1), tuningFade));
}
} // namespace mtm
