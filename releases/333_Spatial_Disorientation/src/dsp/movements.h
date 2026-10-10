// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Original mathematical paths; no additional external measurement data.
#pragma once
#include "fig8.h"
namespace spatial {
inline Fig8Point Pendulum(uint32_t phase,int32_t shape,int32_t excursion){
    // Main aims the arc from front to back through the right side. Y sets
    // the swing up to +/-90 degrees. Radius stays fixed, unlike Fig8/Wander.
    int32_t swing=(Sin(phase)*excursion)>>12;
    return {static_cast<uint32_t>(shape)*524288u+static_cast<uint32_t>(swing)*32768u,1024};
}
class Wander {
public:
    Fig8Point Point(uint32_t phase,int32_t shape,int32_t excursion,uint32_t epoch){
        if(!initialized_||epoch!=epoch_){initialized_=true;epoch_=epoch;wraps_=0;previous_=phase;}
        // Extend the phase into successive random nodes. Reversing retraces;
        // freezing holds. Reset explicitly returns to the same seeded start.
        int32_t delta=static_cast<int32_t>(phase-previous_);
        if(delta>0&&phase<previous_)wraps_+=4;
        if(delta<0&&phase>previous_)wraps_-=4;
        previous_=phase;
        uint32_t node=wraps_+(phase>>30);
        int32_t t=(4096-(Sin(((phase&0x3fffffffu)<<1)+0x40000000u)>>3))>>1;
        int32_t x=Interpolate(Node(node,0),Node(node+1,0),t);
        int32_t z=Interpolate(Node(node,1),Node(node+1,1),t);
        x=(x*excursion)>>12;z=(z*excursion)>>12;
        z=2048+((z*(1024+((shape*5)>>2)))>>12);
        return CartesianPosition(x,z);
    }
private:
    static int32_t Node(uint32_t node,uint32_t axis){
        if(node==0)return 0; // Repeatable front-centre reset.
        uint32_t v=node*0x9e3779b9u+axis*0x85ebca6bu;
        v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;
        return static_cast<int32_t>(v&8191)-4096;
    }
    static int32_t Interpolate(int32_t a,int32_t b,int32_t t){return a+(((b-a)*t)>>12);}
    uint32_t previous_=0,wraps_=0,epoch_=0;bool initialized_=false;
};
}
