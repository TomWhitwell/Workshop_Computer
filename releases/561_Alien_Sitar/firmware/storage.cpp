#include "storage.hpp"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include <cstring>
namespace computer::storage {
namespace {
constexpr uint32_t offset = 2 * 1024 * 1024 - saved::sectorCount * saved::sectorSize;
bool available = false;
const uint8_t *image() {
    return reinterpret_cast<const uint8_t *>(XIP_BASE + offset);
}
} // namespace
void initialise(uint32_t flashBytes) {
    available = flashBytes >= offset + saved::sectorCount * saved::sectorSize;
}
bool load(saved::State &state) {
    return available && saved::newest(image(), state).found;
}
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
    return std::memcmp(image() + target.sector * saved::sectorSize + target.page * saved::pageSize,
                       page, sizeof page) == 0;
}
} // namespace computer::storage
