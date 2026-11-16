#include "saved_settings.hpp"
#include "editor.hpp"
#include <climits>
#include <cstring>
namespace computer::saved {
namespace {
constexpr uint32_t magic = 0x3153544d; // MTS1 in little endian
constexpr uint32_t version = 1;
constexpr size_t crcOffset = 112;
void put(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) {
        p[i] = uint8_t(v >> (8 * i));
    }
}
uint32_t get(const uint8_t *p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) {
        v |= uint32_t(p[i]) << (8 * i);
    }
    return v;
}
uint32_t crc(const uint8_t *p, size_t n) {
    uint32_t v = 0xffffffffu;
    while (n--) {
        v ^= *p++;
        for (unsigned i = 0; i < 8; ++i) {
            v = (v >> 1) ^ (0xedb88320u & (0u - (v & 1u)));
        }
    }
    return ~v;
}
bool erased(const uint8_t *p) {
    for (size_t i = 0; i < pageSize; ++i) {
        if (p[i] != 0xff) {
            return false;
        }
    }
    return true;
}
bool affineValid(const Affine &a) {
    return a.offsetMv >= -500 && a.offsetMv <= 500 && a.scalePpm >= 800000 && a.scalePpm <= 1200000;
}
} // namespace
int32_t apply(const Affine &a, int32_t mv) {
    if (a.scalePpm == 1000000 && a.offsetMv == 0) {
        return mv;
    }
    int64_t v = int64_t(mv) * a.scalePpm / 1000000 + a.offsetMv;
    if (v > INT32_MAX) {
        return INT32_MAX;
    }
    if (v < INT32_MIN) {
        return INT32_MIN;
    }
    return int32_t(v);
}
bool fit(Affine &out, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (x0 < -6000 || x0 > 6000 || x1 < -6000 || x1 > 6000 || y0 < -6000 || y0 > 6000 ||
        y1 < -6000 || y1 > 6000) {
        return false;
    }
    int32_t dx = x1 - x0;
    if (dx > -1000 && dx < 1000) {
        return false;
    }
    int64_t scale = int64_t(y1 - y0) * 1000000 / dx;
    int64_t offset = int64_t(y0) - int64_t(x0) * scale / 1000000;
    if (scale < 800000 || scale > 1200000 || offset < -500 || offset > 500) {
        return false;
    }
    out = {int32_t(offset), int32_t(scale)};
    return true;
}
bool valid(const State &s) {
    for (unsigned i = 0; i < parameterCount; ++i) {
        if (s.parameters[i] < editor::descriptors[i].minimum ||
            s.parameters[i] > editor::descriptors[i].maximum) {
            return false;
        }
    }
    for (auto v : s.held) {
        if (v > 8) {
            return false;
        }
    }
    for (auto v : s.notes) {
        if (v < 36 || v > 84) {
            return false;
        }
    }
    return s.tonic <= 11 && s.preset <= 127 && affineValid(s.pitchIn) && affineValid(s.pitchOut);
}
void encode(uint8_t (&page)[pageSize], const State &s, uint32_t sequence) {
    std::memset(page, 0xff, pageSize);
    put(page, magic);
    put(page + 4, version);
    put(page + 8, sequence);
    for (unsigned i = 0; i < parameterCount; ++i) {
        put(page + 12 + 4 * i, uint32_t(s.parameters[i]));
    }
    std::memcpy(page + 56, s.held, 3);
    std::memcpy(page + 59, s.notes, noteCount);
    page[71] = s.tonic;
    page[72] = s.preset;
    put(page + 76, uint32_t(s.pitchIn.offsetMv));
    put(page + 80, uint32_t(s.pitchIn.scalePpm));
    put(page + 84, uint32_t(s.pitchOut.offsetMv));
    put(page + 88, uint32_t(s.pitchOut.scalePpm));
    put(page + crcOffset, crc(page, crcOffset));
}
bool decode(const uint8_t *page, State &s, uint32_t &sequence) {
    if (get(page) != magic || get(page + 4) != version ||
        get(page + crcOffset) != crc(page, crcOffset)) {
        return false;
    }
    State candidate;
    for (unsigned i = 0; i < parameterCount; ++i) {
        candidate.parameters[i] = int32_t(get(page + 12 + 4 * i));
    }
    std::memcpy(candidate.held, page + 56, 3);
    std::memcpy(candidate.notes, page + 59, noteCount);
    candidate.tonic = page[71];
    candidate.preset = page[72];
    candidate.pitchIn.offsetMv = int32_t(get(page + 76));
    candidate.pitchIn.scalePpm = int32_t(get(page + 80));
    candidate.pitchOut.offsetMv = int32_t(get(page + 84));
    candidate.pitchOut.scalePpm = int32_t(get(page + 88));
    if (!valid(candidate)) {
        return false;
    }
    s = candidate;
    sequence = get(page + 8);
    return true;
}
Location newest(const uint8_t *image, State &state) {
    Location best;
    for (unsigned sector = 0; sector < sectorCount; ++sector) {
        for (unsigned page = 0; page < sectorSize / pageSize; ++page) {
            State candidate;
            uint32_t sequence;
            if (decode(image + sector * sectorSize + page * pageSize, candidate, sequence) &&
                (!best.found || int32_t(sequence - best.sequence) > 0)) {
                best = {true, sector, page, sequence};
                state = candidate;
            }
        }
    }
    return best;
}
Target next(const uint8_t *image, const Location &previous) {
    unsigned sector = previous.found ? previous.sector : 0;
    for (unsigned page = 0; page < sectorSize / pageSize; ++page) {
        if (erased(image + sector * sectorSize + page * pageSize)) {
            return {sector, page, previous.found ? previous.sequence + 1 : 1, false};
        }
    }
    return {sector ^ 1u, 0, previous.found ? previous.sequence + 1 : 1, true};
}
} // namespace computer::saved
