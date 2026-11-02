// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "runtime.hpp"
#include <atomic>
// One producer and one consumer per queue. No locks or waiting in the ISR.
template<class T,unsigned N> struct SpinQueue {
    static_assert(N>0&&(N&(N-1))==0,"Queue capacity must be a power of two");
    T values[N]{};std::atomic<uint32_t> write{0},read{0};
    bool push(const T&v){auto w=write.load(std::memory_order_relaxed);if(w-read.load(std::memory_order_acquire)==N)return false;values[w&(N-1)]=v;write.store(w+1,std::memory_order_release);return true;}
    bool pop(T&v){auto r=read.load(std::memory_order_relaxed);if(r==write.load(std::memory_order_acquire))return false;v=values[r&(N-1)];read.store(r+1,std::memory_order_release);return true;}
    void discard(){read.store(write.load(std::memory_order_acquire),std::memory_order_release);}
};
// Core 1 exclusively owns Audio, including resampling, coefficients and delay RAM.
// Core 0 submits one hardware frame per interrupt; replies return exactly
// 16 hardware frames later. No DSP or rate-table calculation runs in the ISR.
struct SpinWorker {
    static constexpr unsigned latency=16;
    struct Job{SpinFrameInput input;spin::Program program;uint32_t sequence;uint32_t rate;};
    struct Reply{spin::Stereo audio;uint32_t sequence;};
    SpinQueue<Job,64> jobs;SpinQueue<Reply,64> replies;
    spin::Audio&audio;
    std::atomic<uint32_t> reset_request{0},reset_done{0},max_work_us{0};
    std::atomic<uint32_t> resample_max_us{0},virtual_max_us{0};
    uint32_t sequence=0,misses=0;unsigned warmup=0;spin::Stereo held{};
    Reply future{};bool have_future=false;
    explicit SpinWorker(spin::Audio&a):audio(a){}
    template<class Clock> bool step(Clock clock){
        uint32_t epoch=reset_request.load(std::memory_order_acquire);
        if(epoch!=reset_done.load(std::memory_order_relaxed)){
            jobs.discard();audio.reset();max_work_us.store(0,std::memory_order_relaxed);
            resample_max_us.store(0,std::memory_order_relaxed);virtual_max_us.store(0,std::memory_order_relaxed);
            reset_done.store(epoch,std::memory_order_release);return true;
        }
        Job j;if(!jobs.pop(j))return false;
        const bool changed_rate=audio.rate!=int32_t(j.rate);
        const uint64_t ticks=audio.ticks;
        uint32_t before=clock();spin::Stereo result;
        for(unsigned i=0;i<3;++i)audio.pots[i]=j.input.pots[i];
        audio.set_rate(j.rate);
        spin_process_frame(&audio,&j.input.audio,&result,j.program);
        uint32_t elapsed=clock()-before;
        if(elapsed>max_work_us.load(std::memory_order_relaxed))max_work_us.store(elapsed,std::memory_order_release);
        if(!changed_rate){
            auto &maximum=audio.ticks!=ticks?virtual_max_us:resample_max_us;
            if(elapsed>maximum.load(std::memory_order_relaxed))maximum.store(elapsed,std::memory_order_release);
        }
        // A full reply queue cannot occur at supported rates/latency. Dropping
        // is bounded and is detected by the sequence check on the audio core.
        replies.push({result,j.sequence});return true;
    }
    spin::Stereo submit(spin::Stereo in,const uint16_t*pots,spin::Program program,uint32_t rate=32768){
        uint32_t current=sequence++;Job j{{in,{pots[0],pots[1],pots[2]}},program,current,rate};
        if(!jobs.push(j))++misses;
        if(warmup<latency){++warmup;return {};}
        const uint32_t wanted=current-latency;Reply r;bool found=false;
        if(have_future&&int32_t(future.sequence-wanted)<0)have_future=false;
        if(have_future&&future.sequence==wanted){held=future.audio;have_future=false;found=true;}
        // No unbounded drain: at most the fixed queue capacity is inspected.
        for(unsigned n=0;n<64&&!have_future&&!found;++n){
            if(!replies.pop(r))break;
            if(r.sequence==wanted){held=r.audio;found=true;}
            else if(int32_t(r.sequence-wanted)>0){future=r;have_future=true;}
        }
        if(!found)++misses;
        return held;
    }
    void begin_reset(){reset_request.store(reset_request.load(std::memory_order_relaxed)+1,std::memory_order_release);}
    bool reset_ready(){return reset_done.load(std::memory_order_acquire)==reset_request.load(std::memory_order_acquire);}
    void restart_stream(){replies.discard();sequence=0;warmup=0;held={};have_future=false;}
};
