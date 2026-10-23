// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include "dsp/fixed.h"
namespace spatial {
class ElevationPickup {
public:
    void Rearm(){picked_=false;have_previous_=false;}
    void Start(uint32_t height){height_=applied_=height<=4096?height:2048;Rearm();}
    int Update(int32_t raw,uint32_t applied){
        if(applied!=applied_){applied_=applied;if(applied!=height_){height_=applied<=4096?applied:2048;Rearm();}}
        raw=Clamp(raw,0,4064);int32_t value=raw+(raw>>7);if(value==4095)value=4096;
        int32_t d=value-static_cast<int32_t>(height_),old=previous_-static_cast<int32_t>(height_);
        if((d>=-64&&d<=64)||(have_previous_&&((old<0&&d>=0)||(old>0&&d<=0))))picked_=true;
        previous_=value;have_previous_=true;
        if(!picked_||static_cast<uint32_t>(value)==height_)return -1;
        height_=value;return value;
    }
    bool Picked()const{return picked_;}
    uint32_t Height()const{return height_;}
private:
    int32_t previous_=0;uint32_t height_=2048,applied_=2048;
    bool picked_=false,have_previous_=false;
};
}
