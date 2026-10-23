// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include "fixed.h"
#include "hrtf_table.h"
#include "elevation_table.h"
namespace spatial {
struct EarPair {int32_t left,right;};
// Both ear filters read one mono history. Convolution precedes the separate
// interaural delay, avoiding duplicated distance filtering/input history work.
class Hrtf {
public:
    Hrtf(){for(int e=0;e<2;++e)coeff_[e][0]=16384;}
    void Geometry(uint32_t phase,int32_t strength,int elevation=2048){
        if(have_geometry_&&phase==phase_&&strength==strength_&&elevation==elevation_)return;
        elevation_=elevation;mix_=elevation>=2048?elevation-2048:2048-elevation;bank_=elevation<2048?0:1;phase_=phase;strength_=strength;have_geometry_=true;quiet_updates_=0;
        uint64_t position=static_cast<uint64_t>(phase)*kHrtfDirections;
        index_=position>>32;next_=index_+1;if(next_==kHrtfDirections)next_=0;
        fraction_=static_cast<uint32_t>(position)>>20;
        // Resolve direction/bank row addresses once per geometry change, rather
        // than rebuilding them for every ear coefficient. Only pointers are
        // cached: interpolation, rounding and smoothing remain unchanged.
        const auto* primary=mix_?kElevationHrtf[bank_]:kHrtf;
        primary_=primary[index_];primary_next_=primary[next_];
        level_=kHrtf[index_];level_next_=kHrtf[next_];
        intermediate_=mix_>0&&mix_<2048;
    }
    void Update(){
        if(quiet_updates_>=kHrtfTaps)return;
        bool changed=false;
        for(int e=0;e<2;++e){
            // Exact anchors use one bank. Only intermediate heights require
            // both banks; interpolate one coefficient index per frame as before.
            int32_t a=primary_[e][update_],b=primary_next_[e][update_];
            int32_t measured=a+(((b-a)*fraction_)>>12);
            if(intermediate_){
                int32_t c=level_[e][update_],d=level_next_[e][update_];
                int32_t level=c+(((d-c)*fraction_)>>12);
                measured=level+(((measured-level)*mix_)>>11);
            }
            int32_t neutral=update_==0?16384:0;
            int32_t target=neutral+(((measured-neutral)*strength_)>>12);
            int32_t delta=target-coeff_[e][update_];
            changed|=delta!=0;
            coeff_[e][update_]=SlewPower<2>(coeff_[e][update_],target);
        }
        quiet_updates_=changed?0:quiet_updates_+1;
        update_=(update_+1)&(kHrtfTaps-1);
    }
    EarPair Process(int32_t sample){
        head_=(head_-1)&(kHrtfTaps-1);
        history_[head_]=history_[head_+kHrtfTaps]=sample;
        const int32_t* x=history_+head_;
        int32_t left=0,right=0;
        // Eight paired MACs per iteration reduce loop and input-load overhead.
        for(int i=0;i<kHrtfTaps;i+=8){
            left+=x[i]*coeff_[0][i];right+=x[i]*coeff_[1][i];
            left+=x[i+1]*coeff_[0][i+1];right+=x[i+1]*coeff_[1][i+1];
            left+=x[i+2]*coeff_[0][i+2];right+=x[i+2]*coeff_[1][i+2];
            left+=x[i+3]*coeff_[0][i+3];right+=x[i+3]*coeff_[1][i+3];
            left+=x[i+4]*coeff_[0][i+4];right+=x[i+4]*coeff_[1][i+4];
            left+=x[i+5]*coeff_[0][i+5];right+=x[i+5]*coeff_[1][i+5];
            left+=x[i+6]*coeff_[0][i+6];right+=x[i+6]*coeff_[1][i+6];
            left+=x[i+7]*coeff_[0][i+7];right+=x[i+7]*coeff_[1][i+7];
        }
        return {left>>14,right>>14};
    }
private:
    static_assert(kElevationMixtureL1Bound*4096<2147483647,"FIR accumulator bound");
    static_assert((kElevationMixtureL1Bound*4096>>14)<32768,"ear delay int16 bound");
    static_assert(kHrtfTaps==32||kHrtfTaps==64,"supported power-of-two tap count");
    // M0+ word loads have immediate offsets; signed halfword loads need extra
    // offset setup. Expand only working state; measured banks remain int16.
    int32_t history_[kHrtfTaps*2]={},coeff_[2][kHrtfTaps]={};
    int32_t elevation_=2048,mix_=0,bank_=1,head_=0,index_=0,next_=1,fraction_=0,strength_=4095,update_=0,quiet_updates_=0;
    uint32_t phase_=0;
    bool have_geometry_=false,intermediate_=false;
    // Defaults preserve Update-before-Geometry behaviour for standalone callers.
    const int16_t (*primary_)[kHrtfTaps]=kHrtf[0];
    const int16_t (*primary_next_)[kHrtfTaps]=kHrtf[1];
    const int16_t (*level_)[kHrtfTaps]=kHrtf[0];
    const int16_t (*level_next_)[kHrtfTaps]=kHrtf[1];
};
}
