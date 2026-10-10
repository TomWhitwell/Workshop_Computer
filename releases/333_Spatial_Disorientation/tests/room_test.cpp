// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Check audible room structure independently of subjective headphone placement.
#include "dsp/spatial.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace spatial;
struct Response { double direct=0,early=0,late=0; };
static Response Measure(int distance,int room,uint32_t phase) {
    Source source;source.Geometry(phase,distance,2048,4095,room);
    for(int i=0;i<12000;++i)source.Process(0);
    Response r;Stereo previous;
    // The direct impulse is before 2 ms; reflections start after 7 ms.
    int arrival=64+distance*20/256;
    for(int i=0;i<4096;++i){
        Stereo out=source.Process(i==0?1600:0);
        // Transient energy rejects the fixed-point DC blocker's one-step floor.
        double left=out.left-previous.left,right=out.right-previous.right;
        double energy=left*left+right*right;previous=out;
        int t=i-arrival;
        if(t>=0&&t<128)r.direct+=energy;
        if(t>=300&&t<550)r.early+=energy;
        if(t>=550&&t<1400)r.late+=energy;
    }
    return r;
}
int main(){
    auto dry=Measure(0,0,0),near=Measure(0,1200,0),far=Measure(4095,1200,0);
    assert(dry.direct>0);
    assert(near.early+near.late>(dry.early+dry.late)*20);
    assert(near.direct<dry.direct&&near.direct>dry.direct*.85);
    double nearRatio=(near.early+near.late)/near.direct;
    double farRatio=(far.early+far.late)/far.direct;
    assert(farRatio>nearRatio*4); // Distance must change room balance, not just volume.
    auto front=Measure(0,4095,0),back=Measure(0,4095,0x80000000u);
    assert(front.early/front.late>(back.early/back.late)*3);
    // All reflection paths are feed-forward: silence must drain even after room
    // changes while orbit/distance controls continue moving.
    Source source;source.Geometry(0,4095,4095,4095,4095);
    for(int i=0;i<12000;++i)source.Process((i&1)?2047:-2048);
    source.Geometry(0x80000000u,0,4095,4095,0);
    for(int i=0;i<24000;++i)source.Process(0);
    auto tail=source.Process(0);assert(std::abs(tail.left)<=4&&std::abs(tail.right)<=4);
    std::printf("Room/direct energy: near %.4f, far %.4f; early/late: front %.2f, rear %.2f\n",nearRatio,farRatio,front.early/front.late,back.early/back.late);
    std::puts("PASS: reflection arrivals, direct retention, distance balance, directional early/late balance and tail drain");
}
