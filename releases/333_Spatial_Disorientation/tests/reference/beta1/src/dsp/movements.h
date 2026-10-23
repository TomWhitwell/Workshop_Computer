// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Original mathematical paths; no additional external measurement data.
#pragma once
#include "fig8.h"
#include "slope_table.h"
namespace spatial {
// Original bounded vertical path. Excursion is a half-range (2048 = 40 degrees).
// Sin/cos lookups reuse existing phase: freezes and reversals need no new clock.
inline int32_t PathHeight(uint32_t phase,uint32_t movement,int32_t centre,int32_t depth,int32_t wander){
    if(!depth)return centre;
    int32_t unit=movement==2?wander:movement==1?-(Sin(phase*2+0x40000000u)>>4):(Sin(phase)>>4);
    return Clamp(centre+((unit*depth)>>11),0,4096);
}
// Tilt the horizontal path about an axis perpendicular to the selected uphill
// bearing. Unit direction is rotated; distance stays the existing path distance.
// Existing vertical bob is then added and clipped by Engine, rather than rotated.
// Zero tilt bypasses every operation so alpha25 audio remains exact.
struct SlopedPoint {uint32_t angle;int32_t height;};
inline SlopedPoint SlopePath(uint32_t angle,int32_t bearing,int32_t tilt){
    if(!tilt)return {angle,0};
    uint32_t uphill=static_cast<uint32_t>(bearing)*1048576u;
    uint32_t pitch=static_cast<uint32_t>(tilt)*233017u; // 2048 = 40 degrees.
    int32_t sx=Sin(uphill)>>3,sz=Sin(uphill+0x40000000u)>>3;
    int32_t x=Sin(angle)>>3,z=Sin(angle+0x40000000u)>>3;
    int32_t u=(x*sx+z*sz)>>12;
    int32_t y=(u*(Sin(pitch)>>3))>>12;
    int32_t correction=(u*((Sin(pitch+0x40000000u)>>3)-4096))>>12;
    auto horizontal=CartesianPosition(x+((correction*sx)>>12),z+((correction*sz)>>12));
    int32_t magnitude=Clamp(y<0?-y:y,0,4096),i=magnitude>>4,f=magnitude&15;
    int32_t height=kSlopeAsin[i];
    if(i<256)height+=((kSlopeAsin[i+1]-height)*f)>>4;
    return {horizontal.angle,y<0?-height:height};
}
inline Fig8Point Pendulum(uint32_t phase,int32_t shape,int32_t excursion){
    // Main aims the arc from front to back through the right side. Y sets
    // the swing up to +/-90 degrees. Radius stays fixed, unlike Fig8/Wander.
    int32_t swing=(Sin(phase)*excursion)>>12;
    return {static_cast<uint32_t>(shape)*524288u+static_cast<uint32_t>(swing)*32768u,1024};
}
class Wander {
public:
    Fig8Point Point(uint32_t phase,int32_t shape,int32_t excursion,uint32_t epoch,bool vertical=false){
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
        if(vertical)height_=Interpolate(Node(node,2),Node(node+1,2),t)>>1;
        x=(x*excursion)>>12;z=(z*excursion)>>12;
        z=2048+((z*(1024+((shape*5)>>2)))>>12);
        return CartesianPosition(x,z);
    }
    int32_t Height()const{return height_;} // Same segment/easing as the horizontal path.
private:
    static int32_t Node(uint32_t node,uint32_t axis){
        if(node==0)return 0; // Repeatable front-centre reset.
        uint32_t v=node*0x9e3779b9u+axis*0x85ebca6bu;
        v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;v^=v>>16;
        return static_cast<int32_t>(v&8191)-4096;
    }
    static int32_t Interpolate(int32_t a,int32_t b,int32_t t){return a+(((b-a)*t)>>12);}
    int32_t height_=0;
    uint32_t previous_=0,wraps_=0,epoch_=0;bool initialized_=false;
};
}
