// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "block_audio.h"
#include "settings.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace spatial;
int main(){
    // Compare fixed-point projection with an independent double rotation.
    constexpr double pi=3.14159265358979323846;
    for(int bearing=0;bearing<4096;bearing+=137)for(int tilt:{0,1,512,1024,2048})for(int angle=0;angle<4096;angle+=17){
        uint32_t phase=uint32_t(angle)*1048576u;auto s=SlopePath(phase,bearing,tilt);
        if(!tilt){assert(s.angle==phase&&s.height==0);continue;}
        double a=angle*2*pi/4096,b=bearing*2*pi/4096,t=tilt*40*pi/(2048*180);
        double x=std::sin(a),z=std::cos(a),u=std::cos(a-b);
        double y=u*std::sin(t),correction=u*(std::cos(t)-1);
        double expected=std::asin(y)*180/pi*2048/40;
        assert(std::abs(s.height-expected)<8); // <0.16 degrees, lookup/rounding.
        double target=std::atan2(x+correction*std::sin(b),z+correction*std::cos(b));
        double actual=s.angle*(2*pi/4294967296.0);
        assert(std::abs(std::remainder(actual-target,2*pi))<0.004);
        assert(s.height>=-2048&&s.height<=2048);
    }
    assert(SlopePath(0,0,2048).height>2040); // Front uphill.
    assert(SlopePath(0x80000000u,0,2048).height<-2040);
    assert(std::abs(SlopePath(0x40000000u,0,2048).height)<3);
    // Frozen phases hold, reversing retraces, and opposite bearings invert height.
    for(int i=0;i<4096;++i){auto p=uint32_t(i)*1048576u;auto a=SlopePath(p,1024,1536),b=SlopePath(p,3072,1536);assert(std::abs(a.height+b.height)<6);}
    Engine direct,worker;BlockAudio queue;Scene scene;Config config;std::vector<Stereo> expected;
    scene.fig8=true;scene.shape=3000;scene.excursion=4095;
    for(unsigned i=0;i<180000;++i){
        scene.angle[0]=i*1048576u;scene.angle[1]=0u-scene.angle[0];scene.movement=(i/10000)%3;scene.path_epoch=i/20000;
        scene.vertical_centre=(i*13)%4097;scene.vertical_depth=(i*7)%2049;
        scene.slope_bearing=(i*31)&4095;scene.slope_tilt=(i*11)%2049;
        int a=int((i*97)&4095)-2048,b=int((i*71)&4095)-2048;
        auto sample=direct.Process(a,b,scene,config);expected.push_back(sample);
        assert(sample.left>=-2048&&sample.left<=2047&&sample.right>=-2048&&sample.right<=2047);
        auto heights=direct.RenderedElevation();assert(HeightAt(heights,0)<=4096&&HeightAt(heights,1)<=4096);
        auto delayed=queue.Tick(a,b,scene,config);
        if(i>=128)assert(delayed.left==expected[i-128].left&&delayed.right==expected[i-128].right);
        if(i%64==63)assert(queue.Render(worker));
    }
    assert(!queue.fault&&!queue.worker_fault);
    for(int i=0;i<64;++i)direct.Process(0,0,scene,config);auto held=direct.RenderedElevation();
    for(int i=0;i<4096;++i){direct.Process(0,0,scene,config);assert(direct.RenderedElevation()==held);}
    // Wire/storage roundtrip and v6 migration default only the new slope fields.
    Settings bank;bank.slope_bearing=4095;bank.slope_tilt=2048;bank.vertical_centre=731;bank.vertical_depth=1536;
    uint8_t data[43];uint32_t mode=0;Settings decoded;
    auto n=EncodeSettings(bank,2,data);assert(n==34&&DecodeSettings(data,n,decoded,mode));assert(decoded.slope_bearing==4095&&decoded.slope_tilt==2048);
    auto record=MakeRecord(bank);assert(record.version==8);auto loaded=LoadRecord(reinterpret_cast<const uint8_t*>(&record),sizeof(record));assert(loaded.slope_bearing==4095&&loaded.slope_tilt==2048);
    V6Settings old;old.vertical_centre=731;old.vertical_depth=1536;old.height[0]=3555;old.movement=2;old.config[2].value[1]=3100;
    V6Record v6{kRecordMagic,6,Checksum(reinterpret_cast<const uint8_t*>(&old),sizeof(old)),old};
    loaded=LoadRecord(reinterpret_cast<const uint8_t*>(&v6),sizeof(v6));assert(loaded.vertical_centre==731&&loaded.vertical_depth==1536&&loaded.height[0]==3555&&loaded.movement==2&&loaded.config[2].value[1]==3100&&loaded.slope_tilt==0&&loaded.slope_bearing==0);
    data[30]=32;assert(!DecodeSettings(data,n,decoded,mode));EncodeSettings(bank,2,data);data[32]=1;data[33]=16;assert(!DecodeSettings(data,n,decoded,mode));
    std::puts("PASS: independent 3D rotation, axis/opposite direction/zero bypass, bounded combined heights, freeze and 180000 sample-aligned two-source frames, slope wire/flash/migration and rejection");
}
