// Pinned WaveSeq/EightMU source and licenses: ../vendor/EightMU/SOURCE.md.
// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Host-role pattern adapted from Chris Johnson's WaveSeq (MIT).
#pragma once
#include "EightMU.h"
#include "shared.h"
namespace spatial {
inline void RunMuHost(Shared& shared,void (*worker)()){
    static EightMU mu;
    board_init();tuh_init(0);
    __dmb();shared.usb_ready=1;
    uint32_t last_input=0,last_led=0;
    uint32_t last_movement=3,movement_notice=0;
    while(true){
        worker(); // Render the oldest waiting block before servicing USB.
        mu.Poll();
        uint32_t now=time_us_32();
        if(now-last_input>=1000){
            last_input=now;
            MuInput input;
            input.connected=mu.Connected();input.faders_ready=mu.FadersReady();input.session=mu.Session();
            for(int i=0;i<8;++i)input.fader[i]=mu.Fader(i);
            input.roll=mu.Roll();input.yaw=mu.FreshYaw(now);
            for(int i=0;i<4;++i)if(mu.Button(i))input.buttons|=1u<<i;
            shared.PublishMu(input);
        }
        if(mu.Connected()&&now-last_led>=20000){
            last_led=now;
            uint32_t feedback=shared.mu_feedback;
            uint32_t movement=(feedback>>13)&3;
            if((feedback&2048)&&movement!=last_movement){last_movement=movement;movement_notice=now;}
            bool notice=(feedback&2048)&&now-movement_notice<1000000;
            for(int i=0;i<8;++i){
                int32_t brightness;
                if(notice)brightness=static_cast<uint32_t>(i)==movement?4095:0;
                else if(mu.Button(3)&&(!(feedback&2048)||(feedback&32768))){
                    // Each LED is a 150-us band of the peak DSP block time.
                    brightness=shared.block_peak_us>static_cast<uint32_t>(i*150)?2048:0;
                    if(shared.flags&4)brightness=(now/125000)&1?4095:0;
                }else if(i==7&&(feedback&2048)&&(feedback&4096))brightness=(now/125000)&1?4095:0;
                else if(i==7&&(feedback&1024)){
                    // Mixer LED 8: steady A, blinking B. Fader 8 is unused.
                    brightness=(feedback&512)&&!((now/250000)&1)?0:1024;
                }else if(i==7&&(feedback&256))brightness=4095;
                else if(feedback&(1u<<i))brightness=256+(mu.Fader(i)*3>>2);
                else brightness=(now/125000)&1?512:0; // Waiting for pickup.
                mu.SetLed(i,brightness);
            }
        }
        tight_loop_contents();
    }
}
}
