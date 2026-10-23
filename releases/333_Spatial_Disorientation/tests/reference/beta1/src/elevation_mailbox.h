// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "elevation_control.h"
namespace spatial {
// One self-contained aligned word per source. Core 0 alone publishes sequence
// and height together; core 1 alone consumes and updates renderer height state.
// RP2040 aligned 32-bit loads/stores are atomic; no pointers or extra payload
// are published, no wait/RMW lock is needed, and A/B cannot overwrite each other.
class ElevationMailbox {
public:
    void Publish(unsigned source,uint32_t height){
        if(source>1||height>4096)return;
        uint32_t previous=command_[source];
        command_[source]=((previous+8192u)&~8191u)|height;
    }
    uint32_t Consume(uint32_t heights){
        for(unsigned e=0;e<2;++e){
            uint32_t word=command_[e];
            if(word==consumed_[e])continue;
            consumed_[e]=word;
            heights=(heights&~(8191u<<(e*13)))|((word&8191u)<<(e*13));
        }
        return heights;
    }
private:
    alignas(4) volatile uint32_t command_[2]={}; // Core 0 writes.
    uint32_t consumed_[2]={}; // Core 1 only; never cleared by the producer.
};
}
