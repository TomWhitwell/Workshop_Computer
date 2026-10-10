// WaveSeq switch-bank pickup reviewed; original implementation. See ../THIRD_PARTY_NOTICES.md.
// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include "startup_mode.h"
#include "orbits.h"
#include "settings.h"
namespace spatial {
class Modes {
public:
    // Select before USB pickup so each controller compares against the newly
    // selected source's stored values, rather than the old source or panel.
    void SelectMixer(int source,int32_t main,int32_t x,int32_t y){
        if(!initialized_){
            initialized_=true;selected_=source==1?1:0;picked_=7;
            int32_t knobs[3]={main,x,y};
            for(int i=0;i<3;++i)value_[selected_][i]=previous_[i]=Clamp(knobs[i],0,4095);
        }else if(source>=0&&source!=selected_){selected_=source;picked_=0;}
    }
    void RestoreMixer(const Placement& placement,int32_t main,int32_t x,int32_t y){
        initialized_=true;picked_=owner_=0;
        for(int e=0;e<2;++e)for(int i=0;i<3;++i)value_[e][i]=placement.value[e*3+i];
        previous_[0]=main;previous_[1]=x;previous_[2]=y;
    }
    Placement MixerPlacement()const{
        Placement p;for(int e=0;e<2;++e)for(int i=0;i<3;++i)p.value[e*3+i]=value_[e][i];return p;
    }
    int32_t MixerValue(int parameter)const{return value_[selected_][parameter];}
    // source: 0 selects A, 1 selects B, -1 retains the last selection.
    // takeover: Main/X/Y bits for already-picked-up remote controls.
    void Controls(Mode mode,int32_t main,int32_t x,int32_t y,int32_t cv1,int32_t cv2,bool linked,const Config& cfg,int source=-1,uint32_t takeover=0){
        mode_=mode;
        if(mode==Mode::Orbits){orbits_.Controls(main,x,y,cv1,cv2,linked,cfg);return;}
        if(mode==Mode::Disorientation){
            shape_=Clamp(main+cv1*2,0,4095);excursion_=Clamp(y+cv2*2,0,4095);
            orbits_.Controls(2048,x,0,0,0,linked,cfg);return;
        }
        SelectMixer(source,main,x,y);
        int32_t knobs[3]={Clamp(main,0,4095),Clamp(x,0,4095),Clamp(y,0,4095)};
        // On return from USB to the panel, re-arm pickup without treating
        // the old controller's position as a physical knob crossing.
        uint32_t returned=owner_&~takeover;
        picked_&=~returned;
        for(int i=0;i<3;++i)if(returned&(1u<<i))previous_[i]=knobs[i];
        owner_=takeover;
        // Same near/crossing pickup principle as WaveSeq and our 8mu controls.
        // Compare against the previous physical value, never an invented zero.
        for(int i=0;i<3;++i){
            int32_t diff=knobs[i]-value_[selected_][i],old=previous_[i]-value_[selected_][i];
            if((diff>=-64&&diff<=64)||(old<0&&diff>=0)||(old>0&&diff<=0)||(takeover&(1u<<i)))picked_|=1u<<i;
            if(picked_&(1u<<i))value_[selected_][i]=knobs[i];
            previous_[i]=knobs[i];
        }
        // CV modulation applies to both stored placements without overwriting
        // them. Returning CV to zero restores each source's panel setting.
        for(int e=0;e<2;++e){
            fixed_.angle[e]=static_cast<uint32_t>(value_[e][0]-2048+cv1*2)*1048576u;
            int32_t distance=Clamp(value_[e][1]+cv2*2,0,4095);
            if(e){fixed_.distance_b=distance;fixed_.level_b=value_[e][2];}
            else{fixed_.distance=distance;fixed_.level_a=value_[e][2];}
        }
    }
    void Freeze(bool held){frozen_=held;}
    void SetMovement(uint32_t movement){
        if(movement<3&&movement!=movement_){movement_=movement;++path_epoch_;}
    }
    void Pulse(bool connected,bool edge,bool reset){if(mode_!=Mode::Mixer){if(reset)++path_epoch_;orbits_.Pulse(connected,edge,reset);}}
    Scene Advance(){
        if(mode_==Mode::Mixer)return fixed_;
        Scene s=orbits_.Advance(mode_==Mode::Disorientation&&frozen_);
        if(mode_==Mode::Disorientation){s.fig8=true;s.shape=shape_;s.excursion=excursion_;s.movement=movement_;s.path_epoch=path_epoch_;}
        return s;
    }
    bool ClockLocked()const{return mode_!=Mode::Mixer&&orbits_.ClockLocked();}
    int SelectedSource()const{return selected_;}
    uint32_t Pickup()const{return picked_;}
private:
    Orbits orbits_;Scene fixed_;
    Mode mode_=Mode::Orbits;
    int32_t shape_=0,excursion_=0;bool frozen_=false;
    uint32_t movement_=0,path_epoch_=0;
    // Unedited source defaults: A front, B back, near, full panel level.
    int32_t value_[2][3]={{2048,0,4095},{0,0,4095}},previous_[3]={};
    int selected_=0;uint32_t picked_=0,owner_=0;bool initialized_=false;
};
}
