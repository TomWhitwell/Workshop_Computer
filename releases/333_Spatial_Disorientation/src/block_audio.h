// Source revision and original MIT notice: ../vendor/Workshop_BlockAudioCard/LICENSE and ../THIRD_PARTY_NOTICES.md.
// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Fixed ring/two-block scheduling follows Adrian Vos's Workshop_BlockAudioCard
// reference. ComputerCard 0.4.0 still owns the hardware ISR and jack probe here.
#pragma once
#include "dsp/spatial.h"
#ifdef PICO_ON_DEVICE
#include "hardware/sync.h"
#else
#include <atomic>
inline void __dmb(){std::atomic_thread_fence(std::memory_order_seq_cst);}
#endif
namespace spatial {
class BlockAudio {
public:
    static constexpr unsigned kFrames=64,kSlots=4;
    // Only the audio ISR calls Tick. Never waits for the worker; a missed
    // deadline outputs silence and latches a fault instead of reusing old audio.
    Stereo Tick(int32_t a,int32_t b,const Scene& scene,const Config& config) {
        unsigned capture=number_&3;
        if(frame_==0) {
            // Reclaim output which arrived after its scheduled playback block.
            for(auto& slot:slots_)if(slot.state==Output) {
                __dmb();
                if(static_cast<int32_t>(number_-slot.number)>2){slot.state=Free;fault=1;}
            }
            capturing_=slots_[capture].state==Free;
            if(capturing_){slots_[capture].state=Capturing;slots_[capture].number=number_;}
            else fault=1;
            playing_=false;
            if(number_>=2) {
                unsigned index=(number_-2)&3;
                if(slots_[index].state==Output) {
                    __dmb();playing_=slots_[index].number==number_-2;
                }
                if(!playing_)fault=1;
            }
        }
        if(capturing_)slots_[capture].input[frame_]={{a,b},scene,config};
        Stereo result=playing_?slots_[(number_-2)&3].output[frame_]:Stereo{};
        if(++frame_==kFrames) {
            frame_=0;
            if(playing_){__dmb();slots_[(number_-2)&3].state=Free;}
            if(capturing_){__dmb();slots_[capture].state=Ready;}
            ++number_;
        }
        return result;
    }
    // Core 1 only. Select oldest ready block, including after a dropped capture.
    // Core 0 never alters Ready/Rendering slots, so the worker owns their data.
    bool Render(Engine& engine) {
        int candidate=-1;
        for(unsigned i=0;i<kSlots;++i)if(slots_[i].state==Ready) {
            __dmb();
            if(candidate<0 || static_cast<int32_t>(slots_[i].number-slots_[candidate].number)<0)candidate=i;
        }
        if(candidate<0)return false;
        Slot& slot=slots_[candidate];slot.state=Rendering;__dmb();
        for(unsigned i=0;i<kFrames;++i) {
            const auto& f=slot.input[i];engine.SetScene(f.scene,f.config);
            slot.output[i]=engine.Process(f.audio[0],f.audio[1]);
        }
        __dmb();
        // Never make an overdue block eligible for a later playback cycle.
        if(static_cast<int32_t>(number_-slot.number)>2){slot.state=Free;worker_fault|=1;}
        else slot.state=Output;
        return true;
    }
    alignas(4) volatile uint32_t fault=0; // ISR writes only.
    alignas(4) volatile uint32_t worker_fault=0; // Worker writes only.
private:
    enum State:uint32_t {Free,Capturing,Ready,Rendering,Output};
    struct Frame {int32_t audio[2];Scene scene;Config config;};
    struct Slot {
        Frame input[kFrames];Stereo output[kFrames];
        uint32_t number=0;
        alignas(4) volatile uint32_t state=Free;
    } slots_[kSlots];
    alignas(4) volatile uint32_t number_=0;
    unsigned frame_=0;
    bool capturing_=false,playing_=false;
};
}
