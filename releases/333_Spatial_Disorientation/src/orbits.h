// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#pragma once
#include "dsp/spatial.h"
namespace spatial {
class Orbits {
public:
    // Call at sample rate so short clock/reset edges cannot fall between scans.
    void Pulse(bool connected,bool edge,bool reset) {
        ++sample_;
        connected_=connected;
        if (edge && connected) {
            uint32_t interval=sample_-last_edge_;
            if (have_edge_ && interval>=1200 && interval<=2880000) period_=interval;
            last_edge_=sample_;have_edge_=true;
        }
        if (!connected) { have_edge_=false;period_=0; }
        if (reset) phase_=0;
    }
    // Called at 1 kHz. Position wraps, distance clamps, speed has a centre deadband.
    void Controls(int32_t main,int32_t x,int32_t y,int32_t cv1,int32_t cv2,bool linked,const Config& cfg) {
        base_=static_cast<uint32_t>(main-2048+cv1*2)*1048576u;
        distance_=Clamp(y+cv2*2,0,4095);
        linked_=linked;
        separation_=static_cast<uint32_t>(cfg.value[0])*1048576u;
        int32_t displacement=x-2048;
        int32_t magnitude=displacement<0 ? -displacement : displacement;
        if (magnitude<=128) step_=0;
        else {
            magnitude=Clamp(magnitude-128,0,1919);
            int32_t rate=(magnitude*134218)/1919; // up to +/-1.5 turns/sec (75% of the original).
            if (ClockLocked()) {
                uint32_t denom=period_*cfg.value[5]; // bounded above: 46,080,000.
                rate=static_cast<int32_t>(4294967296ull/denom);
                rate=Clamp(rate,1,178957);
            }
            step_=displacement>0 ? rate : -rate;
        }
    }
    Scene Advance(bool frozen=false) {
        if(!frozen)phase_+=static_cast<uint32_t>(step_);
        return {{base_+phase_,base_+(linked_ ? phase_ : 0u-phase_)+separation_},distance_};
    }
    bool ClockLocked() const {
        uint32_t timeout=period_*3;
        if(timeout<144000) timeout=144000;
        return connected_ && period_ && sample_-last_edge_<timeout;
    }
    int32_t Step() const { return step_; }
private:
    uint32_t sample_=0,last_edge_=0,period_=0,phase_=0,base_=0,separation_=0x80000000u;
    int32_t step_=0,distance_=0;
    bool linked_=false,connected_=false,have_edge_=false;
};
}
