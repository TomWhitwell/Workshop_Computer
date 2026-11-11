#include "fixed.hpp"
#include "tables.hpp"
namespace mtm {
int32_t ratio(const uint16_t *t, int32_t x) {
    x = clip(x, 0, 32767);
    unsigned i = unsigned(x) >> 7, f = unsigned(x) & 127;
    return t[i] + ((int32_t(t[i + 1]) - t[i]) * int32_t(f) >> 7);
}

int32_t limit(int32_t x) {
    int32_t a = std::min<int32_t>(abs32(x), 4 * Q);
    unsigned i = unsigned(a) >> 7, f = unsigned(a) & 127;
    if (i >= 1024) {
        return x < 0 ? -tables::limiter[1024] : tables::limiter[1024];
    }
    int32_t y =
        tables::limiter[i] + ((tables::limiter[i + 1] - tables::limiter[i]) * int32_t(f) >> 7);
    return x < 0 ? -y : y;
}
} // namespace mtm
