// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "mu_controls.h"
#include "fig8_mu_controls.h"
#include "block_audio.h"
#include "orbits.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace spatial;
int main(){
    constexpr double pi=3.14159265358979323846;
    for(int x=-4064;x<=4064;x+=113)for(int z=-4064;z<=4064;z+=107){
        if(!TiltActive(x,z))continue;
        double expected=std::atan2(double(x),double(z));double got=TiltDirection(x,z)*(2*pi/4294967296.0);
        assert(std::abs(std::remainder(got-expected,2*pi))<0.001);
    }
    assert(TiltDirection(0,2032)==0&&TiltDirection(2032,0)==0x40000000u&&TiltDirection(0,-2032)==0x80000000u&&TiltDirection(-2032,0)==0xc0000000u);
    MuControls c;MuInput in;Config cfg;int32_t main=2048,x=2048,y=0;
    in.connected=1;in.faders_ready=1;in.fader[7]=4064;
    auto tick=[&](int n){for(int i=0;i<n;++i){main=2048;x=2048;y=0;cfg=Config{};c.Apply(in,main,x,y,cfg);}};
    tick(1);in.buttons=2;tick(1);in.buttons=0;tick(1);
    int pitch[4]={1000,0,-1000,0},roll[4]={0,-1000,0,1000},positions[4]={2048,3072,0,1024};
    // Test the complete control-to-scene chain at a front anchor: negative sensor
    // roll is right edge lowered, and must produce a right-hand sound direction.
    const uint32_t angles[4]={0,0x40000000u,0x80000000u,0xc0000000u};
    for(int k=0;k<4;++k){
        in.pitch=pitch[k];in.roll=roll[k];tick(500);assert(main==positions[k]);
        Orbits orbit;orbit.Controls(main,2048,0,0,0,true,cfg);
        assert(orbit.Advance().angle[0]==angles[k]);
        in.yaw=2032;tick(2000);assert(main==positions[k]);
    }
    // Sign must remain correct around a non-zero captured sensor neutral.
    in.pitch=0;in.roll=400;in.buttons=1;tick(1);in.buttons=0;
    in.roll=-600;tick(500);assert(main==3072);
    in.roll=1400;tick(500);assert(main==1024);
    in.roll=400;tick(500);assert(main==1024);
    // Restore flat neutral for the remaining near-neutral checks.
    in.roll=0;in.buttons=1;tick(1);in.buttons=0;
    in.roll=1000;tick(500);assert(main==1024);
    in.pitch=in.roll=0;tick(500);assert(main==1024); // Positive sensor roll = leftward lean (right edge raised); neutral holds it.
    in.pitch=32;in.roll=-32;tick(500);assert(main==1024);
    in.fader[7]=0;tick(500);assert(main==2048);
    // Captured rotation changes whole Disorientation path, with world-space slope.
    Engine staticEngine;Scene s;Config config;s.fig8=true;s.movement=1;s.shape=0;s.excursion=0;s.rotation=0x40000000u;s.slope_bearing=1024;s.slope_tilt=2048;
    for(int i=0;i<64;++i)staticEngine.Process(0,0,s,config);
    assert(staticEngine.RenderedScene().angle[0]==0x40000000u);assert(HeightAt(staticEngine.RenderedElevation(),0)>4080);
    Engine direct,worker;BlockAudio q;std::vector<Stereo> expected;
    for(unsigned i=0;i<180000;++i){
        s.movement=(i/10000)%3;s.angle[0]=i*1048576u;s.angle[1]=0u-s.angle[0];s.rotation=i*73856093u;s.path_epoch=i/9000;
        s.shape=(i*13)&4095;s.excursion=4095;s.vertical_depth=(i*7)%2049;s.vertical_centre=(i*11)%4097;
        int a=int((i*97)&4095)-2048,b=int((i*71)&4095)-2048;
        expected.push_back(direct.Process(a,b,s,config));auto out=q.Tick(a,b,s,config);
        if(i>=128)assert(out.left==expected[i-128].left&&out.right==expected[i-128].right);
        if(i%64==63)assert(q.Render(worker));
    }
    assert(!q.fault&&!q.worker_fault);
    std::puts("PASS: direct atan reference/cardinal axes/neutral hold/fader8/yaw rejection, physical roll polarity/recenter, world-space rotated slope and180000 captured rotation transport frames");
}
