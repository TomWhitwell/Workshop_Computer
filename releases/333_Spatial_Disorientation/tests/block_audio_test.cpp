// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "block_audio.h"
#include <cassert>
#include <cstdio>
using namespace spatial;
int main(){
    BlockAudio transport;Engine worker,reference;Scene scene;Config cfg;cfg.value[1]=0;
    Stereo expected[12000]={};
    uint32_t random=7;
    // Frame-exact match to the unbuffered engine with 128 frames latency,
    // including changing controls/config and many slot reuses.
    for(int i=0;i<12000;++i){
        random=random*1664525u+1013904223u;
        int a=static_cast<int>(random&4095)-2048,b=static_cast<int>((random>>12)&4095)-2048;
        scene.angle[0]+=1234567;scene.angle[1]-=1234567;
        scene.distance=(i/500)*123;scene.distance_b=4095-scene.distance;
        scene.fig8=i>=4000&&i<9000;scene.movement=(i/2000)%3;scene.path_epoch=i/3000;scene.shape=(i*7)&4095;scene.excursion=(i*13)&4095;
        scene.level_a=(i*17)&4095;scene.level_b=(i*31)&4095;
        if(i==3000)cfg.value[0]=1024;
        reference.SetScene(scene,cfg);expected[i]=reference.Process(a,b);
        auto got=transport.Tick(a,b,scene,cfg);
        if(i<128)assert(got.left==0&&got.right==0);
        else assert(got.left==expected[i-128].left&&got.right==expected[i-128].right);
        if(i%64==63)assert(transport.Render(worker));
    }
    assert(!transport.fault);
    assert(!transport.Render(worker));
    // No worker: bounded silence, never stale output and no producer waiting.
    BlockAudio stalled;Engine recovery;
    for(int i=0;i<800;++i){auto got=stalled.Tick(1000,-1000,scene,cfg);assert(got.left==0&&got.right==0);}
    assert(stalled.fault);
    for(int i=0;i<2000;++i){stalled.Tick(0,0,scene,cfg);stalled.Render(recovery);}
    // Recovery must not leave the ring wedged after skipped input slots.
    bool rendered=false;
    for(int i=0;i<128;++i){stalled.Tick(0,0,scene,cfg);rendered|=stalled.Render(recovery);}
    assert(rendered);
    std::puts("PASS: block scheduling, exact 128-frame latency, controls/config alignment, slot reuse, worker starvation silence and recovery");
}
