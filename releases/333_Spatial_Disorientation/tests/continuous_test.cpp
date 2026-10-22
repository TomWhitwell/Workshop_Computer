// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "dsp/spatial.h"
#include "mixer_mu_controls.h"
#include "elevation_mailbox.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace spatial;
int main(){
    // Settled impulse responses remain between the measured level/end responses.
    // This tests acoustic interpolation via rendered samples, not private state.
    for(uint32_t phase:{0u,123456789u,0x80000000u,0xffffffffu})
    for(int strength:{0,2048,4095})for(int end:{0,4096}){
        Hrtf level,endpoint,mid;level.Geometry(phase,strength,2048);endpoint.Geometry(phase,strength,end);mid.Geometry(phase,strength,(2048+end)/2);
        for(int i=0;i<8192;++i){level.Update();endpoint.Update();mid.Update();}
        for(int i=0;i<64;++i){int impulse=i==0?4096:0;auto a=level.Process(impulse),b=endpoint.Process(impulse),c=mid.Process(impulse);
            assert(c.left>=(a.left<b.left?a.left:b.left)-1&&c.left<=(a.left>b.left?a.left:b.left)+1);
            assert(c.right>=(a.right<b.right?a.right:b.right)-1&&c.right<=(a.right>b.right?a.right:b.right)+1);
            assert(c.left-(a.left+b.left)/2>=-1&&c.left-(a.left+b.left)/2<=1);
            assert(c.right-(a.right+b.right)/2>=-1&&c.right-(a.right+b.right)/2<=1);
        }
    }
    // Moving intermediate heights under full-scale/random two-source input.
    Engine engine;Scene scene;Config cfg;uint32_t rng=1;
    for(unsigned frame=0;frame<96000;++frame){
        rng=rng*1664525u+1013904223u;scene.angle[0]=rng;scene.angle[1]=0u-rng;
        scene.distance=(rng>>8)&4095;scene.distance_b=(rng>>20)&4095;cfg.value[1]=(rng>>4)&4095;
        engine.SetElevation(PackElevations(frame%4097,(frame*7)%4097));
        auto sample=engine.Process(frame&1?2047:-2048,int(rng&4095)-2048,scene,cfg);
        assert(sample.left>=-2048&&sample.left<=2047&&sample.right>=-2048&&sample.right<=2047);
    }
    ElevationPickup p;p.Start(2048);assert(p.Update(4064,2048)==-1&&!p.Picked());assert(p.Update(2033,2048)==-1&&p.Picked());
    uint32_t applied=2048;int changes=0;
    for(int raw=2034;raw<=4064;++raw){int next=p.Update(raw,applied);if(next>=0){assert(uint32_t(next)>applied);applied=next;++changes;}}
    assert(applied==4096&&changes>2000);for(int i=0;i<10000;++i)assert(p.Update(4064,4096)==-1);
    assert(p.Update(0,1024)==-1&&!p.Picked());assert(p.Update(1016,1024)==1023&&p.Picked());
    ElevationMailbox mailbox;mailbox.Publish(0,731);mailbox.Publish(1,3555);auto heights=mailbox.Consume(kLevelElevations);
    assert(HeightAt(heights,0)==731&&HeightAt(heights,1)==3555);mailbox.Publish(0,0);mailbox.Publish(1,4096);heights=mailbox.Consume(heights);
    assert(HeightAt(heights,0)==0&&HeightAt(heights,1)==4096);mailbox.Publish(2,0);mailbox.Publish(0,4097);assert(mailbox.Consume(heights)==heights);
    Modes modes;MixerMuControls mu;MuInput input;Config config;int32_t main=2048,x=1000,y=2000;
    modes.Controls(Mode::Mixer,main,x,y,0,0,false,config,0);input.connected=input.faders_ready=1;input.session=1;input.fader[7]=4064;
    auto tick=[&](){auto cfg=config;mu.Apply(input,modes,main,x,y,cfg,heights);if(mu.ElevationChange()>=0)mailbox.Publish(modes.SelectedSource(),mu.ElevationChange());heights=mailbox.Consume(heights);};
    tick();assert(mu.ElevationChange()==-1);input.fader[7]=0;tick();assert(HeightAt(heights,0)==0);input.fader[7]=1016;tick();assert(HeightAt(heights,0)==1023);
    input.buttons=2;tick();input.buttons=0;tick();assert(modes.SelectedSource()==1&&!(mu.Feedback()&128));assert(HeightAt(heights,1)==4096);
    input.fader[7]=4064;tick();assert(mu.Feedback()&128);input.fader[7]=3048;tick();assert(HeightAt(heights,1)==3071&&HeightAt(heights,0)==1023);
    input.connected=0;tick();assert(!(mu.Feedback()&128));input.fader[7]=0;input.connected=1;++input.session;tick();assert(!(mu.Feedback()&128));
    uint8_t wire[]={0,0,0,32};uint32_t packed=0;assert(DecodeElevation(wire,4,packed)&&HeightAt(packed,0)==0&&HeightAt(packed,1)==4096);
    wire[0]=1;wire[1]=32;assert(!DecodeElevation(wire,4,packed)&&HeightAt(packed,0)==0);
    std::puts("PASS: acoustic intermediate-height impulse interpolation, >2000 continuous pickup values, mailbox bounds, selected A/B isolation and session rearm");
}
