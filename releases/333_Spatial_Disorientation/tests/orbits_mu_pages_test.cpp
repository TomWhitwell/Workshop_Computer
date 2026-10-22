// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "orbits_mu_controls.h"
#include <cassert>
#include <cstdio>
using namespace spatial;
int main(){
    OrbitsMuControls c;MuInput in;Settings saved;Config cfg=saved.config[0];
    int32_t main=2048,x=2048,y=0;in.connected=1;in.session=1;in.faders_ready=1;
    auto tick=[&](int n=1){bool reset=false;for(int i=0;i<n;++i){cfg=saved.config[0];main=2048;x=2048;y=0;reset=c.Apply(in,main,x,y,cfg,saved);}return reset;};
    auto tap=[&](){in.buttons=8;tick(30);in.buttons=0;tick();};
    in.fader[0]=2032;in.fader[1]=0;in.fader[2]=2032;in.fader[7]=4064;
    tick();assert(!c.HeightPage()&&c.Height()==2048&&c.Tilt()==0);assert((c.Feedback()&7)==7);
    in.fader[0]=3000;in.fader[1]=2500;in.fader[2]=1000;tick();int speed=x,distance=y,separation=cfg.value[0];
    tap();assert(c.HeightPage());assert((c.Feedback()&7)==0);assert(x==speed&&y==distance&&cfg.value[0]==separation);
    // First page snapshot must not invent pickup from the other page's position.
    assert(c.Height()==2048&&c.Tilt()==0&&c.Bearing()==0);
    in.fader[0]=2032;in.fader[1]=0;in.fader[2]=0;tick();assert((c.Feedback()&7)==7);
    in.fader[0]=4064;in.fader[1]=4064;in.fader[2]=2032;tick();assert(c.Height()==4096&&c.Tilt()==2048&&c.Bearing()>=2047&&c.Bearing()<=2048);
    assert(x==speed&&y==distance&&cfg.value[0]==separation);
    // Shared faders and motion controls work on height page.
    in.fader[3]=1191;in.fader[4]=2032;in.fader[5]=2032;in.fader[6]=4064;tick();
    in.fader[3]=2000;tick();assert(cfg.value[1]>2000);
    in.buttons=2;tick();assert(c.Motion());in.buttons=0;tick();in.roll=1000;tick(50);assert(main!=2048);
    in.buttons=1;assert(tick());in.buttons=0;tick();
    in.buttons=4;tick(20);assert(x==2048&&(c.Feedback()&4096));in.buttons=0;tick();assert(x==speed);
    // D >=500ms is diagnostics only; release does not switch pages.
    in.buttons=8;tick(600);assert(c.HeightPage()&&(c.Feedback()&(1u<<28)));in.buttons=0;tick();assert(c.HeightPage());
    tap();assert(!c.HeightPage());assert(c.Height()==4096&&c.Tilt()==2048);assert((c.Feedback()&7)==0);assert(x==speed&&y==distance&&cfg.value[0]==separation);
    in.fader[0]=3000;in.fader[1]=2500;in.fader[2]=1000;tick();assert((c.Feedback()&7)==7);
    in.fader[0]=1000;tick();assert(x<1100);
    tap();assert(c.HeightPage());assert(c.Height()==4096&&c.Tilt()==2048); // Retains height values and rearms.
    assert((c.Feedback()&7)==0);
    // Disconnect restores saved elevation, releases motion, and uses panel pickup.
    int previousSpeed=x,previousDistance=y;in.connected=0;in.buttons=0;tick();assert(!c.HeightPage()&&!c.Motion());assert(c.Height()==2048&&c.Tilt()==0&&c.Bearing()==0);assert(x==previousSpeed&&y==previousDistance);
    // Reconnect with D already held must not create a page toggle on release.
    in.fader[0]=4064;in.connected=1;in.session=2;in.buttons=8;tick();in.buttons=0;tick();assert(!c.HeightPage());
    in.fader[0]=2032;tick();assert(x==previousSpeed); // Panel position is not the held remote pickup target.
    in.fader[0]=1000;tick();assert(x<1100);
    // Saved banks are input-only; runtime remote elevation never alters them.
    assert(saved.orbits_height==2048&&saved.orbits_tilt==0);
    // Rapid page/gesture/fader/session changes remain bounded, with page-independent C.
    for(int i=0;i<20000;++i){
        for(int j=0;j<8;++j)in.fader[j]=(i*(j+11))%4065;
        in.buttons=i%7==0?8:i%11==0?4:0;tick();assert(c.Height()<=4096&&c.Tilt()<=2048&&c.Bearing()<=4095);assert(x>=0&&x<=4095&&y>=0&&y<=4095);
    }
    std::puts("PASS: Orbits D-tap/hold/fresh-held safety, independent page pickup, retained speed/distance/separation and height, shared controls/motion/A/C, disconnect restoration and bounded stress");
}
