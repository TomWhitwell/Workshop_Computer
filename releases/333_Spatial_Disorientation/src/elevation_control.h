// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
namespace spatial {
constexpr uint32_t kHeightLevel=2048;
constexpr uint32_t kLevelElevations=kHeightLevel|(kHeightLevel<<13);
inline uint32_t HeightAt(uint32_t packed,unsigned e){return (packed>>(e*13))&8191u;}
inline uint32_t PackElevations(uint32_t a,uint32_t b){return a|(b<<13);}
inline bool DecodeElevation(const uint8_t* data,size_t count,uint32_t& packed){
    if(count!=4)return false;
    for(size_t i=0;i<4;++i)if(data[i]>127)return false;
    uint32_t a=data[0]|(uint32_t(data[1])<<7),b=data[2]|(uint32_t(data[3])<<7);
    if(a>4096||b>4096)return false;
    packed=PackElevations(a,b);return true;
}
}
