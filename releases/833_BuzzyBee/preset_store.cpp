#include "preset_store.h"

#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

namespace
{
constexpr uint32_t kMagic = 0x425A5750; // "BZWP"
constexpr uint16_t kVersion = 1;
constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

struct SavedPresets
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    buzzypreset presets[7];
    uint32_t checksum;
};

static_assert(sizeof(SavedPresets) <= FLASH_PAGE_SIZE, "saved presets must fit one flash page");

uint32_t __not_in_flash_func(checksum)(const uint8_t *data, size_t size)
{
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < size; ++i)
    {
        value ^= data[i];
        value *= 16777619u;
    }
    return value;
}

bool valid(const SavedPresets &state)
{
    return state.magic == kMagic && state.version == kVersion && state.size == sizeof(SavedPresets) &&
           state.checksum == checksum(reinterpret_cast<const uint8_t *>(&state), offsetof(SavedPresets, checksum));
}
} // namespace

bool preset_store_load(buzzypreset presets[7])
{
    const auto *state = reinterpret_cast<const SavedPresets *>(XIP_BASE + kFlashOffset);
    if (!valid(*state))
    {
        return false;
    }
    std::memcpy(presets, state->presets, sizeof(state->presets));
    return true;
}

void __not_in_flash_func(preset_store_save)(const buzzypreset presets[7])
{
    SavedPresets state = {};
    state.magic = kMagic;
    state.version = kVersion;
    state.size = sizeof(SavedPresets);
    std::memcpy(state.presets, presets, sizeof(state.presets));
    state.checksum = checksum(reinterpret_cast<const uint8_t *>(&state), offsetof(SavedPresets, checksum));

    uint8_t page[FLASH_PAGE_SIZE];
    std::memset(page, 0xff, sizeof(page));
    std::memcpy(page, &state, sizeof(state));

    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(kFlashOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kFlashOffset, page, FLASH_PAGE_SIZE);
    restore_interrupts(interrupts);
}
