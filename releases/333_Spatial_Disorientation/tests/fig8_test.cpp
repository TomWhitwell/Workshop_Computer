// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "modes.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace spatial;
int main(){
    constexpr double pi=3.141592653589793;
    uint32_t seed=1;
    // Compare the fixed-point angle and distance conversion to independent
    // floating-point Cartesian geometry over the complete control range.
    for(int i=0;i<100000;++i){
        seed=seed*1664525u+1013904223u;uint32_t p=seed;
        int shape=(seed>>8)&4095,excursion=seed&4095;
        auto got=FigureEight(p,shape,excursion);
        int x=((Sin(p)>>3)*excursion)>>12,z=2048+(((((Sin(p*2)>>3)*excursion)>>12)*(1024+((shape*5)>>2)))>>12);
        double expected=std::atan2(double(x),double(z))/(2*pi)*4294967296.0;
        double error=double(static_cast<int32_t>(got.angle-static_cast<uint32_t>(static_cast<int64_t>(expected))));
        assert(std::fabs(error)<1200000); // <0.11 degrees; lookup/interpolation quantization.
        int radius=std::sqrt(double(x*x+z*z));
        int distance=Clamp(((radius-1024)*4095)>>13,0,4095);
        assert(std::abs(got.distance-distance)<=3);
    }
    auto crossing=FigureEight(0,4095,4095),half=FigureEight(0x80000000u,4095,4095);
    assert(crossing.angle==0&&half.angle==0&&crossing.distance==half.distance);
    auto rearRight=FigureEight(0x60000000u,4095,4095),rearLeft=FigureEight(0xe0000000u,4095,4095);
    assert(rearRight.angle>0x40000000u&&rearRight.angle<0x80000000u);
    assert(rearLeft.angle>0x80000000u&&rearLeft.angle<0xc0000000u);
    for(int i=0;i<256;++i){auto zero=FigureEight(uint32_t(i)<<24,4095,0);assert(zero.angle==crossing.angle&&zero.distance==crossing.distance);}
    Config cfg;Modes m;m.Controls(Mode::Disorientation,4095,3000,4095,0,0,true,cfg);
    m.Pulse(false,false,false);auto first=m.Advance();assert(first.fig8&&first.shape==4095&&first.excursion==4095&&first.angle[1]-first.angle[0]==0x80000000u);
    m.Freeze(true);for(int i=0;i<5000;++i){m.Pulse(false,false,false);auto s=m.Advance();assert(s.angle[0]==first.angle[0]&&s.angle[1]==first.angle[1]);}
    m.Freeze(false);m.Pulse(false,false,false);assert(m.Advance().angle[0]!=first.angle[0]);
    m.Controls(Mode::Disorientation,2048,2048,3000,-2048,2047,false,cfg);auto clamped=m.Advance();assert(clamped.shape==0&&clamped.excursion==4095);
    m.Freeze(true);m.Pulse(false,false,true);assert(m.Advance().angle[0]==0); // Pulse reset works while frozen.
    m.Controls(Mode::Disorientation,4095,4095,4095,0,0,true,cfg);m.Freeze(false);
    for(int i=0;i<5000;++i){m.Pulse(true,i%2000==0,false);m.Advance();}assert(m.ClockLocked());
    Engine engine;bool distinct=false;int lo=4095,hi=0;
    for(int i=0;i<160000;++i){
        seed=seed*1664525u+1013904223u;m.Pulse(false,false,false);
        auto s=m.Advance();engine.SetScene(s,cfg);auto out=engine.Process(int(seed&4095)-2048,int((seed>>12)&4095)-2048);
        assert(out.left>=-2048&&out.left<=2047&&out.right>=-2048&&out.right<=2047);
        const auto& rendered=engine.RenderedScene();assert(rendered.distance>=0&&rendered.distance<=4095);
        if(i>64){assert(rendered.distance_b>=0&&rendered.distance_b<=4095);distinct|=rendered.distance!=rendered.distance_b;}
        lo=lo<rendered.distance?lo:rendered.distance;hi=hi>rendered.distance?hi:rendered.distance;
    }
    assert(distinct&&hi-lo>1500);
    std::puts("PASS: fixed-point Cartesian reference, two rear-reaching lobes, crossing/zero excursion, held freeze/resume, CV clamps, clock/reset, independent distance and bounded rendered audio");
}
