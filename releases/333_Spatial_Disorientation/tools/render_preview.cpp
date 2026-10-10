// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Original synthetic sources rendered through the same DSP as firmware.
#include "orbits.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace spatial;
static void Word(FILE* f,uint32_t v,unsigned bytes){while(bytes--){fputc(v&255,f);v>>=8;}}
int main(int argc,char** argv){
    if(argc<3||argc>5){std::fprintf(stderr,"usage: render_preview output.wav linked(0/1) or front-back [room 0..4095] [distance 0..4095]\n");return 1;}
    int room=argc>3?std::atoi(argv[3]):-1,distance=argc>4?std::atoi(argv[4]):-1;
    if((argc>3&&(room<0||room>4095))||(argc>4&&(distance<0||distance>4095)))return 1;
    FILE* f=std::fopen(argv[1],"wb");if(!f)return 2;
    constexpr int samples=48000*12;std::fwrite("RIFF",1,4,f);Word(f,36+samples*4,4);std::fwrite("WAVEfmt ",1,8,f);Word(f,16,4);Word(f,1,2);Word(f,2,2);Word(f,48000,4);Word(f,192000,4);Word(f,4,2);Word(f,16,2);std::fwrite("data",1,4,f);Word(f,samples*4,4);
    Engine engine;Orbits orbit;Config cfg;bool comparison=std::strcmp(argv[2],"front-back")==0;
    bool linked=std::atoi(argv[2])!=0;uint32_t noise=1;
    if(comparison)cfg.value[1]=0;
    if(room>=0)cfg.value[1]=room;
    for(int i=0;i<samples;++i){
        if(i%48==0)orbit.Controls(comparison?((i/144000)%2?0:2048):2048,comparison?2048:2400,distance>=0?distance:(comparison?0:700),0,0,linked,cfg);
        orbit.Pulse(false,false,false);engine.SetScene(orbit.Advance(),cfg);
        double t=i/48000.0;
        int a=static_cast<int>(1000*(std::sin(2*M_PI*220*t)+.3*std::sin(2*M_PI*660*t)));
        if(comparison && i%144000==0)noise=1;
        noise=noise*1664525u+1013904223u;
        double envelope=std::exp(-((i%24000)/48000.0)*25);
        int b=static_cast<int>(envelope*(static_cast<int>(noise&4095)-2048)*.7);
        if(comparison){a=static_cast<int>(noise&4095)-2048;b=0;}
        auto o=engine.Process(a,b);Word(f,static_cast<uint16_t>(o.left*16),2);Word(f,static_cast<uint16_t>(o.right*16),2);
    }
    std::fclose(f);
}
