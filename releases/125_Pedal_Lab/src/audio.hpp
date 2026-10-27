// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "machine.hpp"
#include "fir_tables.hpp"
namespace spin {
struct Stereo { int32_t l=0,r=0; };

// Fixed 48kHz hardware; asynchronously sample the input at the virtual clock.
// Input FIR + causal cubic event interpolation; cubic output reconstruction.
// Two virtual frames + one host frame of interpolation latency, plus the FIR.
// This is NOT a model of the FV-1 analog converters.
struct Audio {
    Machine machine;
    uint16_t pots[3]={256,256,256};
    uint32_t phase=0; // rational phase in Hz; exact 32768 ticks / 48000 host frames
    int32_t rate=0;
    int32_t coefficients[33]{};
    Audio() {set_rate(32768);}
    // Mirrored circular history makes both sides of the symmetric FIR linear
    // reads: 17 products/channel instead of 33, with no per-tap wrap branch.
    int32_t history[2][66]{};
    unsigned head=0;
    Stereo inputs[4]{}, outputs[4]{};
    uint64_t ticks=0;
    void set_rate(int32_t hz) {
        hz=std::clamp<int32_t>(hz,8000,40000);
        if(hz==rate)return;
        rate=hz;
        unsigned row=unsigned(rate-8000)/1000;
        unsigned blend=unsigned(rate-8000)%1000;
        int sum=0;
        for(unsigned k=0;k<33;++k) {
            int32_t c=fir[row][k];
            if(row<32)c+=(int32_t(fir[row+1][k]-c)*int32_t(blend))/1000;
            coefficients[k]=c;sum+=c;
        }
        coefficients[16]+=32768-sum;
    }
    void reset_audio() {
        phase=0; head=0;
        for(auto &v:inputs)v={};
        for(auto &v:outputs)v={};
        for(auto &channel:history) for(auto &v:channel)v=0;
    }
    void reset() {machine.reset();reset_audio();ticks=0;}
    static int32_t lerp(int32_t a,int32_t b,uint32_t fraction) {
        const int32_t d=b-a;
        return a+(d>>16)*int32_t(fraction)+int32_t((uint32_t(d&65535)*fraction)>>16);
    }
    // Catmull–Rom cubic. Decomposed multiplies keep this integer-only on M0+.
    // The middle interval is reconstructed with a one-sample lookahead delay.
    static int32_t cubic(int32_t p0,int32_t p1,int32_t p2,int32_t p3,uint32_t t) {
        int32_t a=-p0+3*p1-3*p2+p3;
        int32_t b=2*p0-5*p1+4*p2-p3;
        int32_t c=-p0+p2;
        return sat24(p1+(lerp(0,c+lerp(0,b+lerp(0,a,t),t),t)>>1));
    }
    static Stereo interpolate(const Stereo *v,uint32_t t) {
        return {cubic(v[0].l,v[1].l,v[2].l,v[3].l,t),
                cubic(v[0].r,v[1].r,v[2].r,v[3].r,t)};
    }
    Stereo filter(Stereo in) {
        history[0][head]=history[0][head+33]=in.l>>8;
        history[1][head]=history[1][head+33]=in.r>>8;
        const int32_t *left=&history[0][head+33],*right=&history[1][head+33];
        int32_t sum[2]={left[-16]*coefficients[16],right[-16]*coefficients[16]};
        for(unsigned k=0;k<16;++k) {
            int32_t c=coefficients[k];
            sum[0]+=(left[-int(k)]+left[-32+int(k)])*c;
            sum[1]+=(right[-int(k)]+right[-32+int(k)])*c;
        }
        if(++head==33)head=0;
        return {sum[0]>>7,sum[1]>>7};
    }
    template<class Engine> Stereo process_with(Stereo in,Engine engine) {
        Stereo filtered=filter(in);
        inputs[0]=inputs[1];inputs[1]=inputs[2];inputs[2]=inputs[3];
        inputs[3]=filtered;
        uint32_t before=phase; phase+=uint32_t(rate);
        if(phase>=48000) {
            phase-=48000;
            uint32_t f=((48000-before)<<16)/uint32_t(rate);
            f=std::min<uint32_t>(f,65536);
            Stereo sampled=interpolate(inputs,f);
            outputs[0]=outputs[1];outputs[1]=outputs[2];outputs[2]=outputs[3];
            outputs[3]=engine(sampled);
            ++ticks;
        }
        return interpolate(outputs,(phase<<16)/48000);
    }
    Stereo process(Stereo in,Program program) {
        return process_with(in,[&](Stereo sampled){
            machine.begin(sampled.l,sampled.r,pots);program(machine);
            Stereo result{machine.regs[0x16],machine.regs[0x17]};machine.end_sample();return result;
        });
    }
};
} // namespace spin
