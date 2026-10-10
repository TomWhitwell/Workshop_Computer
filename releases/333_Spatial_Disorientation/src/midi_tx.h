// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
namespace spatial {
// Core-1-only bounded byte queue. Complete SysEx frames enqueue transactionally;
// the USB pump can consume partial writes without ever waiting for the host.
class MidiTx {
public:
    bool Enqueue(const uint8_t* data,size_t n) {
        if(n>sizeof(bytes_)-count_)return false;
        for(size_t i=0;i<n;++i)bytes_[(head_+count_+i)&255]=data[i];
        count_+=n;return true;
    }
    const uint8_t* Front(size_t& n) const {
        n=count_;if(n>sizeof(bytes_)-head_)n=sizeof(bytes_)-head_;if(n>64)n=64;
        return bytes_+head_;
    }
    void Consume(size_t n){if(n>count_)n=count_;head_=(head_+n)&255;count_-=n;}
    bool Empty()const{return count_==0;}
    void Clear(){head_=count_=0;}
private:
    uint8_t bytes_[256]={};size_t head_=0,count_=0;
};
}
