/// @file saved_settings.hpp
/// @brief Program-card settings journal: record layout, validation and
///        two-sector rotation bookkeeping.
///
/// Pure serialization/validation layer for the persistence feature (see
/// PERSISTENCE.md). Callers own the hardware: every function takes an
/// image of the reserved two-sector journal, and nothing here touches
/// flash, allocates or blocks. Records are 256-byte little-endian pages;
/// they are written only by the explicit foreground Save path, never from
/// the audio path. Boot and Restore accept only the newest complete,
/// version-checked, CRC-valid and range-validated record.
#pragma once
#include <cstddef>
#include <cstdint>

namespace computer::saved {
// One page is the atomic *record*, not an assumption of atomic flash writes.
// A partially programmed page is ignored unless its complete CRC validates.
/// @brief Journal geometry: 256-byte record pages inside a rotating pair
///        of 4 KiB sectors (16 pages each).
constexpr size_t pageSize = 256, sectorSize = 4096, sectorCount = 2;
/// @brief Persisted item counts: eleven instrument parameters (editor
///        Parameter ordinals 0..ResonatorMix) and twelve taraf notes.
constexpr unsigned parameterCount = 11, noteCount = 12;

/// @brief User affine correction for one CV path: result = scale * mv + offset.
struct Affine {
    /// Millivolt offset correction, bounded to +/-500.
    int32_t offsetMv = 0;
    /// Scale correction in ppm, bounded to 800000..1200000 (0.8x..1.2x).
    int32_t scalePpm = 1000000;
};

/// @brief In-memory settings snapshot carried to and from the journal.
/// @details parameters[] mirrors editor::Parameter ordinals 0..ResonatorMix
/// (Jawari..ResonatorMix) and held[] carries the three HeldAssignment
/// storage IDs for Main/X/Y; the default values match the editor
/// descriptor initials.
struct State {
    /// Instrument parameter values in editor units (see editor::descriptors).
    int32_t parameters[parameterCount] = {2048, 2048, 2048, 32767, -1, 0, 17, 50, 100, 100, 50};
    /// Held-assignment storage IDs for Main/X/Y (0..HeldAssignmentMax).
    uint8_t held[3] = {1, 2, 3};
    /// Twelve taraf chord MIDI notes (36..84).
    uint8_t notes[noteCount] = {61, 59, 62, 62, 64, 66, 68, 69, 71, 73, 74, 76};
    /// Tonic class (0..11) and preset selection (0..127), carried in the
    /// record and bounds-checked by valid().
    uint8_t tonic = 0, preset = 0;
    /// User CV1 input / pitch output corrections saved with the record.
    Affine pitchIn, pitchOut;
};

// Scale applies after factory input calibration, or before factory output
// calibration. Offset is millivolts. Bounds cap accidental extreme corrections.
/// @brief Apply a user affine correction to a factory millivolt value.
/// @param a Correction to apply (identity passes through untouched).
/// @param mv Reading to correct, in millivolts.
/// @return Corrected millivolts; a 64-bit intermediate prevents overflow
///         and out-of-range results saturate to the int32 limits.
int32_t apply(const Affine &a, int32_t mv);

// Fit y = scale*x + offset to two explicitly measured points in millivolts.
// Returns false for inadequate span, implausible measurements or bounds.
/// @brief Fit a user affine correction from two measured points.
/// @param out Receives the fitted correction only on success.
/// @param x0 First source reading, in millivolts.
/// @param y0 First target reading, in millivolts.
/// @param x1 Second source reading, in millivolts.
/// @param y1 Second target reading, in millivolts.
/// @return False when any input lies outside +/-6000 mV, the source span
///         is under 1000 mV, or the fit falls outside the Affine bounds.
bool fit(Affine &out, int32_t x0, int32_t y0, int32_t x1, int32_t y1);

/// @brief Range-check every field of a decoded or restored record.
/// @param s Candidate state.
/// @return True only when every parameter lies within its editor
///         descriptor bounds, held IDs are at most 8, notes are 36..84,
///         tonic is at most 11, preset at most 127 and both affine
///         corrections lie within their bounds.
bool valid(const State &s);

/// @brief Serialize a state and sequence into one record page.
/// @param page 256-byte page buffer, fully overwritten by this call.
/// @param s State to serialize.
/// @param sequence Monotonic record number distinguishing generations.
void encode(uint8_t (&page)[pageSize], const State &s, uint32_t sequence);

/// @brief Validate and parse one candidate record page.
/// @param page First byte of a 256-byte page.
/// @param s Receives the decoded state only after full validation.
/// @param sequence Receives the stored monotonic record number.
/// @return False unless magic, schema version, CRC and every bounds check
///         pass; a valid candidate is committed to the caller's state whole.
bool decode(const uint8_t *page, State &s, uint32_t &sequence);

/// @brief Journal cursor describing the newest valid record found.
struct Location {
    /// False when the journal contained no valid record.
    bool found = false;
    /// Which sector (0..1), page (0..15) and monotonic sequence number the
    /// winning record came from; absent a record, zeros plus found == false.
    unsigned sector = 0, page = 0;
    uint32_t sequence = 0;
};

/// @brief Scan the whole journal for the newest valid record.
/// @param image Image of both sectors (2 * sectorSize bytes).
/// @param state Receives the winning record's state; untouched when no
///        record validates.
/// @return Location of the winner, or found == false for an empty or
///         entirely invalid journal.
Location newest(const uint8_t *image, State &state);

// Caller writes to erased pages only. If erase is true, erase the indicated
// sector first, leaving the sector containing the previous record untouched.
/// @brief Chosen write slot for the next record.
struct Target {
    /// Destination sector (0..1) and page (0..15).
    unsigned sector, page;
    /// Monotonic sequence number to stamp on the new record.
    uint32_t sequence;
    /// When true, the caller must erase the destination sector before
    /// programming it.
    bool erase;
};

/// @brief Pick the next write slot: the first erased page of the previous
///        sector, otherwise the other sector at page 0 (with erase).
/// @param image Journal image to scan.
/// @param previous Location of the current newest record.
/// @return Write target; its sequence increments the previous record's
///         number by one (starting at 1 for an empty journal).
Target next(const uint8_t *image, const Location &previous);
} // namespace computer::saved
