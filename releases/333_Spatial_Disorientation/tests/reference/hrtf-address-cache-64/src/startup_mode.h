// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace spatial {
enum class Mode : uint32_t { Orbits=0, Mixer=1, Disorientation=2 };
// Sample at 1 kHz INSIDE ProcessSample. Let the ADC/pot smoothing settle before
// inspecting the boot gesture. Normal startup always chooses Twin Orbits.
class StartupMode {
public:
    void Tick(bool down,int32_t main){
        if(ready_)return;
        if(!decided_){
            if(++warmup_<50)return;
            decided_=true;selecting_=down;
            if(!down){ready_=true;return;}
            mode_=Region(main);
        }
        if(down){
            released_=0;
            // Small hysteresis stops the lights chattering at third boundaries.
            int32_t lo=static_cast<int32_t>(mode_)*1365;
            int32_t hi=lo+1365;
            if(main<lo-32||main>=hi+32)mode_=Region(main);
        }else if(++released_>=6){selecting_=false;ready_=true;}
    }
    bool Ready() const{return ready_;}
    bool Selecting() const{return selecting_;}
    Mode Selected() const{return mode_;}
    uint32_t LedMask() const{
        return mode_==Mode::Mixer?0x15u:(mode_==Mode::Disorientation?0x2au:0u);
    }
private:
    static Mode Region(int32_t main){return main<1365?Mode::Orbits:(main<2730?Mode::Mixer:Mode::Disorientation);}
    Mode mode_=Mode::Orbits;
    uint32_t warmup_=0,released_=0;
    bool decided_=false,selecting_=false,ready_=false;
};
}
