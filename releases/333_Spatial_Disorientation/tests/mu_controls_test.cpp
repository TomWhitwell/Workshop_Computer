// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "mu_controls.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
using namespace spatial;
struct Rig {
    MuControls mu;MuInput in;Config base,cfg;
    int32_t main=2048,x=2048,y=1200;
    bool Tick(int32_t panelMain=2048,bool reset=false){
        main=panelMain;x=2048;y=1200;cfg=base;
        return mu.Apply(in,main,x,y,cfg,reset);
    }
};
int main(){
    Rig r;r.Tick();assert(r.main==2048&&r.x==2048&&r.cfg.value[1]==1200);
    r.in.connected=1;
    r.Tick();assert(r.mu.Picked()==0); // Unreceived zero is not a fader position.
    r.in.faders_ready=1;
    for(auto& f:r.in.fader)f=0;
    r.Tick();assert(r.mu.Picked()==0&&r.y==1200&&r.cfg.value[4]==4095);
    // Sweep X through its pickup point; Y and unrelated settings stay independent.
    r.in.fader[0]=70*32;r.Tick();assert(r.mu.Picked()&1);
    assert(r.x>2200&&r.y==1200&&r.cfg.value[1]==1200);
    r.in.fader[0]=127*32;r.Tick();assert(r.x==4095);
    r.in.fader[0]=0;r.Tick();assert(r.x==0);
    // Pick up every slider and prove the field order, including strength.
    for(int i=1;i<8;++i)r.in.fader[i]=127*32;
    r.Tick();assert(r.mu.Picked()==255);
    for(int i=0;i<8;++i)r.in.fader[i]=(i+1)*12*32;
    r.Tick();assert(r.x==387&&r.y==774);
    const int expected[5]={1161,1548,1935,2322,2709};
    for(int i=0;i<5;++i)assert(r.cfg.value[i]==expected[i]);
    assert(r.cfg.value[5]==4); // Clock divider stays in the saved configuration.
    r.in.buttons=4;r.Tick();assert(r.x==2048);
    r.x=500;r.y=1200;r.cfg=r.base;r.mu.Apply(r.in,r.main,r.x,r.y,r.cfg,false,false);assert(r.x!=2048); // Mixer C never changes distance.
    r.in.buttons=0;r.Tick();assert(r.x!=2048); // C is held, not a toggle.
    // Main is replaced by motion, not added to a knob that keeps changing.
    r.in.buttons=2;r.Tick(1800);assert(r.mu.Motion()&&r.main==1800);
    r.in.buttons=0;
    for(int i=0;i<100;++i)r.Tick(3000);
    assert(r.main==1800);
    r.in.roll=1200;
    for(int i=0;i<300;++i)r.Tick(3000);
    assert(r.main>2000&&r.main<3000);
    // A recaptures pose/Main and emits exactly one phase reset.
    r.in.buttons=1;assert(r.Tick(2200));assert(r.main==2200);
    assert(!r.Tick(2200));
    r.in.buttons=0;
    for(int i=0;i<50;++i)r.Tick(3200);
    assert(r.main==2200);
    r.Tick(2500,true);assert(r.main==2500); // Panel Down also recentres.
    // Rotation rate integrates; stopping holds heading instead of springing back.
    r.in.yaw=1000;
    for(int i=0;i<500;++i)r.Tick(2500);
    int moved=r.main;assert(moved!=2500);
    r.in.yaw=0;
    for(int i=0;i<200;++i)r.Tick(2500);
    int held=r.main;
    for(int i=0;i<200;++i)r.Tick(2500);
    assert(std::abs(r.main-held)<=1);
    // B returns Main to the panel; disconnect drops all slider ownership.
    r.in.buttons=2;r.Tick(3100);assert(!r.mu.Motion()&&r.main==3100);
    r.in.connected=0;r.Tick(1900);
    assert(r.main==1900&&r.x==2048&&r.y==1200&&r.cfg.value[1]==1200&&r.mu.Picked()==0);
    // Reconnect with controls away from stored values cannot cause a jump.
    r.in.connected=1;r.in.buttons=0;
    for(auto& f:r.in.fader)f=0;
    r.Tick();assert(r.mu.Picked()==0&&r.x==2048&&r.y==1200);
    // With motion on, repeatedly cross circular wrap with extreme tilt/rate.
    r.in.buttons=2;r.Tick();r.in.buttons=0;r.in.yaw=2032;r.in.roll=-2032;
    for(int i=0;i<15000;++i){r.Tick();assert(r.main>=0&&r.main<=4095&&r.cfg.Valid());}
    // Even if a fast detach/attach skips the disconnected snapshot, the new
    // USB session must discard pickup and motion ownership.
    ++r.in.session;for(auto& f:r.in.fader)f=0;
    r.Tick();assert(!r.mu.Motion()&&r.mu.Picked()==0&&r.main==2048);
    std::puts("PASS: 8mu independent pickup, field mapping, held stop, motion takeover/recenter/toggle, rate integration, disconnect/reconnect and wrap bounds");
}
