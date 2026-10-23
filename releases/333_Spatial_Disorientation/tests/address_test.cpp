// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#define spatial baseline
#include "reference/hrtf-address-cache-64/src/dsp/hrtf.h"
#undef spatial
#include "dsp/hrtf.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
    baseline::Hrtf old;spatial::Hrtf fresh;uint32_t rng=7;unsigned comparisons=0;
    // Standalone constructor/default addressing before the first Geometry call.
    for(int i=0;i<8192;++i){old.Update();fresh.Update();auto a=old.Process(i==0?4095:0);auto b=fresh.Process(i==0?4095:0);assert(a.left==b.left&&a.right==b.right);++comparisons;}
    for(int cadence:{1,16,32,64,4096})for(unsigned i=0;i<196608;++i){
        rng=rng*1664525u+1013904223u;
        if(i%cadence==0){uint32_t phase=(i<4096?0:i<8192?0xffffffffu:rng);int height=i<16384?int((i/cadence)%3)*2048:int((rng>>4)%4097);int strength=(rng>>20)&4095;
            old.Geometry(phase,strength,height);fresh.Geometry(phase,strength,height);
            if(i%128==0){old.Geometry(phase,strength,height);fresh.Geometry(phase,strength,height);}
        }
        old.Update();fresh.Update();int input=i<1000?(i==0?4095:0):i&1?-4096:int(rng&8191)-4096;
        auto a=old.Process(input);auto b=fresh.Process(input);assert(a.left==b.left&&a.right==b.right);++comparisons;
    }
    // Fully settled filters keep identical quiet-update/smoothing behaviour.
    old.Geometry(123456789u,4095,731);fresh.Geometry(123456789u,4095,731);
    for(int i=0;i<32768;++i){old.Update();fresh.Update();auto a=old.Process(i&1?-4096:4095);auto b=fresh.Process(i&1?-4096:4095);assert(a.left==b.left&&a.right==b.right);++comparisons;}
    std::printf("PASS: %u exact HRTF frames, constructor/anchors/intermediate heights/wraps/geometry cadences/settling\n",comparisons);
}
