#include "ConfigStore.h"

#include <cstring>

#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

namespace {
static const uint8_t* kFlashPtr = reinterpret_cast<const uint8_t*>(XIP_BASE + ConfigStore::kOffset);
static uint8_t sector_buf[ConfigStore::kBlockSize] __attribute__((aligned(4)));
static uint8_t wr_buf[ConfigStore::kBlockSize] __attribute__((aligned(4)));
}  // namespace

void ConfigStore::Load(bool force_reset) {
  std::memcpy(&config_, kFlashPtr, sizeof(Data));
  if (force_reset || config_.magic != kMagic) {
    config_ = Data{};
    Save();
  }
}

void ConfigStore::Save() {
  std::memcpy(sector_buf, kFlashPtr, kBlockSize);
  if (std::memcmp(&config_, sector_buf, sizeof(config_)) == 0) {
    return;
  }

  std::memcpy(wr_buf, sector_buf, kBlockSize);
  std::memcpy(wr_buf, &config_, sizeof(config_));

  const uint32_t ints = save_and_disable_interrupts();
  // Load() may call Save() from GridsCard's constructor before core1 is launched.
  // multicore_lockout_start_blocking() waits for core1's lockout victim — which
  // never arrives — so a blank config sector would hang forever on first boot.
  if (multicore_lockout_victim_is_initialized(1)) {
    multicore_lockout_start_blocking();
    flash_range_erase(kOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kOffset, wr_buf, kBlockSize);
    multicore_lockout_end_blocking();
  } else {
    flash_range_erase(kOffset, FLASH_SECTOR_SIZE);
    flash_range_program(kOffset, wr_buf, kBlockSize);
  }
  restore_interrupts(ints);
}

void ConfigStore::SaveData(const Data& data) {
  config_ = data;
  Save();
}

ConfigStore::Data& ConfigStore::Get() {
  return config_;
}
