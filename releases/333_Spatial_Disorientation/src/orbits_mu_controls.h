// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Original two-page mapping reusing this project's WaveSeq/EightMU pickup engine.
#pragma once
#include "mu_controls.h"
#include "settings.h"
namespace spatial {
class OrbitsMuControls {
public:
    bool Apply(const MuInput& in,int32_t& main,int32_t& x,int32_t& y,Config& cfg,const Settings& saved,bool panelReset=false){
        int32_t panel[2]={x,y};
        if(!initialized_){initialized_=true;for(int i=0;i<2;++i)last_[i]=previous_panel_[i]=panel[i];}
        bool fresh=in.connected&&(!connected_||session_!=in.session);
        if(!in.connected||fresh){
            pending_|=owned_&3;owned_=0;page_=false;d_ticks_=0;height_picked_=0;have_height_=false;
            height_=saved.orbits_height;tilt_=saved.orbits_tilt;bearing_=saved.orbits_bearing;
            if(fresh){session_=in.session;d_ticks_=(in.buttons&8)?500:0;}
        }
        if(in.connected&&!fresh){
            if(in.buttons&8){if(d_ticks_<500)++d_ticks_;}
            else if(d_ticks_){if(d_ticks_<500){page_=!page_;controls_.Rearm(7);height_picked_=0;have_height_=false;}d_ticks_=0;}
        }
        connected_=in.connected;
        // Retain normal-page remote values on the other page and until pickup.
        if((owned_|pending_)&1)x=last_[0];
        if((owned_|pending_)&2)y=last_[1];
        if(owned_&4)cfg.value[0]=separation_;
        MuInput faders=in;
        bool reset=controls_.Apply(faders,main,x,y,cfg,panelReset,false,page_?248:255);
        uint32_t picked=controls_.Picked();
        if(in.connected&&!page_)owned_|=picked&7;
        if(in.connected&&page_&&in.faders_ready){
            int32_t target[3]={height_==4096?4095:height_,Clamp(tilt_*2,0,4095),bearing_};
            for(int i=0;i<3;++i){
                int32_t raw=Clamp(in.fader[i],0,4064),value=raw+(raw>>7);
                int32_t diff=value-target[i],before=previous_height_[i]-target[i];
                if((diff>=-64&&diff<=64)||(have_height_&&((before<0&&diff>=0)||(before>0&&diff<=0))))height_picked_|=1u<<i;
                previous_height_[i]=value;
                if(height_picked_&(1u<<i))target[i]=value;
            }
            have_height_=true;
            // Preserve exact saved values until their individual fader picks up.
            if(height_picked_&1)height_=target[0]+(target[0]==4095);
            if(height_picked_&2)tilt_=(target[1]+1)>>1;
            if(height_picked_&4)bearing_=target[2];
        }
        int32_t values[2]={x,y};
        for(int i=0;i<2;++i){
            uint32_t bit=1u<<i;
            if(in.connected&&(owned_&bit))pending_&=~bit;
            else if(pending_&bit){
                int32_t diff=panel[i]-last_[i],before=previous_panel_[i]-last_[i];
                if((diff>=-64&&diff<=64)||(before<0&&diff>=0)||(before>0&&diff<=0))pending_&=~bit;
                else values[i]=last_[i];
            }
            last_[i]=values[i];previous_panel_[i]=panel[i];
        }
        separation_=cfg.value[0];x=values[0];y=values[1];
        frozen_=in.connected&&(in.buttons&4);if(frozen_)x=2048;
        return reset;
    }
    uint16_t Height()const{return height_;}uint16_t Tilt()const{return tilt_;}uint16_t Bearing()const{return bearing_;}
    bool Motion()const{return controls_.Motion();}bool HeightPage()const{return page_;}
    uint32_t Feedback()const{
        uint32_t picked=controls_.Picked();if(page_)picked=(picked&248)|height_picked_;
        return picked|(1u<<30)|(page_?1u<<29:0)|(d_ticks_>=500?1u<<28:0)|(Motion()?256u:0)|(frozen_?4096u:0);
    }
private:
    MuControls controls_;
    int32_t last_[2]={},previous_panel_[2]={},previous_height_[3]={},separation_=2048;
    uint32_t session_=0,d_ticks_=0,owned_=0,pending_=0,height_picked_=0;
    uint16_t height_=2048,tilt_=0,bearing_=0;
    bool connected_=false,page_=false,have_height_=false,initialized_=false,frozen_=false;
};
}
