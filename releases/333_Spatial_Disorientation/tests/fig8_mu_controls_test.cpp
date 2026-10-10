// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "fig8_mu_controls.h"
#include "modes.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
using namespace spatial;
struct Rig {
    Fig8MuControls mu;MuInput in;Config base,cfg;
    int32_t main=2048,x=2048,y=1200;
    bool Tick(int32_t pm=2048,int32_t px=2048,int32_t py=1200){
        main=pm;x=px;y=py;cfg=base;return mu.Apply(in,main,x,y,cfg);
    }
};
int main(){
    Rig r;r.Tick();assert(!r.mu.Motion()&&!r.mu.Frozen()&&r.main==2048);
    r.in.connected=1;r.Tick();assert(r.mu.Picked()==0);
    r.in.faders_ready=1;r.Tick();assert(r.mu.Picked()==0); // No invented first crossing.
    for(auto& f:r.in.fader)f=4064;
    r.Tick();assert(r.mu.Picked()==255&&r.x==4095&&r.y==4095);
    for(int i=0;i<8;++i)r.in.fader[i]=(i+1)*12*32;
    r.Tick();assert(r.x==387&&r.y==774);
    int expect[5]={1161,1548,1935,2322,2709};
    for(int i=0;i<5;++i)assert(r.cfg.value[i]==expect[i]);
    assert(r.cfg.value[5]==r.base.value[5]);
    // C freezes phase without altering the selected speed; release resumes.
    Modes modes;auto tick=[&](){
        bool reset=r.Tick();modes.Controls(Mode::Disorientation,r.main,r.x,r.y,0,0,true,r.cfg);
        modes.Freeze(r.mu.Frozen());modes.Pulse(false,false,reset);return modes.Advance();
    };
    auto before=tick();r.in.buttons=4;auto held=tick();assert(before.angle[0]==held.angle[0]&&r.mu.Frozen()&&r.x==387);
    for(int i=0;i<1000;++i)assert(tick().angle[0]==held.angle[0]);
    assert(r.mu.Feedback()&4096);r.in.buttons=0;assert(tick().angle[0]!=held.angle[0]);
    // B anchors motion at Main. Tilt/rotation alter depth; panel cannot override.
    r.in.buttons=2;r.Tick(1800);assert(r.mu.Motion()&&r.main==1800);
    r.in.buttons=0;for(int i=0;i<100;++i)r.Tick(3100);assert(r.main==1800);
    r.in.roll=1200;for(int i=0;i<200;++i)r.Tick(3100);assert(r.main>2500&&r.main<3000);
    r.in.buttons=1;assert(r.Tick(2200));assert(r.main==2200);assert(!r.Tick(2200));
    r.in.buttons=0;r.in.yaw=1000;
    for(int i=0;i<500;++i)r.Tick(3100);assert(r.main>2200);
    r.in.yaw=0;for(int i=0;i<200;++i)r.Tick(3100);int stationary=r.main;
    for(int i=0;i<200;++i)r.Tick(3100);assert(std::abs(r.main-stationary)<=1);
    // Full-scale depth saturates rather than wrapping; opposite rotation returns.
    r.in.fader[7]=4064;r.in.roll=1200;r.in.yaw=2032;
    for(int i=0;i<12000;++i){r.Tick(3100);assert(r.main>=0&&r.main<=4095);}
    assert(r.main==4095);r.in.yaw=-2032;
    for(int i=0;i<12000;++i)r.Tick(3100);assert(r.main==0);
    // Amount zero suppresses both tilt and rate. It leaves gyro's held base.
    r.in.fader[7]=0;r.in.yaw=2032;r.in.roll=-2032;
    for(int i=0;i<300;++i)r.Tick(3100);assert(r.main==0);
    // Toggle off preserves depth until Main picks it up, rather than jumping.
    r.in.buttons=2;r.Tick(3100);assert(!r.mu.Motion()&&r.main==0&&(r.mu.PanelPending()&1));
    r.in.buttons=0;r.Tick(3000);assert(r.main==0);r.Tick(0);assert(r.main==0&&!(r.mu.PanelPending()&1));
    r.Tick(1000);assert(r.main==1000);
    // Disconnect releases C/motion, retains X/Y until their physical pickups.
    r.in.buttons=4;r.Tick(1000);assert(r.mu.Frozen());
    r.in.connected=0;r.Tick(1000);assert(!r.mu.Frozen()&&!r.mu.Motion()&&r.mu.Picked()==0);
    assert(r.x==387&&r.y==774&&(r.mu.PanelPending()&6)==6);
    assert(r.cfg.value[1]==r.base.value[1]);r.Tick(1000,0,0);assert(r.x==0&&r.y==0&&r.mu.PanelPending()==0);
    // New USB session drops old ownership, and held A/B cannot fire on attach.
    Rig fresh;fresh.in.connected=1;fresh.Tick();fresh.in.buttons=2;fresh.Tick();assert(fresh.mu.Motion());
    fresh.in.buttons=0;fresh.in.roll=800;for(int i=0;i<100;++i)fresh.Tick();int old=fresh.main;
    ++fresh.in.session;fresh.in.buttons=3;assert(!fresh.Tick());assert(!fresh.mu.Motion()&&fresh.main==old&&fresh.mu.Picked()==0);
    // C and panel freeze are composable; feedback does not depend on editor.
    assert(fresh.mu.Feedback(true)&4096);assert(fresh.mu.Feedback()&2048);
    // D taps cycle once on release, while a long hold only shows diagnostics.
    Rig select;select.in.connected=1;select.Tick();assert(select.mu.Movement()==0);
    for(int expected=1;expected<=3;++expected){
        select.in.buttons=8;for(int i=0;i<80;++i)select.Tick();assert(select.mu.Movement()==uint32_t(expected-1)%3);
        select.in.buttons=0;select.Tick();assert(select.mu.Movement()==uint32_t(expected)%3);
    }
    select.in.buttons=8;for(int i=0;i<600;++i)select.Tick();assert(select.mu.Feedback()&32768);
    select.in.buttons=0;select.Tick();assert(select.mu.Movement()==0);
    select.in.connected=0;select.Tick();assert(select.mu.Movement()==0);
    select.in.connected=1;select.in.buttons=8;++select.in.session;select.Tick();select.in.buttons=0;select.Tick();assert(select.mu.Movement()==0);
    std::puts("PASS: Fig8 fader mapping/pickup, held phase freeze/resume, reset edges, bounded motion/depth amount, panel handback and fresh-session safety");
}
