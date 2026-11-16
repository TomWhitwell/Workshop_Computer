/// @file fixed.cpp
/// @brief Curve-table lookups shared by the fixed-point DSP.
///
/// Implements the two curve helpers declared in fixed.hpp on top of the
/// coefficient tables generated into tables.hpp. Both run in the Core1 audio
/// worker's hot path: bounded integer arithmetic, no allocation, no floating
/// point.
#include "fixed.hpp"
#include "tables.hpp"
namespace mtm {
int32_t ratio(const uint16_t *t, int32_t x) {
    x = clip(x, 0, 32767);
    // 128-step cells: entry x >> 7, low 7 bits as the linear fraction.
    unsigned i = unsigned(x) >> 7, f = unsigned(x) & 127;
    return t[i] + ((int32_t(t[i + 1]) - t[i]) * int32_t(f) >> 7);
}

int32_t limit(int32_t x) {
    // |x| above 4 * Q needs no table fraction; the guard below saturates it.
    int32_t a = std::min<int32_t>(abs32(x), 4 * Q);
    unsigned i = unsigned(a) >> 7, f = unsigned(a) & 127;
    // i == 1024 only at a == 4 * Q; reading limiter[i + 1] would be out of
    // bounds, so return the curve's top entry directly.
    if (i >= 1024) {
        return x < 0 ? -tables::limiter[1024] : tables::limiter[1024];
    }
    int32_t y =
        tables::limiter[i] + ((tables::limiter[i + 1] - tables::limiter[i]) * int32_t(f) >> 7);
    return x < 0 ? -y : y;
}
} // namespace mtm
