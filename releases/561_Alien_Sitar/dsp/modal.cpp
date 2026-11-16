#include "modal.hpp"
#include "tables.hpp"
namespace mtm {
ModalBank::ModalBank() {
    for (int i = 0; i < count; i++) {
        auto &m = modes[i];
        m.aSlow = tables::modal[i * 5];
        m.lossSlow = tables::modal[i * 5 + 1];
        m.aFast = tables::modal[i * 5 + 2];
        m.lossFast = tables::modal[i * 5 + 3];
        m.gain = tables::modal[i * 5 + 4];
    }
}

int32_t ModalBank::feedback(int32_t x, int32_t a) {
    return (x >> 14) * a + (((x & 16383) * a) >> 14);
}

int32_t ModalBank::loss(int32_t x, int32_t l) {
    return (((x >> 12) * l + (((x & 4095) * l) >> 12)) >> 12);
}

int32_t ModalBank::advance(int32_t &y1, int32_t &y2, int32_t a, int32_t l, int32_t injection) {
    // a = 2r*cos(w), Q14; l = (1-r*r), Q24. Keeping the tiny loss separately
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

int32_t ModalBank::next(int32_t bridge, int32_t dampQ15) {
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
    return clip(sum >> 1);
}
} // namespace mtm
