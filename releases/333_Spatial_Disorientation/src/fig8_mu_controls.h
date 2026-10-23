// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Original Fig8 mapping; reuses our WaveSeq/EightMU-derived pickup integration.
// Source and license notices: ../THIRD_PARTY_NOTICES.md.
#pragma once
#include "mu_controls.h"
namespace spatial {
class Fig8MuControls {
public:
    // Core 0, 1 kHz. Return an A-button phase-reset edge; Down remains a freeze.
    bool Apply(const MuInput& in,int32_t& main,int32_t& x,int32_t& y,Config& cfg,uint32_t savedMovement=0){
        int32_t panel[3]={main,x,y};
        if(!initialized_){
            initialized_=true;
            for(int i=0;i<3;++i)last_[i]=previous_panel_[i]=panel[i];
        }
        bool fresh=in.connected&&(!connected_||session_!=in.session);
        if(fresh){session_=in.session;motion_=false;previous_buttons_=in.buttons;d_ticks_=(in.buttons&8)?500:0;}
        uint32_t pressed=in.connected&&!fresh?in.buttons&~previous_buttons_:0;
        previous_buttons_=in.connected?in.buttons:0;connected_=in.connected;
        if(!connected_||fresh){movement_=savedMovement<3?savedMovement:0;if(!connected_)motion_=false;}
        // D tap selects movement on release. Hold >=500 ms reserves diagnostics,
        // and release of a long hold never changes the movement.
        if(in.connected&&!fresh&&(in.buttons&8)){if(d_ticks_<500)++d_ticks_;}
        else if(in.connected&&!fresh&&d_ticks_){if(d_ticks_<500)movement_=(movement_+1)%3;d_ticks_=0;}
        else if(!in.connected)d_ticks_=0;
        if(pressed&2){
            motion_=!motion_;
            if(motion_)Recenter((pending_&1)?last_[0]:panel[0],in.pitch,in.roll);
        }
        bool reset=(pressed&1)!=0;
        if(reset)Recenter(panel[0],in.pitch,in.roll);
        // Share the tested fader pickup implementation, but handle Fig8 buttons
        // and bounded depth motion here rather than using circular heading.
        MuInput faders=in;faders.buttons=0;faders.roll=faders.pitch=faders.yaw=0;
        if(pending_&2)x=last_[1];
        if(pending_&4)y=last_[2];
        controls_.Apply(faders,main,x,y,cfg,false,false);
        uint32_t owners=(controls_.Picked()&1?2u:0u)|(controls_.Picked()&2?4u:0u);
        if(motion_){
            int32_t amount=controls_.MotionDepth();
            // Pitch places front/back depth directly; roll turns the whole
            // path left/right. No angular-rate integration or yaw dependency.
            int32_t forward=((Clamp(in.pitch,-2032,2032)-pitch_zero_)*amount)>>12;
            // EightMU positive roll lifts the right edge: invert for right-edge-down = right.
            int32_t sideways=((roll_zero_-Clamp(in.roll,-2032,2032))*amount)>>12;
            int32_t desired=Clamp(anchor_-forward,0,4095);
            rotation_target_=static_cast<uint32_t>(sideways)*524288u;
            depth_=Clamp(Slew(depth_,desired,16),0,4095);
            main=depth_;owners|=1;
        }
        if(!motion_)rotation_target_=0;
        int32_t angular_delta=static_cast<int32_t>(rotation_target_-rotation_);
        rotation_+=static_cast<uint32_t>(angular_delta/16+(angular_delta>0?1:(angular_delta<0?-1:0)));
        int32_t values[3]={main,x,y};
        for(int i=0;i<3;++i){
            uint32_t bit=1u<<i;
            if(owners&bit)pending_&=~bit;
            else{
                if(owned_&bit)pending_|=bit;
                if(pending_&bit){
                    int32_t diff=panel[i]-last_[i],old=previous_panel_[i]-last_[i];
                    if((diff>=-64&&diff<=64)||(old<0&&diff>=0)||(old>0&&diff<=0))pending_&=~bit;
                }
                values[i]=(pending_&bit)?last_[i]:panel[i];
            }
            last_[i]=values[i];previous_panel_[i]=panel[i];
        }
        owned_=owners;main=values[0];x=values[1];y=values[2];
        frozen_=in.connected&&(in.buttons&4);
        return reset;
    }
    uint32_t Rotation()const{return rotation_;}
    bool Motion()const{return motion_;}
    bool Frozen()const{return frozen_;}
    uint32_t Picked()const{return controls_.Picked();}
    uint32_t PanelPending()const{return pending_;}
    uint32_t Movement()const{return movement_;}
    uint32_t Feedback(bool panelFrozen=false)const{
        return controls_.Picked()|2048u|(motion_?256u:0u)|((panelFrozen||frozen_)?4096u:0u)
            |(movement_<<13)|(d_ticks_>=500?32768u:0u);
    }
private:
    void Recenter(int32_t main,int32_t pitch,int32_t roll){
        depth_=anchor_=Clamp(main,0,4095);roll_zero_=Clamp(roll,-2032,2032);pitch_zero_=Clamp(pitch,-2032,2032);rotation_=rotation_target_=0;
    }
    MuControls controls_;
    int32_t last_[3]={},previous_panel_[3]={},depth_=2048,anchor_=2048,roll_zero_=0,pitch_zero_=0;
    uint32_t session_=0,previous_buttons_=0,pending_=0,owned_=0;
    uint32_t movement_=0,d_ticks_=0,rotation_=0,rotation_target_=0;
    bool initialized_=false,connected_=false,motion_=false,frozen_=false;
};
}
