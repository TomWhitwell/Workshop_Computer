// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Direct gravity-vector direction, using this project's existing integer atan table.
#pragma once
#include "dsp/fixed.h"
#include "dsp/fig8_table.h"
namespace spatial {
inline bool TiltActive(int32_t lateral,int32_t forward){
    return lateral<=-64||lateral>=64||forward<=-64||forward>=64;
}
// Zero front, quarter-turn right. Bounded one-division lookup at 1kHz on core0,
// not audio-rate trig or FIR. Caller holds the previous direction near neutral.
inline uint32_t TiltDirection(int32_t lateral,int32_t forward){
    uint32_t ax=lateral<0?-lateral:lateral,az=forward<0?-forward:forward;
    uint32_t major=ax>az?ax:az,minor=ax>az?az:ax;
    if(!major)return 0;
    uint32_t ratio=(minor<<10)/major,index=ratio>>2,f=ratio&3;
    uint32_t angle=kAtan[index];
    if(index<256)angle+=((kAtan[index+1]-angle)*f)>>2;
    if(ax>az)angle=0x40000000u-angle;
    if(forward<0)angle=0x80000000u-angle;
    if(lateral<0)angle=0u-angle;
    return angle;
}
}
