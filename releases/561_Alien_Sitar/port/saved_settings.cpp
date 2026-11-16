/// @file saved_settings.cpp
/// @brief Implementation of the program-card settings journal.
///
/// Serialization (encode/decode), CRC and fitting helpers for the record
/// format documented in saved_settings.hpp and PERSISTENCE.md. This
/// translation unit is hardware-free and allocation-free: pages are plain
/// byte images, the record is validated in full before any state is
/// committed, and integer packing is done byte-wise so the layout is
/// independent of host endianness and alignment.
#include "saved_settings.hpp"
#include "editor.hpp"
#include <climits>
#include <cstring>
namespace computer::saved {
namespace {
/// @brief Record signature validated by decode().
constexpr uint32_t magic = 0x3153544d; // MTS1 in little endian
/// @brief Record schema version; decode() rejects other values.
constexpr uint32_t version = 1;
/// @brief Byte offset of the record CRC; bytes [0, crcOffset) are covered.
constexpr size_t crcOffset = 112;

/// @brief Store a uint32 in little-endian byte order.
/// @param p Destination (four bytes).
/// @param v Value to store.
void put(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) {
        p[i] = uint8_t(v >> (8 * i));
    }
}

/// @brief Load a little-endian uint32.
/// @param p Source (four bytes).
/// @return Reconstructed value.
uint32_t get(const uint8_t *p) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) {
        v |= uint32_t(p[i]) << (8 * i);
    }
    return v;
}

/// @brief CRC-32 (reflected, polynomial 0xEDB88320, initial value
///        0xffffffff, final complement; matches zlib) over n bytes.
/// @param p First byte of the range.
/// @param n Byte count.
/// @return CRC value; a change anywhere in the input flips it.
uint32_t crc(const uint8_t *p, size_t n) {
    uint32_t v = 0xffffffffu;
    while (n--) {
        v ^= *p++;
        for (unsigned i = 0; i < 8; ++i) {
            // (0u - bit) yields an all-ones or all-zeros mask, so the
            // conditional polynomial XOR runs branch-free.
            v = (v >> 1) ^ (0xedb88320u & (0u - (v & 1u)));
        }
    }
    return ~v;
}

/// @brief True when every byte of the page is 0xff (never programmed).
/// @param p First byte of a 256-byte page.
/// @return Blank-erased status of the page.
bool erased(const uint8_t *p) {
    for (size_t i = 0; i < pageSize; ++i) {
        if (p[i] != 0xff) {
            return false;
        }
    }
    return true;
}

/// @brief Bounds-check one affine correction against the record limits.
/// @param a Candidate correction.
/// @return Offset within +/-500 mV and scale within 800000..1200000 ppm.
bool affineValid(const Affine &a) {
    return a.offsetMv >= -500 && a.offsetMv <= 500 && a.scalePpm >= 800000 && a.scalePpm <= 1200000;
}
} // namespace

/// @brief Apply a user affine correction to a factory millivolt value.
/// @param a Correction to apply (identity passes through untouched).
/// @param mv Reading to correct, in millivolts.
/// @return Corrected millivolts, saturated to the int32 limits.
int32_t apply(const Affine &a, int32_t mv) {
    // Exact identity fast path skips rounding for unmodified calibration.
    if (a.scalePpm == 1000000 && a.offsetMv == 0) {
        return mv;
    }
    // 64-bit intermediate: mv * scale can reach ~2^46 before dividing.
    int64_t v = int64_t(mv) * a.scalePpm / 1000000 + a.offsetMv;
    if (v > INT32_MAX) {
        return INT32_MAX;
    }
    if (v < INT32_MIN) {
        return INT32_MIN;
    }
    return int32_t(v);
}

