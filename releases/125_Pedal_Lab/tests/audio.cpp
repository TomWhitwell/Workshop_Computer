// SPDX-License-Identifier: MPL-2.0
#include "audio.hpp"
#include "bank.hpp"
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
static void check(bool c,const char* text){if(!c)throw std::runtime_error(text);}
int main() try {
    auto a=std::make_unique<spin::Audio>();
    auto unity=[](spin::Machine&s){s.regs[0x16]=s.regs[0x14];s.regs[0x17]=s.regs[0x15];};
    uint32_t random=4151;
    for(unsigned n=0;n<100000;++n) {
        int32_t p[4];for(auto &v:p){random=random*1664525u+1013904223u;v=int32_t(random&0xffffff)-8388608;}
        random=random*1664525u+1013904223u;uint32_t f=random&65535;
        double t=double(f)/65536;
        double y=p[1]+.5*t*((-double(p[0])+p[2])+t*((2.0*p[0]-5.0*p[1]+4.0*p[2]-p[3])+t*(-double(p[0])+3.0*p[1]-3.0*p[2]+p[3])));
        int32_t expected=int32_t(std::clamp(y,-8388608.0,8388607.0));
        check(std::abs(spin::Audio::cubic(p[0],p[1],p[2],p[3],f)-expected)<=4,"cubic arithmetic error");
    }
    std::cout<<"PASS 100000 cubic comparisons against double precision\n";
    // Frequency response of the complete 48k -> virtual -> 48k path.
    for(int rate:{8000,16384,32768,40000}) {
      for(int frequency:{440,1000,5000,10000}) {
        if(frequency>=rate/2)continue;
        a->reset();a->set_rate(rate);double power=0;
        for(int i=0;i<48000;++i) {
            int32_t x=int32_t(2097152*std::sin(2*3.141592653589793*frequency*i/48000));
            auto y=a->process({x,x},unity);
            if(i>=24000)power+=double(y.l)*y.l;
        }
        double gain=std::sqrt(power/24000)/(2097152/std::sqrt(2.0));
        if(frequency==440)check(gain>.93&&gain<1.01,"440Hz unity response outside tolerance");
        std::cout<<"unity "<<frequency<<"Hz @ "<<rate<<" Hz: "<<20*std::log10(gain)<<" dB\n";
      }
    }
    // Changing the virtual clock must preserve state and account for every tick.
    a->reset();uint64_t phase_sum=0;
    for(unsigned i=0;i<96000;++i) {
        int rate=8000+int((uint64_t(i)*32000)/95999);
        a->set_rate(rate);phase_sum+=rate;
        auto y=a->process({1000000,-1000000},unity);
        // Exclude the FIR's ordinary startup step-response ringing.
        if(i>128)check(std::abs(y.l)<=1000256&&std::abs(y.r)<=1000256,"clock sweep excursion");
    }
    check(a->ticks==phase_sum/48000&&a->phase==phase_sum%48000,"clock sweep time accounting");
    std::cout<<"PASS continuous 8–40kHz clock sweep and exact tick accounting\n";
    // Actual bank smoke: a -18dBFS stereo signal, all virtual clock extremes,
    // low/mid/high controls. Report clipping rather than hiding effect gain.
    for(unsigned slot=0;slot<spin::program_count;++slot) {
        int64_t peak=0;uint64_t clips=0,nonzero=0;
        for(int rate:{8000,32768,40000})for(uint16_t pot:{uint16_t(0),uint16_t(256),uint16_t(511)}) {
            a->reset();a->set_rate(rate);for(auto&p:a->pots)p=pot;
            for(unsigned i=0;i<96000;++i) {
                int32_t x=i<24000?int32_t(528030*std::sin(2*3.141592653589793*220*i/48000)):0;
                auto y=a->process({x,x},spin::programs[slot]);
                for(int32_t v:{y.l,y.r}) {
                    peak=std::max<int64_t>(peak,std::abs(int64_t(v)));
                    clips+=v>4192256||v<-4194304;nonzero+=v!=0;
                }
            }
        }
        check(nonzero>0,"silent algorithm");
        check(clips==0,"bank clips at -18dBFS smoke level");
        std::cout<<"PASS audio smoke "<<spin::program_names[slot]<<": peak "<<peak<<", clipped "<<clips<<"\n";
    }
} catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
