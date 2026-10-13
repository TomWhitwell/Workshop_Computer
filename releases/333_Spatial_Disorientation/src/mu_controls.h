// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include "config.h"
#include "dsp/spatial.h"
namespace spatial {
struct MuInput {
    int32_t fader[8]={}; // EightMU units 0..4064, converted below to full panel range.
    int32_t roll=0,yaw=0;
    uint32_t buttons=0,connected=0,faders_ready=0,session=0;
};
// Core 0 only, at 1 kHz. USB never writes DSP/knob state directly.
class MuControls {
public:
    bool Apply(const MuInput& in,int32_t& main,int32_t& x,int32_t& y,Config& cfg,bool panelReset=false,bool orbitStop=true){
        if(!in.connected){
            picked_=0;connected_=false;motion_=false;previous_buttons_=0;
            depth_=4095;
            return false;
        }
        if(!connected_||session_!=in.session){
            connected_=true;session_=in.session;picked_=0;motion_=false;
            have_faders_=false;previous_buttons_=in.buttons;
        }
        uint32_t pressed=in.buttons&~previous_buttons_;previous_buttons_=in.buttons;
        if(pressed&2){motion_=!motion_;Recenter(main,in.roll);}
        bool reset=(pressed&1)!=0;
        if(reset||panelReset)Recenter(main,in.roll);
        int32_t target[8]={x,y,cfg.value[0],cfg.value[1],cfg.value[2],cfg.value[3],cfg.value[4],4095};
        if(in.faders_ready){
            for(int i=0;i<8;++i){
                int32_t raw=Clamp(in.fader[i],0,4064);
                int32_t value=raw+(raw>>7); // 0..4095 without ISR division.
                // Pickup engages near the target or while crossing it. The first
                // valid snapshot cannot invent a crossing from unreceived zero.
                int32_t diff=value-target[i],old=previous_[i]-target[i];
                if((diff>=-64&&diff<=64)||(have_faders_&&((old<0&&diff>=0)||(old>0&&diff<=0))))picked_|=1u<<i;
                previous_[i]=value;
                if(picked_&(1u<<i))target[i]=value;
            }
            have_faders_=true;
        }
        x=target[0];y=target[1];
        depth_=target[7];
        for(int i=0;i<5;++i)cfg.value[i]=static_cast<uint16_t>(target[i+2]);
        if(orbitStop&&(in.buttons&4))x=2048; // Hold C to stop the orbit; release resumes it.
        if(motion_){
            // Yaw is a rotation RATE, not heading. Integrate at 1 kHz, bounded
            // to about half a turn/sec at full scale. Deadband rejects drift.
            int32_t yaw=Clamp(in.yaw,-2032,2032);
            if(yaw>-32&&yaw<32)yaw=0;
            yaw=(yaw*target[7])>>12;
            yaw_phase_+=static_cast<uint32_t>(yaw*1057);
            int32_t tilt=(Clamp(in.roll,-2032,2032)-roll_zero_)*target[7]>>12;
            uint32_t desired=anchor_+yaw_phase_+static_cast<uint32_t>(tilt)*1048576u;
            // Smooth around the circle: crossing 0/360 never sweeps backwards.
            int32_t delta=static_cast<int32_t>(desired-angle_);
            angle_+=static_cast<uint32_t>(delta/16+(delta>0?1:(delta<0?-1:0)));
            main=static_cast<int32_t>(angle_>>20);
        }
        return reset;
    }
    // Re-arm only source-specific faders when changing Mixer source. The next
    // snapshot cannot invent a crossing from the other source's last value.
    void Rearm(uint32_t mask){picked_&=~mask;have_faders_=false;}
    uint32_t Picked() const {return picked_;}
    bool Motion() const {return motion_;}
    int32_t MotionDepth() const {return depth_;}
private:
    void Recenter(int32_t main,int32_t roll){
        anchor_=static_cast<uint32_t>(main)*1048576u;angle_=anchor_;yaw_phase_=0;
        roll_zero_=Clamp(roll,-2032,2032);
    }
    int32_t previous_[8]={},roll_zero_=0,depth_=4095;
    uint32_t picked_=0,previous_buttons_=0,anchor_=0,angle_=0,yaw_phase_=0,session_=0;
    bool connected_=false,have_faders_=false,motion_=false;
};
}
