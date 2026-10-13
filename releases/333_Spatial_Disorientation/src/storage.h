// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include "settings.h"
#include <cstring>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
namespace spatial {
extern "C" char __flash_binary_end;
class Storage {
public:
    void Init(uint32_t bytes) {
        // ComputerCard 0.4.0 probes actual capacity before main(). Reserve only
        // the final 4 KB, and reject any overlap with the linked firmware image.
        valid_=bytes>=2*1024*1024 && bytes<=16*1024*1024 && (bytes&4095)==0;
        offset_=valid_ ? bytes-4096 : 0;
        valid_=valid_ && reinterpret_cast<uintptr_t>(&__flash_binary_end)<XIP_BASE+offset_;
    }
    Settings Load() const {
        if(!valid_)return {};
        return LoadRecord(Address(),sizeof(Record)); // Migrates v1/v2/v3 in RAM; never writes at boot.
    }
    // Core 1 only, after core 0 acknowledges muted audio. Both cores execute
    // from RAM; lock out core 0 and disable local interrupts around flash access.
    bool Save(const Settings& cfg) {
        if (!valid_ || !cfg.Valid()) return false;
        Record record=MakeRecord(cfg);
        if (std::memcmp(Address(),&record,sizeof(record))==0) return true;
        std::memset(page_,0xff,sizeof(page_));
        std::memcpy(page_,&record,sizeof(record));
        if (!multicore_lockout_start_timeout_us(500000)) return false;
        uint32_t irq=save_and_disable_interrupts();
        flash_range_erase(offset_,FLASH_SECTOR_SIZE);
        flash_range_program(offset_,page_,FLASH_PAGE_SIZE);
        restore_interrupts(irq);
        multicore_lockout_end_blocking();
        return std::memcmp(Address(),&record,sizeof(record))==0;
    }
private:
    const uint8_t* Address() const { return reinterpret_cast<const uint8_t*>(XIP_BASE+offset_); }
    alignas(4) uint8_t page_[FLASH_PAGE_SIZE]={};
    uint32_t offset_=0;
    bool valid_=false;
};
}
