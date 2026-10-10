// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "modes.h"
#include <cassert>
#include <cstdio>
using namespace spatial;
int main(){
    Config cfg;Modes m;Orbits reference;
    for(int i=0;i<120000;++i){
        if(i%48==0){m.Controls(Mode::Orbits,1200,3300,700,123,-50,false,cfg);reference.Controls(1200,3300,700,123,-50,false,cfg);}
        m.Pulse(false,false,false);reference.Pulse(false,false,false);
        Scene a=m.Advance(),b=reference.Advance();assert(a.angle[0]==b.angle[0]&&a.angle[1]==b.angle[1]&&a.distance==b.distance&&a.distance_b==-1&&a.level_a==4096&&a.level_b==4096);
    }
    m.Controls(Mode::Mixer,2048,800,2000,0,0,false,cfg,0);
    auto a=m.Advance();assert(a.angle[0]==0&&a.angle[1]==0x80000000u&&a.distance==800&&a.distance_b==0&&a.level_a==2000&&a.level_b==4095);
    // Selecting B must not jump to A's physical knob positions.
    m.Controls(Mode::Mixer,2048,800,2000,0,0,false,cfg,1);
    auto b=m.Advance();assert(b.angle[1]==a.angle[1]&&b.distance_b==0&&b.level_b==4095&&m.Pickup()==0);
    // Middle retains B; approach each saved value to pick up, then edit B.
    m.Controls(Mode::Mixer,0,0,4095,0,0,false,cfg);assert(m.Pickup()==7);
    m.Controls(Mode::Mixer,1024,1600,1000,0,0,false,cfg);
    b=m.Advance();assert(b.angle[1]==0xc0000000u&&b.distance_b==1600&&b.level_b==1000&&b.angle[0]==a.angle[0]&&b.distance==a.distance&&b.level_a==a.level_a);
    for(int i=0;i<10000;++i){m.Pulse(true,i%1000==0,true);auto s=m.Advance();assert(s.angle[0]==a.angle[0]&&s.angle[1]==b.angle[1]);}
    assert(!m.ClockLocked());
    m.Controls(Mode::Mixer,1024,1600,1000,0,0,false,cfg,0);assert(m.Pickup()==0);
    m.Controls(Mode::Mixer,3072,0,3000,0,0,false,cfg); // Cross all three targets without landing on them.
    assert(m.Pickup()==7);auto moved=m.Advance();assert(moved.angle[0]==0x40000000u&&moved.distance==0&&moved.level_a==3000&&moved.angle[1]==b.angle[1]);
    m.Controls(Mode::Mixer,3072,0,3000,512,2047,false,cfg);
    auto cv=m.Advance();assert(cv.angle[0]==0x80000000u&&cv.angle[1]==0&&cv.distance==4094&&cv.distance_b==4095);
    m.Controls(Mode::Mixer,3072,0,3000,0,0,false,cfg);assert(m.Advance().distance_b==1600);
    // An already-picked-up remote position bypasses panel pickup.
    m.Controls(Mode::Mixer,2000,2000,2000,0,0,false,cfg,1,1);assert(m.Pickup()&1);assert(m.Advance().angle[1]==static_cast<uint32_t>(2000-2048)*1048576u);
    std::puts("PASS: exact Twin Orbits regression, source selection retention, independent mixer position/distance/level, near/crossing pickup, common CV restoration and remote takeover");
}
