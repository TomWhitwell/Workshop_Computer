// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "orbits.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace spatial;
static double Energy(uint32_t angle,int distance,bool right=false,bool inputB=false,double frequency=993.126845,bool strength=true) {
    Engine e;Config c;c.value[1]=0;c.value[4]=strength?4095:0;
    Scene s{{angle,angle},distance};e.SetScene(s,c);
    double sum=0;
    for(int i=0;i<48000;++i) {
        int sample=static_cast<int>(1500*std::sin(i*frequency*6.283185307179586/48000.0));
        auto o=e.Process(inputB?0:sample,inputB?sample:0);
        if(i>12000) { double v=right?o.right:o.left;sum+=v*v; }
    }
    return sum;
}
int main() {
    Config c,decoded;uint8_t bytes[18];EncodeConfig(c,bytes);
    assert(DecodeConfig(bytes,18,decoded));assert(std::memcmp(&c,&decoded,sizeof(c))==0);
    bytes[0]=2;assert(!DecodeConfig(bytes,18,decoded));
    EncodeConfig(c,bytes);bytes[2]=127;assert(!DecodeConfig(bytes,18,decoded));
    EncodeConfig(c,bytes);bytes[16]=3;assert(!DecodeConfig(bytes,18,decoded));
    assert(!DecodeConfig(bytes,17,decoded));
    assert(Checksum(reinterpret_cast<const uint8_t*>("hello"),5)==0x4f9f2cab);
    double left=Energy(0x40000000u,0),right=Energy(0x40000000u,0,true);
    assert(right>left*3); // Right source is stronger in right ear.
    assert(std::abs(Energy(0xc0000000u,0)-right)<right*.03);
    assert(Energy(0,4095)<Energy(0,0)*.2);
    assert(std::abs(Energy(0,0,false,true)-Energy(0,0))<Energy(0,0)*.01);
    // Validate the convolver against the generated measured impulse responses.
    // Check every direction and both ears, including filter settling and tails.
    for(int direction=0;direction<72;++direction) {
        Hrtf h;
        uint32_t phase=static_cast<uint32_t>((static_cast<uint64_t>(direction)<<32)/72+1);
        h.Geometry(phase,4095);
        for(int i=0;i<4096;++i){h.Update();h.Process(0);}
        for(int i=0;i<kHrtfTaps+8;++i) {
            h.Update();
            auto pair=h.Process(i==0?1024:0);
            for(int ear=0;ear<2;++ear) {
                int got=ear==0?pair.left:pair.right;
                int measured=i<kHrtfTaps?kHrtf[direction][ear][i]:0;
                int neutral=i==0?16384:0;
                int coefficient=neutral+(((measured-neutral)*4095)>>12);
                int expected=(1024*coefficient)>>14;
                assert(std::abs(got-expected)<=1);
            }
        }
        // Compare paired/unrolled MACs with an independent scalar int64 FIR
        // on full-range deterministic input, including repeated ring wrap.
        int32_t history[kHrtfTaps]={};uint32_t seed=direction+1;
        for(int i=0;i<256;++i){
            seed=seed*1664525u+1013904223u;
            for(int n=kHrtfTaps-1;n>0;--n)history[n]=history[n-1];
            history[0]=static_cast<int32_t>(seed&8191)-4096;
            h.Update();auto pair=h.Process(history[0]);
            for(int ear=0;ear<2;++ear){
                int64_t accumulator=0;
                for(int n=0;n<kHrtfTaps;++n){
                    int neutral=n==0?16384:0;
                    int coefficient=neutral+(((kHrtf[direction][ear][n]-neutral)*4095)>>12);
                    accumulator+=static_cast<int64_t>(history[n])*coefficient;
                }
                assert(std::abs((ear==0?pair.left:pair.right)-static_cast<int>(accumulator>>14))<=1);
            }
        }
    }
    // Front and rear now have distinct measured spectra, not a fixed rear notch.
    double lowRatio=Energy(0x80000000u,0,false,false,250)/Energy(0,0,false,false,250);
    double highRatio=Energy(0x80000000u,0,false,false,8000)/Energy(0,0,false,false,8000);
    assert(std::abs(10*std::log10(highRatio/lowRatio))>3);
    // Strength zero bypasses directional colour (room disabled here).
    double flatFront=Energy(0,0,false,false,8000,false);
    double flatBack=Energy(0x80000000u,0,false,false,8000,false);
    assert(std::abs(flatFront-flatBack)<flatFront*.01);
    std::printf("Rear/front energy ratio: 250 Hz %.3f; 8 kHz %.3f\n",lowRatio,highRatio);
    Engine e;Scene s;uint32_t random=1;
    for(int i=0;i<300000;++i) {
        random=random*1664525u+1013904223u;
        if((i&255)==0) {
            s={{random,0u-random},static_cast<int32_t>(random&4095)};
            for(int n=0;n<5;++n)c.value[n]=(random>>(n*3))&4095;
            e.SetScene(s,c);
        }
        auto o=e.Process(static_cast<int>(random&4095)-2048,static_cast<int>((random>>12)&4095)-2048);
        assert(o.left>=-2048&&o.left<=2047&&o.right>=-2048&&o.right<=2047);
    }
    for(int i=0;i<96000;++i)e.Process(0,0);
    auto silence=e.Process(0,0);assert(std::abs(silence.left)<=4&&std::abs(silence.right)<=4);
    Orbits orbit;Config defaults;
    orbit.Controls(2048,2048,0,0,0,false,defaults);assert(orbit.Step()==0);
    orbit.Controls(2048,4095,0,0,0,false,defaults);assert(orbit.Step()==134218);
    auto start=orbit.Advance();for(int i=0;i<100;++i)orbit.Advance();auto finish=orbit.Advance();
    assert(finish.angle[0]-start.angle[0]==start.angle[1]-finish.angle[1]);
    orbit.Pulse(true,true,true);for(int i=0;i<23999;++i)orbit.Pulse(true,false,false);orbit.Pulse(true,true,false);
    orbit.Controls(2048,4095,0,0,0,true,defaults);
    assert(orbit.ClockLocked());assert(orbit.Step()==44739); // 120 BPM / 4 pulses = .5 Hz.
    orbit.Controls(2048,2048,0,0,0,true,defaults);assert(orbit.Step()==0);
    orbit.Pulse(true,false,true);auto reset=orbit.Advance();assert(reset.angle[0]==0&&reset.angle[1]==0x80000000u);
    for(int i=0;i<144001;++i)orbit.Pulse(true,false,false);assert(!orbit.ClockLocked());
    orbit.Controls(2048,4095,0,0,0,true,defaults);assert(orbit.Step()==134218);
    std::puts("PASS: config rejection, checksum, ear symmetry, distance, independent sources, DSP bounds/tails, measured FIR reference/front-back/bypass, orbit direction, clock, stop/reset and timeout");
}
