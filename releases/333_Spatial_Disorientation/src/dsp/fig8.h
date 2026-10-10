// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include "fixed.h"
#include "fig8_table.h"
namespace spatial {
struct Fig8Point {uint32_t angle;int32_t distance;};
// Gerono figure eight, translated in front of the listener: lateral=sin(p),
// depth=sin(2p). At large excursion/depth its lobes also pass behind the head.
// Run in the block worker at the staggered geometry cadence, never in the ISR.
inline Fig8Point CartesianPosition(int32_t lateral,int32_t depth){
    uint32_t ax=lateral<0?-lateral:lateral,az=depth<0?-depth:depth;
    uint32_t major=ax>az?ax:az,minor=ax>az?az:ax;
    if(!major)return {0,0};
    uint32_t ratio=(minor<<10)/major,index=ratio>>2,f=ratio&3;
    uint32_t angle=kAtan[index],scale=kRadius[index];
    if(index<256){angle+=((kAtan[index+1]-angle)*f)>>2;scale+=((kRadius[index+1]-scale)*f)>>2;}
    if(ax>az)angle=0x40000000u-angle;
    if(depth<0)angle=0x80000000u-angle;
    if(lateral<0)angle=0u-angle;
    int32_t radius=(major*scale)>>12;
    return {angle,Clamp(((radius-1024)*4095)>>13,0,4095)};
}
inline Fig8Point FigureEight(uint32_t phase,int32_t shape,int32_t excursion){
    int32_t lateral=((Sin(phase)>>3)*excursion)>>12;
    int32_t depth=((Sin(phase*2)>>3)*excursion)>>12;
    depth=2048+((depth*(1024+((shape*5)>>2)))>>12);
    return CartesianPosition(lateral,depth);
}
}
