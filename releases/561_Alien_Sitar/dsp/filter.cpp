/// @file filter.cpp
/// @brief Sparse symmetric 17-tap halfband FIR implementation.
///
/// Compile-time validation of the halfband coefficient shape plus the
/// mirrored-history FIR that converts between the 24 kHz voice rate and the
/// 12 kHz modal bank rate. Integer only, fixed state, no allocation.
#include "filter.hpp"
#include "tables.hpp"
namespace mtm {
namespace {
// The specialised filter below requires symmetric coefficients and zero even
// taps except the centre. Bound the absolute coefficient sum so any accumulation
// order stays within int32 for inputs clipped to +/-32767.
constexpr bool validHalfband() {
    int32_t magnitude = 0;
    for (unsigned i = 0; i < 17; i++) {
        const int32_t c = tables::halfband[i];
        if (c != tables::halfband[16 - i] || (i != 8 && !(i & 1u) && c != 0)) {
            return false;
        }
        magnitude += c < 0 ? -c : c;
    }
    return magnitude <= 65536;
}
static_assert(validHalfband(), "FIR optimisation requires bounded sparse symmetric coefficients");

} // namespace

int32_t Fir::next(int32_t x) {
    // The new sample is stored twice so p[0..16] holds the 17 newest samples
    // with no per-tap modulo addressing.
    history[head] = history[head + 17] = clip(x);
    const int32_t *p = history + head;
    // Symmetric pairs share one multiply; zero coefficients require no loads.
    int32_t sum = (p[1] + p[15]) * tables::halfband[1] + (p[3] + p[13]) * tables::halfband[3] +
                  (p[5] + p[11]) * tables::halfband[5] + (p[7] + p[9]) * tables::halfband[7] +
                  p[8] * tables::halfband[8];
    // Ring head walks backwards; next frame's newest sample lands one slot lower.
    head = head ? head - 1 : 16;
    // Q15 output scale (coefficient sum is bounded to 65536 above).
    return sum >> 15;
}
} // namespace mtm
