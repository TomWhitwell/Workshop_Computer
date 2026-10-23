// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "block_audio.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace spatial;
int main(){
    assert(PathHeight(0,0,2048,0,0)==2048);
    assert(PathHeight(0x40000000u,0,2048,2048,0)>=4095);
    assert(PathHeight(0xc0000000u,0,2048,2048,0)<=1);
    assert(PathHeight(0,1,2048,2048,0)<=1);
    assert(PathHeight(0x40000000u,1,2048,2048,0)>=4095);
    assert(PathHeight(0x80000000u,1,2048,2048,0)<=1);
    // At equal angular swing, the rising/falling arc retraces the same height.
    for(uint32_t p=0;p<0x40000000u;p+=1048576u)
        assert(PathHeight(p,1,2048,1700,0)==PathHeight(0x80000000u-p,1,2048,1700,0));
    for(int centre:{0,731,2048,3555,4096})for(int depth:{0,1,512,2048})
    for(uint32_t n=0;n<4096;++n)for(int movement=0;movement<3;++movement){
        int height=PathHeight(n*1048576u,movement,centre,depth,int(n)-2048);
        assert(height>=0&&height<=4096);if(!depth)assert(height==centre);
    }
    // Same extended Wander segments supply horizontal and vertical targets.
    Wander w;std::vector<int> heights;uint32_t phase=0;
    for(int i=0;i<1800;++i){w.Point(phase,3000,4000,7,true);heights.push_back(w.Height());phase+=16777216u;}
    phase-=16777216u;bool positive=false,negative=false;
    for(int i=1799;i>=0;--i){w.Point(phase,3000,4000,7,true);assert(w.Height()==heights[i]);positive|=w.Height()>512;negative|=w.Height()<-512;phase-=16777216u;}
    assert(positive&&negative);w.Point(0,3000,4000,8,true);assert(w.Height()==0);
    w.Point(123456789u,3000,4000,8,true);int held=w.Height();for(int i=0;i<1000;++i){w.Point(123456789u,3000,4000,8,true);assert(w.Height()==held);}
    // Render both independently phased sources through every vertical movement.
    for(int movement=0;movement<3;++movement){
        Engine engine;Scene scene;Config cfg;scene.fig8=true;scene.movement=movement;scene.shape=3000;scene.excursion=4095;scene.vertical_depth=2048;
        uint32_t rng=3;bool low=false,high=false,different=false;
        for(uint32_t i=0;i<96000;++i){
            rng=rng*1664525u+1013904223u;scene.angle[0]=i*1048576u;scene.angle[1]=scene.angle[0]+0x40000000u;
            cfg.value[1]=i&4095;scene.vertical_centre=i<48000?2048:731;
            auto audio=engine.Process(int(rng&4095)-2048,i&1?2047:-2048,scene,cfg);
            assert(audio.left>=-2048&&audio.left<=2047&&audio.right>=-2048&&audio.right<=2047);
            auto h=engine.RenderedElevation();auto a=HeightAt(h,0),b=HeightAt(h,1);assert(a<=4096&&b<=4096);low|=a<1024;high|=a>3072;different|=a!=b;
        }
        assert(low&&high&&different);
        scene.angle[0]=123456789u;scene.angle[1]=scene.angle[0]+0x40000000u;
        for(int i=0;i<64;++i)engine.Process(0,0,scene,cfg);auto fixed=engine.RenderedElevation();
        for(int i=0;i<4096;++i){engine.Process(0,0,scene,cfg);assert(engine.RenderedElevation()==fixed);}
    }
    // Vertical configuration remains aligned with each captured audio frame.
    Engine direct,worker;BlockAudio queue;Scene captured;Config config;std::vector<Stereo> expected;
    captured.fig8=true;captured.shape=3000;captured.excursion=4095;
    for(unsigned i=0;i<8192;++i){
        captured.angle[0]=i*1048576u;captured.angle[1]=0u-captured.angle[0];captured.movement=(i/1000)%3;captured.path_epoch=i/2000;
        captured.vertical_centre=(i*13)%4097;captured.vertical_depth=(i*7)%2049;
        int a=int((i*97)&4095)-2048,b=int((i*71)&4095)-2048;
        expected.push_back(direct.Process(a,b,captured,config));auto audio=queue.Tick(a,b,captured,config);
        if(i>=128){assert(audio.left==expected[i-128].left&&audio.right==expected[i-128].right);}
        else assert(audio.left==0&&audio.right==0);
        if(i%64==63)assert(queue.Render(worker));
    }
    assert(queue.fault==0&&queue.worker_fault==0);
    std::puts("PASS: vertical phase/pendulum arc, bounds and zero excursion, Wander reverse/wrap/epoch/freeze, all-path two-source movement, held heights and sample-aligned block capture");
}
