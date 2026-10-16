// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "modes.h"
#include <cassert>
#include <cstdio>
using namespace spatial;
int main(){
    auto centre=Pendulum(0,0,4095),right=Pendulum(0x40000000u,0,4095),left=Pendulum(0xc0000000u,0,4095);
    assert(centre.angle==0&&centre.distance==1024);
    assert(right.angle>0x3ff00000u&&right.angle<=0x40000000u);
    assert(left.angle>=0xc0000000u&&left.angle<0xc0100000u);
    for(int i=0;i<256;++i){auto p=Pendulum(uint32_t(i)<<24,2048,0);assert(p.angle==0x40000000u&&p.distance==1024);}
    // Full cycle retraces the same pendulum arc without distance modulation.
    for(uint32_t p=0;p<0x40000000u;p+=0x01000000u){
        auto a=Pendulum(p,2000,3500),b=Pendulum(0x80000000u-p,2000,3500);
        assert(a.angle==b.angle&&a.distance==b.distance);
    }
    Wander wander,reference;auto start=wander.Point(0,4095,4095,1);
    assert(start.angle==0&&start.distance==511);
    Fig8Point history[600];uint32_t phase=0;
    // Traverse beyond phase wrap, then retrace and compare exact points.
    for(int i=0;i<600;++i){phase+=0x01000000u;history[i]=wander.Point(phase,4095,4095,1);}
    assert(history[0].angle!=history[256].angle||history[0].distance!=history[256].distance);
    for(int i=598;i>=0;--i){phase-=0x01000000u;auto p=wander.Point(phase,4095,4095,1);assert(p.angle==history[i].angle&&p.distance==history[i].distance);}
    auto held=wander.Point(phase,4095,4095,1);for(int i=0;i<1000;++i){auto p=wander.Point(phase,4095,4095,1);assert(p.angle==held.angle&&p.distance==held.distance);}
    auto reset=wander.Point(0,4095,4095,2);assert(reset.angle==start.angle&&reset.distance==start.distance);
    for(int i=0;i<200;++i){uint32_t p=uint32_t(i)*1234567u;auto a=wander.Point(p,3000,4095,2),b=reference.Point(p,3000,4095,2);assert(a.angle==b.angle&&a.distance==b.distance);}
    Wander collapsed;for(int i=0;i<600;++i){auto p=collapsed.Point(uint32_t(i)*0x01000000u,4095,0,0);assert(p.angle==0&&p.distance==511);}
    // Different cycles choose different nodes; movement resets do not loop a
    // single set of random positions. All three paths use bounded audio output.
    Config cfg;Modes modes;Engine engine;uint32_t random=7;
    for(uint32_t movement=0;movement<3;++movement){
        modes.SetMovement(movement);modes.Controls(Mode::Disorientation,4095,3500,4095,0,0,true,cfg);
        for(int i=0;i<80000;++i){
            modes.Pulse(false,false,false);auto s=modes.Advance();assert(s.movement==movement);
            random=random*1664525u+1013904223u;engine.SetScene(s,cfg);
            auto out=engine.Process(int(random&4095)-2048,int((random>>12)&4095)-2048);
            assert(out.left>=-2048&&out.left<=2047&&out.right>=-2048&&out.right<=2047);
            auto& rendered=engine.RenderedScene();assert(rendered.distance>=0&&rendered.distance<=4095);
        }
        modes.Freeze(true);auto before=modes.Advance();modes.Pulse(false,false,false);auto after=modes.Advance();assert(before.angle[0]==after.angle[0]);
        modes.Pulse(false,false,true);after=modes.Advance();assert(after.angle[0]==0&&after.path_epoch!=before.path_epoch);
        modes.Freeze(false);
    }
    std::puts("PASS: pendulum arc/retrace/collapse, continuous deterministic wander/reverse/wrap/reset, frozen paths, epoch reset and all-path audio bounds");
}