/// @brief Fit a user affine correction from two measured points.
/// @param out Receives the fitted correction only on success.
/// @param x0 First source reading, in millivolts.
/// @param y0 First target reading, in millivolts.
/// @param x1 Second source reading, in millivolts.
/// @param y1 Second target reading, in millivolts.
/// @return False on out-of-range points, an inadequate source span, or a
///         fit outside the recorded bounds.
bool fit(Affine &out, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (x0 < -6000 || x0 > 6000 || x1 < -6000 || x1 > 6000 || y0 < -6000 || y0 > 6000 ||
        y1 < -6000 || y1 > 6000) {
        return false;
    }
    int32_t dx = x1 - x0;
    // At least 1000 mV of span keeps the integer division meaningful.
    if (dx > -1000 && dx < 1000) {
        return false;
    }
    int64_t scale = int64_t(y1 - y0) * 1000000 / dx;
    int64_t offset = int64_t(y0) - int64_t(x0) * scale / 1000000;
    // The fitted line must remain plausible against the stored bounds.
    if (scale < 800000 || scale > 1200000 || offset < -500 || offset > 500) {
        return false;
    }
    out = {int32_t(offset), int32_t(scale)};
    return true;
}

/// @brief Range-check every field of a decoded or restored record.
/// @param s Candidate state.
/// @return True only when all fields lie within their acceptable ranges.
bool valid(const State &s) {
    // Mirror the editor schema so stored records stay protocol-consistent.
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
    // Taraf notes stay inside the MIDI-note window used by the editor.
    for (auto v : s.notes) {
        if (v < 36 || v > 84) {
            return false;
        }
    }
    return s.tonic <= 11 && s.preset <= 127 && affineValid(s.pitchIn) && affineValid(s.pitchOut);
}

/// @brief Serialize a state and sequence into one record page.
/// @param page 256-byte page buffer, fully overwritten by this call.
/// @param s State to serialize.
/// @param sequence Monotonic record number distinguishing generations.
void encode(uint8_t (&page)[pageSize], const State &s, uint32_t sequence) {
    std::memset(page, 0xff, pageSize);
    // Layout (little endian): signature, version, sequence, eleven int32
    // parameters, held[3], notes[12], tonic, preset, both CV corrections;
    // the CRC over bytes [0, crcOffset) lands at crcOffset.
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

/// @brief Validate and parse one candidate record page.
/// @param page First byte of a 256-byte page.
/// @param s Receives the decoded state only after full validation.
/// @param sequence Receives the stored monotonic record number.
/// @return False unless the header, CRC and bounds all check out.
bool decode(const uint8_t *page, State &s, uint32_t &sequence) {
    // Header and CRC reject torn or foreign pages before any state moves.
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
    // Commit only whole, validated records: no partial state escapes.
    s = candidate;
    sequence = get(page + 8);
    return true;
}

/// @brief Scan the whole journal for the newest valid record.
/// @param image Image of both sectors (2 * sectorSize bytes).
/// @param state Receives the winning record's state; untouched when no
///        record validates.
/// @return Location of the winner, or found == false for an empty or
///         entirely invalid journal.
Location newest(const uint8_t *image, State &state) {
    Location best;
    for (unsigned sector = 0; sector < sectorCount; ++sector) {
        for (unsigned page = 0; page < sectorSize / pageSize; ++page) {
            State candidate;
            uint32_t sequence;
            if (decode(image + sector * sectorSize + page * pageSize, candidate, sequence) &&
                // Wrap-safe comparison: a signed difference of the unsigned
                // counters ranks records across sequence rollover.
                (!best.found || int32_t(sequence - best.sequence) > 0)) {
                best = {true, sector, page, sequence};
                state = candidate;
            }
        }
    }
    return best;
}

/// @brief Pick the next write slot: the first erased page of the previous
///        sector, otherwise the other sector at page 0.
/// @param image Journal image to scan.
/// @param previous Location of the current newest record.
/// @return Write target; the sequence increments by one (1 when the
///         journal held no valid record), erase true only on rotation.
Target next(const uint8_t *image, const Location &previous) {
    unsigned sector = previous.found ? previous.sector : 0;
    for (unsigned page = 0; page < sectorSize / pageSize; ++page) {
        if (erased(image + sector * sectorSize + page * pageSize)) {
            return {sector, page, previous.found ? previous.sequence + 1 : 1, false};
        }
    }
    // Rotation: the other sector is erased wholesale, so the sector with
    // the previous record stays intact until the rotate completes.
    return {sector ^ 1u, 0, previous.found ? previous.sequence + 1 : 1, true};
}
} // namespace computer::saved
