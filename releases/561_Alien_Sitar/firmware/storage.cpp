/// @file storage.cpp
/// @brief RP2040 flash implementation of the program-card settings journal.
///
/// The journal occupies two 4 KiB sectors, offsets 0x1fe000-0x1fffff of the
/// standard 2 MiB program prefix (linker-reserved); records are 256-byte
/// little-endian pages carrying a schema version, sequence and CRC32, as
/// encoded by saved_settings.hpp. Saves append to blank pages and rotate
/// between the two sectors: when the current sector is full the other is
/// erased and receives the next record while the previous sector stays
/// intact, so a torn or interrupted write leaves the previous record
/// readable. The build is copy-to-RAM: normal code and the active runtime
/// vector table live in SRAM, so the SDK flash helpers - which use the boot
/// ROM, restore XIP from a RAM boot2 copy, and do not mask interrupts - can
/// erase/program while the DMA/PWM/USB handlers keep running (fault/NMI
/// stubs remain in flash). This foreground path is the only runtime
/// reader/writer of the journal; real save latency and audio continuity
/// remain physical measurements.
#include "storage.hpp"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include <cstring>
namespace computer::storage {
namespace {
// Start of the reserved journal = 0x1fe000, matching the linker region limit.
constexpr uint32_t offset = 2 * 1024 * 1024 - saved::sectorCount * saved::sectorSize;
// Set by initialise(); all later journal operations require it to be true.
bool available = false;
/// @brief XIP-mapped pointer to the journal image, used for reads only.
/// @return Base of the reserved sectors as seen through the execute-in-place
///         window; never dereferenced during erase/programming.
const uint8_t *image() {
    return reinterpret_cast<const uint8_t *>(XIP_BASE + offset);
}
} // namespace
/// @brief Full contract in storage.hpp; records the detected capacity check.
/// @param flashBytes Detected flash size; @p available becomes true only when
///        the card covers the whole reserved journal region.
void initialise(uint32_t flashBytes) {
    available = flashBytes >= offset + saved::sectorCount * saved::sectorSize;
}
/// @brief Loads the newest valid journal record from XIP (see storage.hpp).
/// @param state Receives the record's settings when found.
/// @return True exactly when a valid record was decoded into @p state.
/// @note Pure read: no flash erasing or programming ever happens here.
bool load(saved::State &state) {
    return available && saved::newest(image(), state).found;
}
/// @brief Explicit-Save journal append: decode state, erase if needed, program.
///
/// Foreground path on core0 (Card constructor control tick or USB editor); the
/// only caller that touches flash write operations. Steps: locate the previous
/// record and the next append target, encode the validated state into a
/// 256-byte stack page (no allocation), optionally erase the exhausted sector
/// (never the one holding the previous record), program the page, and verify
/// by comparing the programmed bytes against the encoded page.
/// @param state Complete settings snapshot; @c saved::valid is required first.
/// @return True when the programmed page reads back identical (see memcmp
///         below); false also guards a missing capacity or an invalid state.
bool save(const saved::State &state) {
    if (!available || !saved::valid(state)) {
        return false;
    }
    saved::State old;
    auto previous = saved::newest(image(), old);
    uint8_t page[saved::pageSize];
    auto target = saved::next(image(), previous);
    saved::encode(page, state, target.sequence);
    // copy_to_ram places normal code, data, and the active IRQ table in SRAM.
    // Installed DMA/PWM/USB handlers run there; default fault/NMI stubs remain
    // in flash. SDK flash functions use boot ROM and leave IRQs enabled.
    // This foreground is the only runtime reader/writer of the journal.
    if (target.erase) {
        flash_range_erase(offset + target.sector * saved::sectorSize, saved::sectorSize);
    }
    uint32_t address = offset + target.sector * saved::sectorSize + target.page * saved::pageSize;
    flash_range_program(address, page, saved::pageSize);
    // Read-back verification through XIP: only a byte-identical programmed
    // page proves the record landed, so the compare result is the save result.
    return std::memcmp(image() + target.sector * saved::sectorSize + target.page * saved::pageSize,
                       page, sizeof page) == 0;
}
} // namespace computer::storage
