#pragma once
#include <algorithm>
#include <cstdint>
namespace mtm {
constexpr int RATE = 24000, Q = 32768;
inline int32_t clip(int32_t x, int32_t lo = -32767, int32_t hi = 32767) {
    return x < lo ? lo : (x > hi ? hi : x);
}
// Bounded Q15 multiply, rounded toward zero. Callers must keep the signed
// product above INT32_MIN and below INT32_MAX. No 64-bit hot-path multiply.
inline int32_t mul(int32_t x, int32_t c) {
    int32_t p = x * c;
    return p < 0 ? -((-p) >> 15) : (p >> 15);
}
inline int32_t abs32(int32_t x) {
    return x == INT32_MIN ? INT32_MAX : (x < 0 ? -x : x);
}
inline int32_t approach(int32_t x, int32_t target, unsigned shift) {
    int32_t d = target - x;
    if (!d) {
        return x;
    }
    int32_t step = d >> shift;
    if (step == 0) {
        step = 1;
    }
    return x + step;
}
inline uint32_t random(uint32_t &r) {
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    return r;
}
int32_t ratio(const uint16_t *t, int32_t x);
int32_t limit(int32_t x);
// Fractional accumulation preserves weak decay/DC motion below one sample LSB.
inline int32_t carryStep(int32_t p, int32_t &r, unsigned bits) {
    p += r;
    int32_t value = p >> bits;
    r = p - value * (1 << bits);
    return value;
}
// Accumulate magnitude loss so alternating oscillator signs do not cancel
// sub-LSB damping. Apply each accumulated unit using the current sign.
inline void decayWithRemainder(int32_t &x, uint32_t &error, int32_t loss) {
    uint32_t sum = uint32_t(abs32(x)) * uint32_t(loss) + error;
    int32_t d = int32_t(sum >> 24);
    error = sum & 0xffffffu;
    x += x < 0 ? d : -d;
}
// Direct Sitar Morph controls, unipolar Q15 except bipolar Pick.
struct Parameters {
    int32_t string = 26214, jawari = 21299, swell = 21299, sympathy = 32767, pick = 0;
};
inline Parameters bounded(Parameters p) {
    return {clip(p.string, 0, 32767), clip(p.jawari, 0, 32767), clip(p.swell, 0, 32767),
            clip(p.sympathy, 0, 32767), clip(p.pick)};
}
} // namespace mtm
