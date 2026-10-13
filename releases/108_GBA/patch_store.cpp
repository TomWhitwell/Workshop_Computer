// patch_store.cpp — see patch_store.h for why this is more delicate than it looks.

#include "patch_store.h"

#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include <cstring>

#define SECTOR_BYTES 4096u

static_assert(GBA_PATCH_SLOTS * GBA_PATCH_SLOT_BYTES == SECTOR_BYTES,
              "the slot table must be exactly one flash sector: the sector is the erase unit");

// The last sector of the 2 MB part. Firmware is under 60 kB, so there is no chance of collision,
// and putting it at the very end keeps it clear of anything the linker might grow into.
static constexpr uint32_t kFlashOffset = PICO_FLASH_SIZE_BYTES - SECTOR_BYTES;

static const uint8_t *slot_ptr(int slot)
{
    return (const uint8_t *)(XIP_BASE + kFlashOffset + (uint32_t)slot * GBA_PATCH_SLOT_BYTES);
}

// A stored slot begins with the same two magic bytes and version the GBA's Patch does. Erased
// flash reads as 0xFF, so a never-written slot fails this and reads as empty rather than as a
// patch full of 0xFF.
static bool slot_valid(const uint8_t *p)
{
    return p[0] == 0x42 && p[1] == 0x47;      // 'B','G' little-endian PATCH_MAGIC
}

bool patch_store_load(int slot, uint8_t *out)
{
    if (slot < 0 || slot >= GBA_PATCH_SLOTS) return false;
    const uint8_t *p = slot_ptr(slot);
    if (!slot_valid(p)) return false;
    memcpy(out, p, GBA_PATCH_SLOT_BYTES);
    return true;
}

uint16_t patch_store_used(void)
{
    uint16_t mask = 0;
    for (int i = 0; i < GBA_PATCH_SLOTS; i++)
        if (slot_valid(slot_ptr(i))) mask |= (uint16_t)(1u << i);
    return mask;
}

// Runs with XIP disabled and the other core parked, so it must touch nothing in flash: no
// function calls that are not inlined, no constant tables, no library routines. Everything it
// needs arrives in the RAM buffer pointed at by `param`.
struct SectorWrite { uint8_t *buf; };

static void __not_in_flash_func(do_sector_write)(void *param)
{
    SectorWrite *w = (SectorWrite *)param;
    flash_range_erase(kFlashOffset, SECTOR_BYTES);
    flash_range_program(kFlashOffset, w->buf, SECTOR_BYTES);
}

bool patch_store_save(int slot, const uint8_t *in)
{
    if (slot < 0 || slot >= GBA_PATCH_SLOTS) return false;

    // The erase unit is the whole sector, so every save is a read-modify-write of all sixteen
    // slots. 4 kB of stack would be reckless on core 1; this lives in .bss.
    static uint8_t sector[SECTOR_BYTES];
    memcpy(sector, (const void *)(XIP_BASE + kFlashOffset), SECTOR_BYTES);
    memcpy(sector + (uint32_t)slot * GBA_PATCH_SLOT_BYTES, in, GBA_PATCH_SLOT_BYTES);

    SectorWrite w = { sector };
    // 1500 ms is generous: a sector erase is a few milliseconds. The timeout exists so a missing
    // lockout victim fails cleanly instead of wedging the link engine for ever.
    int rc = flash_safe_execute(do_sector_write, &w, 1500);
    return rc == PICO_OK;
}
