// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#define spatial baseline
#include "reference/fir-unroll16-64/src/dsp/spatial.h"
#undef spatial
#include "../src/dsp/spatial.h"
#include <cassert>
#include <cstdio>
int main(){
for(int mode=0;mode<6;++mode){
baseline::Engine old;spatial::Engine fresh;baseline::Scene a;spatial::Scene b;baseline::Config ca;spatial::Config cb;uint32_t rng=7;
for(int i=0;i<96000;++i){
rng=rng*1664525u+1013904223u;
a.angle[0]=b.angle[0]=mode==0?0:mode==1?0xffffffffu:mode==2?uint32_t(i)*89478u:rng;
a.angle[1]=b.angle[1]=0u-a.angle[0];
a.distance=b.distance=mode<2?(mode?4095:0):int((rng>>8)&4095);
a.distance_b=b.distance_b=mode<2?-1:int((rng>>20)&4095);
a.fig8=b.fig8=mode>=3;a.movement=b.movement=mode==3?0:mode==4?1:2;
a.shape=b.shape=(rng>>4)&4095;a.excursion=b.excursion=(rng>>16)&4095;a.path_epoch=b.path_epoch=i/20000;
a.vertical_centre=b.vertical_centre=mode<2?2048:int((rng>>8)%4097);a.vertical_depth=b.vertical_depth=mode<2?0:int((rng>>4)%2049);
for(int n=0;n<5;++n)ca.value[n]=cb.value[n]=mode<2?(n==1?(mode?4095:0):4095):int((rng>>(n*3))&4095);
if(i%4096==0){uint32_t heights=mode>=3?0:(uint32_t(i/4096)%3)*5;old.SetElevation(baseline::PackElevations((heights&3)==1?0:(heights&3)==2?4096:2048,((heights>>2)&3)==1?0:((heights>>2)&3)==2?4096:2048));fresh.SetElevation(spatial::PackElevations((heights&3)==1?0:(heights&3)==2?4096:2048,((heights>>2)&3)==1?0:((heights>>2)&3)==2?4096:2048));}
int x=i<2048?0:i<4096?(i==2048?2047:0):i<8192?(i&1?-2048:2047):int(rng&4095)-2048;
int y=i<8192?-x/2:int((rng>>12)&4095)-2048;
auto ox=old.Process(x,y,a,ca);auto nx=fresh.Process(x,y,b,cb);assert(ox.left==nx.left&&ox.right==nx.right);
}
}
std::puts("PASS 576000 exact frames: static, motion, room off/max/changing, silence/impulse/full-scale, ring wraps and heights");
}
