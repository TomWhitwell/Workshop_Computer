// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#define spatial beta
#define __dmb beta_dmb
#include "reference/beta1/src/block_audio.h"
#undef __dmb
#undef spatial
#include "block_audio.h"
#include "settings.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using namespace spatial;
int main(){
    // Defaults with real Orbits scene flag must reproduce beta1 exactly.
    beta::Engine old;Engine fresh;beta::Scene before;Scene scene;beta::Config ca;Config cb;scene.orbit=true;uint32_t rng=7;
    for(unsigned i=0;i<180000;++i){
        rng=rng*1664525u+1013904223u;
        before.angle[0]=scene.angle[0]=rng;before.angle[1]=scene.angle[1]=0u-rng;
        before.distance=scene.distance=rng&4095;
        for(int n=0;n<5;++n)ca.value[n]=cb.value[n]=(rng>>(3*n))&4095;
        int a=int(rng&4095)-2048,b=int((rng>>12)&4095)-2048;
        auto x=old.Process(a,b,before,ca);auto y=fresh.Process(a,b,scene,cb);assert(x.left==y.left&&x.right==y.right);
    }
    // Axis expectations, linked/opposing phase, static height and clipping.
    for(int centre:{0,731,2048,3555,4096})for(int tilt:{0,1,1024,2048})for(int bearing:{0,512,1024,2048,3072}){
        Engine engine;Scene s;s.orbit=true;s.vertical_centre=centre;s.slope_tilt=tilt;s.slope_bearing=bearing;
        for(unsigned p=0;p<256;++p){
            s.angle[0]=p*16777216u;s.angle[1]=0u-s.angle[0]+0x80000000u;
            for(int i=0;i<32;++i)engine.Process(0,0,s,cb);
            for(int e=0;e<2;++e){auto expected=SlopePath(s.angle[e],bearing,tilt);assert(HeightAt(engine.RenderedElevation(),e)==uint32_t(Clamp(centre+expected.height,0,4096)));assert(engine.RenderedScene().angle[e]==expected.angle);}
        }
        auto held=engine.RenderedElevation();for(int i=0;i<64;++i)engine.Process(0,0,s,cb);assert(engine.RenderedElevation()==held);
    }
    // Captured height/slope must stay aligned with input samples, not live settings.
    Engine direct,worker;BlockAudio queue;std::vector<Stereo> expected;
    for(unsigned i=0;i<180000;++i){
        scene.angle[0]=i*1048576u;scene.angle[1]=0u-scene.angle[0];scene.vertical_centre=(i*13)%4097;scene.slope_bearing=(i*31)&4095;scene.slope_tilt=(i*7)%2049;
        int a=int((i*97)&4095)-2048,b=int((i*71)&4095)-2048;
        expected.push_back(direct.Process(a,b,scene,cb));auto out=queue.Tick(a,b,scene,cb);
        if(i>=128)assert(out.left==expected[i-128].left&&out.right==expected[i-128].right);
        if(i%64==63)assert(queue.Render(worker));
    }
    assert(!queue.fault&&!queue.worker_fault);
    Settings saved;saved.orbits_height=3555;saved.orbits_bearing=3072;saved.orbits_tilt=2048;saved.vertical_centre=731;saved.slope_tilt=1024;
    uint8_t data[43];uint32_t mode;Settings decoded;auto n=EncodeSettings(saved,0,data);assert(n==28&&DecodeSettings(data,n,decoded,mode)&&mode==0);assert(decoded.orbits_height==3555&&decoded.orbits_bearing==3072&&decoded.orbits_tilt==2048&&decoded.vertical_centre==2048);
    n=EncodeSettings(saved,2,data);assert(DecodeSettings(data,n,decoded,mode));assert(decoded.orbits_height==3555&&decoded.vertical_centre==731&&decoded.slope_tilt==1024);
    auto record=MakeRecord(saved);assert(record.version==8);auto loaded=LoadRecord(reinterpret_cast<const uint8_t*>(&record),sizeof(record));assert(!std::memcmp(&loaded,&saved,sizeof(saved)));
    V7Settings previous;previous.vertical_centre=731;previous.slope_tilt=1536;previous.height[0]=3555;previous.config[0].value[1]=3100;
    V7Record v7{kRecordMagic,7,Checksum(reinterpret_cast<const uint8_t*>(&previous),sizeof(previous)),previous};
    loaded=LoadRecord(reinterpret_cast<const uint8_t*>(&v7),sizeof(v7));assert(loaded.orbits_height==2048&&loaded.orbits_tilt==0&&loaded.orbits_bearing==0&&loaded.vertical_centre==731&&loaded.slope_tilt==1536&&loaded.height[0]==3555&&loaded.config[0].value[1]==3100);
    previous.slope_tilt=2049;v7={kRecordMagic,7,Checksum(reinterpret_cast<const uint8_t*>(&previous),sizeof(previous)),previous};assert(LoadRecord(reinterpret_cast<const uint8_t*>(&v7),sizeof(v7)).slope_tilt==0);
    EncodeSettings(saved,0,data);Settings intact=decoded;data[20]=1;data[21]=32;assert(!DecodeSettings(data,28,decoded,mode)&&!std::memcmp(&intact,&decoded,sizeof(decoded)));
    EncodeSettings(saved,0,data);data[22]=17;assert(!DecodeSettings(data,28,decoded,mode));
    std::puts("PASS: 180000 beta-exact Orbits frames, height/slope axes/bounds/freeze, 180000 changing captured-control transport frames, independent banks, flash8/wire7 roundtrip and beta migration/rejection");
}
