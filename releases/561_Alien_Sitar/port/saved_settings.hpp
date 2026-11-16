#pragma once
#include <cstddef>
#include <cstdint>

namespace computer::saved {
// One page is the atomic *record*, not an assumption of atomic flash writes.
// A partially programmed page is ignored unless its complete CRC validates.
constexpr size_t pageSize = 256, sectorSize = 4096, sectorCount = 2;
constexpr unsigned parameterCount = 11, noteCount = 12;
struct Affine {
    int32_t offsetMv = 0;
    int32_t scalePpm = 1000000;
};
struct State {
    int32_t parameters[parameterCount] = {2048, 2048, 2048, 32767, -1, 0, 17, 50, 100, 100, 50};
    uint8_t held[3] = {1, 2, 3};
    uint8_t notes[noteCount] = {61, 59, 62, 62, 64, 66, 68, 69, 71, 73, 74, 76};
    uint8_t tonic = 0, preset = 0;
    Affine pitchIn, pitchOut;
};
// Scale applies after factory input calibration, or before factory output
// calibration. Offset is millivolts. Bounds cap accidental extreme corrections.
int32_t apply(const Affine &a, int32_t mv);
// Fit y = scale*x + offset to two explicitly measured points in millivolts.
// Returns false for inadequate span, implausible measurements or bounds.
bool fit(Affine &out, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
bool valid(const State &s);
void encode(uint8_t (&page)[pageSize], const State &s, uint32_t sequence);
bool decode(const uint8_t *page, State &s, uint32_t &sequence);
struct Location {
    bool found = false;
    unsigned sector = 0, page = 0;
    uint32_t sequence = 0;
};
Location newest(const uint8_t *image, State &state);
// Caller writes to erased pages only. If erase is true, erase the indicated
// sector first, leaving the sector containing the previous record untouched.
struct Target {
    unsigned sector, page;
    uint32_t sequence;
    bool erase;
};
Target next(const uint8_t *image, const Location &previous);
} // namespace computer::saved
