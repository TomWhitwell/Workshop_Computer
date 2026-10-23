// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Compare the preserved alpha25 implementation against the actual new transport.
#define spatial baseline
#define __dmb baseline_dmb
#include "reference/fir-unroll16-64/src/block_audio.h"
#undef __dmb
#undef spatial
#include "block_audio.h"
#include <cassert>
#include <cstdio>

int main(){
    baseline::Engine old;spatial::Engine fresh;
    baseline::BlockAudio oldQueue;spatial::BlockAudio newQueue;
    baseline::Scene a;spatial::Scene b;baseline::Config ca;spatial::Config cb;
    uint32_t random=7;
    for(int i=0;i<180000;++i){
        random=random*1664525u+1013904223u;
        a.angle[0]=b.angle[0]=random;a.angle[1]=b.angle[1]=0u-random;
        a.distance=b.distance=random&4095;a.distance_b=b.distance_b=(random>>12)&4095;
        a.level_a=b.level_a=(i*17)&4095;a.level_b=b.level_b=(i*31)&4095;
        a.fig8=b.fig8=(i/4000)%4!=0;a.movement=b.movement=(i/4000)%3;
        a.shape=b.shape=(i*7)&4095;a.excursion=b.excursion=(i*13)&4095;
        a.path_epoch=b.path_epoch=i/9000;
        a.vertical_centre=b.vertical_centre=(i*5)%4097;a.vertical_depth=b.vertical_depth=(i*11)%2049;
        for(int n=0;n<5;++n)ca.value[n]=cb.value[n]=(random>>(n*3))&4095;
        int left=int(random&4095)-2048,right=int((random>>12)&4095)-2048;
        auto x=oldQueue.Tick(left,right,a,ca);auto y=newQueue.Tick(left,right,b,cb);
        assert(x.left==y.left&&x.right==y.right);
        if(i%64==63){
            old.SetElevation(baseline::kLevelElevations);fresh.SetElevation(spatial::kLevelElevations);
            bool solo=(i/10000)%5==0;old.SetSolo(solo);fresh.SetSolo(solo);
            // Deliberate worker stalls test identical late-block silence/recovery.
            if(i%8192>=7680)continue;
            assert(oldQueue.Render(old)==newQueue.Render(fresh));
            const auto& ro=old.RenderedScene();const auto& rn=fresh.RenderedScene();
            assert(ro.angle[0]==rn.angle[0]&&ro.angle[1]==rn.angle[1]);
            assert(ro.distance==rn.distance&&ro.distance_b==rn.distance_b);
        }
    }
    assert(oldQueue.fault==newQueue.fault&&oldQueue.worker_fault==newQueue.worker_fault);
    // The reference overload accepts short-lived values without retaining them.
    auto temporary=fresh.Process(0,0,spatial::Scene{},spatial::Config{});
    assert(temporary.left>=-2048&&temporary.left<=2047);
    std::puts("PASS: 180000 frame-exact alpha25/alpha26 zero-tilt transport comparisons, per-frame config/control changes, all paths, heights, solo/dual, slot reuse and worker starvation/recovery");
}
