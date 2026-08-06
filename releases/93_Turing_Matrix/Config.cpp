#include "Config.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <cstring>
#include "hardware/regs/addressmap.h"
#include "pico/multicore.h"

uint32_t const Config::MAGIC = 0x434F4E46;
size_t const Config::FLASH_SIZE = 2 * 1024 * 1024;
size_t const Config::BLOCK_SIZE = 4096;
size_t const Config::OFFSET = Config::FLASH_SIZE - Config::BLOCK_SIZE;

static const uint8_t *CONFIG_FLASH_PTR = reinterpret_cast<const uint8_t *>(XIP_BASE + Config::OFFSET);

// Config.cpp  – put these lines near the top of the file
static uint8_t sector_buf[Config::BLOCK_SIZE] __attribute__((aligned(4)));
static uint8_t wr_buf[Config::BLOCK_SIZE] __attribute__((aligned(4)));

void Config::load(bool forceReset)
{
    std::memcpy(&config, CONFIG_FLASH_PTR, sizeof(Data));

    if (config.magic != Config::MAGIC || forceReset)
    {
        config = Data(); // Reset to defaults
        save();
    }

    config.divide = 5;
    if (config.vactrol.law > 1)
        config.vactrol.law = 0;
    if (config.vactrol.relation > 2)
        config.vactrol.relation = 0;
    if (config.vactrol.min1 > config.vactrol.max1)
        config.vactrol.max1 = config.vactrol.min1;
    if (config.vactrol.min2 > config.vactrol.max2)
        config.vactrol.max2 = config.vactrol.min2;
}

/*
void Config::save()
{
    uint8_t flash_copy[BLOCK_SIZE];
    std::memcpy(flash_copy, CONFIG_FLASH_PTR, BLOCK_SIZE);

    if (std::memcmp(&config, flash_copy, sizeof(Data)) == 0)
        return; // No change, skip

    uint8_t temp[BLOCK_SIZE] = {0};
    std::memcpy(temp, &config, sizeof(Data));

    //     // Stop all other execution paths
    multicore_lockout_start_blocking();

    //     // Kill interrupts
    uint32_t ints = save_and_disable_interrupts();

    //     // Do the write
    // flash_range_erase(OFFSET, FLASH_SECTOR_SIZE);
    // flash_range_program(OFFSET, temp, BLOCK_SIZE);

    //     // Restore

    restore_interrupts(ints);
    multicore_lockout_end_blocking();
}
*/

void Config::save()
{
    /* ---------- 1. Compare ---------- */
    memcpy(sector_buf, CONFIG_FLASH_PTR, Config::BLOCK_SIZE);
    if (memcmp(&config, sector_buf, sizeof config) == 0)
        return; // no change

    /* ---------- 2. Prepare write buffer ---------- */
    memcpy(wr_buf, sector_buf, Config::BLOCK_SIZE); // old sector
    memcpy(wr_buf, &config, sizeof config);         // patch with new data

    /* ---------- 3. Critical section ---------- */
    uint32_t ints = save_and_disable_interrupts(); // 1) IRQs off
    multicore_lockout_start_blocking();            // 2) park Core 1

    // --- flash operations (uncomment when ready) ---
    flash_range_erase(OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(OFFSET, wr_buf, Config::BLOCK_SIZE);

    multicore_lockout_end_blocking(); // 3) release Core 1
    restore_interrupts(ints);         // 4) IRQs on

}

Config::Data &Config::get()
{
    return config;
}
