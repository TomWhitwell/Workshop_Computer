// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Original Mixer mapping using this project's MuControls; source/licensing
// for WaveSeq/EightMU and rppicomidi is in ../THIRD_PARTY_NOTICES.md.
#pragma once
#include "modes.h"
#include "mu_controls.h"
namespace spatial {
class MixerMuControls {
public:
    uint32_t Apply(const MuInput& in,Modes& modes,int32_t& main,int32_t& x,int32_t& y,Config& cfg){
        bool fresh=in.connected&&(!connected_||session_!=in.session);
        if(fresh){session_=in.session;previous_buttons_=in.buttons;source_=modes.SelectedSource();}
        uint32_t pressed=in.connected&&!fresh?in.buttons&~previous_buttons_:0;
        previous_buttons_=in.connected?in.buttons:0;connected_=in.connected;
        if(pressed&1)modes.SelectMixer(0,main,x,y); // A and B select their named source.
        else if(pressed&2)modes.SelectMixer(1,main,x,y);
        if(source_!=modes.SelectedSource()){
            source_=modes.SelectedSource();controls_.Rearm(7);
        }
        int32_t position=modes.MixerValue(0),distance=modes.MixerValue(1),level=modes.MixerValue(2);
        Config mapped=cfg;mapped.value[0]=static_cast<uint16_t>(position);
        // Reuse the tested eight-fader pickup engine: its former
        // separation field becomes a selected-source position fader in Mixer.
        MuInput faders=in;
        // Mixer never uses accelerometer/gyro or motion buttons. D's diagnostic
        // LEDs are handled on core 1; no button alters this pickup engine.
        faders.buttons=0;faders.roll=0;faders.yaw=0;
        controls_.Apply(faders,position,distance,level,mapped,false,false);
        uint32_t picked=controls_.Picked(),takeover=0;
        if(picked&1){x=distance;takeover|=2;}
        if(picked&2){y=level;takeover|=4;}
        if(picked&4){main=mapped.value[0];takeover|=1;}
        for(int i=1;i<5;++i)cfg.value[i]=mapped.value[i]; // Keep unused separation intact.
        return takeover;
    }
    void Rearm(){controls_.Rearm(7);}
    uint32_t Feedback()const{return (controls_.Picked()&127u)|(source_?512u:0u)|1024u;}
private:
    MuControls controls_;
    uint32_t previous_buttons_=0,session_=0;
    int source_=0;bool connected_=false;
};
}
