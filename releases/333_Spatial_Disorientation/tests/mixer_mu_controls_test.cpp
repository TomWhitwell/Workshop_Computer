// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "mixer_mu_controls.h"
#include <cassert>
#include <cstdio>
using namespace spatial;
struct Rig {
    Modes modes;MixerMuControls mu;MuInput in;Config cfg,base;
    int32_t main=2048,x=1200,y=2000;
    void Tick(int source=-1,int32_t panelMain=2048,int32_t panelX=1200,int32_t panelY=2000){
        main=panelMain;x=panelX;y=panelY;cfg=base;
        modes.SelectMixer(source,main,x,y);
        uint32_t takeover=mu.Apply(in,modes,main,x,y,cfg);
        modes.Controls(Mode::Mixer,main,x,y,0,0,false,cfg,-1,takeover);
    }
};
int main(){
    Rig r;r.Tick();auto initial=r.modes.Advance();
    assert(initial.angle[0]==0&&initial.distance==1200&&initial.level_a==2000);
    r.in.connected=1;r.Tick();assert((r.mu.Feedback()&127)==0);
    r.in.faders_ready=1;for(auto& f:r.in.fader)f=0;r.Tick();
    assert((r.mu.Feedback()&7)==0);assert(r.modes.Advance().distance==1200);
    // Crossing picks up source-specific faders; the third is POSITION, not separation.
    r.in.fader[0]=2000;r.in.fader[1]=2500;r.in.fader[2]=3000;r.Tick();
    assert((r.mu.Feedback()&7)==7);auto a=r.modes.Advance();
    assert(a.distance==2015&&a.level_a==2519&&a.angle[0]==static_cast<uint32_t>(3023-2048)*1048576u);
    assert(r.cfg.value[0]==r.base.value[0]); // Keep saved separation unchanged.
    // Button B selects B despite a panel switch left Up. No control may jump.
    r.in.buttons=2;r.Tick();auto b=r.modes.Advance();
    assert(r.modes.SelectedSource()==1&&(r.mu.Feedback()&512));assert((r.mu.Feedback()&7)==0);
    assert(b.angle[1]==0x80000000u&&b.distance_b==0&&b.level_b==4095);
    for(int i=0;i<20;++i)r.Tick();assert(r.modes.SelectedSource()==1); // Held B never repeats.
    r.in.buttons=0;r.in.fader[0]=0;r.in.fader[1]=4064;r.in.fader[2]=0;r.Tick();
    assert((r.mu.Feedback()&7)==7);
    r.in.fader[0]=1500;r.in.fader[1]=1000;r.in.fader[2]=1000;r.Tick();b=r.modes.Advance();
    assert(b.distance_b==1511&&b.level_b==1007);
    assert(b.angle[0]==a.angle[0]&&b.distance==a.distance&&b.level_a==a.level_a);
    // Large motion values and C held are entirely ignored in Mixer.
    r.in.roll=-2032;r.in.yaw=2032;r.in.buttons=4;
    for(int i=0;i<500;++i){r.in.fader[7]=i&1?4064:0;r.Tick();}auto noMotion=r.modes.Advance();
    assert(noMotion.angle[1]==b.angle[1]&&noMotion.distance_b==b.distance_b&&!(r.mu.Feedback()&256));
    r.in.buttons=1;r.Tick();assert(r.modes.SelectedSource()==0&&!(r.mu.Feedback()&512));
    assert((r.mu.Feedback()&7)==0);assert(r.modes.Advance().angle[0]==a.angle[0]);
    // Source-specific pickup rearms, while the four shared trim/room/strength faders retain ownership.
    r.in.buttons=0;for(int i=3;i<7;++i)r.in.fader[i]=4064;r.Tick();
    assert((r.mu.Feedback()&120)==120);
    r.in.fader[3]=1000;r.in.fader[4]=1200;r.in.fader[5]=1400;r.in.fader[6]=1600;r.Tick();
    int expected[4]={1007,1209,1410,1612};for(int i=0;i<4;++i)assert(r.cfg.value[i+1]==expected[i]);
    r.in.buttons=2;r.Tick();assert((r.mu.Feedback()&120)==120);
    // Pick B up again, then disconnect while panel controls are elsewhere.
    r.in.buttons=0;r.in.fader[0]=1500;r.in.fader[1]=1000;r.in.fader[2]=1000;r.Tick();
    assert((r.mu.Feedback()&7)==7);auto beforeDetach=r.modes.Advance();
    r.in.connected=0;r.Tick();auto detached=r.modes.Advance();
    assert(detached.angle[1]==beforeDetach.angle[1]&&detached.distance_b==beforeDetach.distance_b&&detached.level_b==beforeDetach.level_b);
    assert((r.mu.Feedback()&127)==0&&r.cfg.value[1]==r.base.value[1]);
    // Returning to panel also needs pickup, rather than using a USB position as a crossing.
    r.Tick(-1,1000,1500,1000);assert(r.modes.Pickup()==7);
    // Panel gestures can select A after remote selection; a new USB session clears ownership.
    r.Tick(0);assert(r.modes.SelectedSource()==0);
    r.in.connected=1;++r.in.session;r.in.buttons=2;for(auto& f:r.in.fader)f=0;r.Tick();
    assert(r.modes.SelectedSource()==0);assert((r.mu.Feedback()&7)==0); // Held B at attach is ignored.
    std::puts("PASS: Mixer 8mu mapping, source pickup/retention, buttons/panel selection, shared trims, motion/C exclusion, disconnect handback and fresh-session safety");
}
