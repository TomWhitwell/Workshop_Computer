// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
// Exercise actual Storage and UsbEditor command parser/handshake with mock hardware.
// The mock asserts physical-order preconditions but cannot reproduce ADC hardware.
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <vector>
#include <atomic>
#define private public
#include "usb_editor.h"
#undef private
uint8_t mock_flash[4096];
namespace spatial {extern "C" {char __flash_binary_end=0;}}
using namespace spatial;
Shared shared;
Settings applied;
bool isPaused=false,isRunning=true,pauseRequested=false,lockout=false;
bool muteTimeout=false,pauseTimeout=false,resumeTimeout=false,lockoutFail=false,corrupt=false;
unsigned erases=0,programs=0,pauses=0,resumes=0,workerCalls=0;
uint32_t now=0;
std::vector<uint8_t> reply;
uint32_t time_us_32(){now+=1000;return now;}
uint32_t tud_midi_stream_write(uint8_t,const uint8_t* p,size_t n){reply.insert(reply.end(),p,p+n);return n;}
bool multicore_lockout_start_timeout_us(uint64_t){assert(isPaused&&!isRunning);lockout=!lockoutFail;return lockout;}
void multicore_lockout_end_blocking(){assert(lockout);lockout=false;}
void flash_range_erase(uint32_t offset,size_t n){assert(isPaused&&!isRunning&&lockout&&offset==0&&n==4096);++erases;std::memset(mock_flash,255,4096);}
void flash_range_program(uint32_t offset,const uint8_t* data,size_t n){assert(isPaused&&!isRunning&&lockout&&offset==0&&n==256);++programs;std::memcpy(mock_flash,data,n);if(corrupt)mock_flash[0]^=1;}
void Worker(){
    ++workerCalls;
    shared.Consume(applied);
    if(shared.snapshot_request==1){shared.snapshot=applied;shared.snapshot_request=2;}
    if(shared.save==1&&!muteTimeout)shared.save=2;
    if(pauseRequested&&!pauseTimeout){assert(shared.save==2);isPaused=true;isRunning=false;}
    if(!pauseRequested&&!resumeTimeout){isPaused=false;isRunning=true;}
}
void Pause(){assert(shared.save==2);++pauses;pauseRequested=true;}
bool Paused(){return isPaused;}
void Resume(){++resumes;pauseRequested=false;}
bool Running(){return isRunning;}
int main(){
    std::memset(mock_flash,255,sizeof(mock_flash));Storage storage;storage.valid_=true;storage.offset_=0;
    UsbEditor usb(shared,storage,applied,Worker,Pause,Paused,Resume,Running);
    auto save=[&](uint8_t status){
        reply.clear();for(uint8_t b:{uint8_t(240),uint8_t(125),uint8_t(83),uint8_t(68),kProtocolVersion,uint8_t(3),uint8_t(1),uint8_t(247)})usb.Byte(b);
        while(!usb.tx_.Empty())usb.Pump();if(reply.size()!=10||reply[8]!=status){std::fprintf(stderr,"save mismatch erases=%u expected=%u bytes=%zu replystatus=%u\n",erases,status,reply.size(),reply.size()>8?reply[8]:255);}assert(reply.size()==10&&reply[5]==66&&reply[7]==3&&reply[8]==status);assert(shared.save==0&&!lockout&&!pauseRequested);
    };
    for(int i=0;i<1000;++i){applied.orbits_height=(i*31)%4097;applied.orbits_tilt=i%2049;save(0);assert(isRunning&&!isPaused&&!shared.save_fault);auto loaded=storage.Load();assert(loaded.orbits_height==applied.orbits_height&&loaded.orbits_tilt==applied.orbits_tilt);}
    assert(erases==1000&&programs==1000&&pauses==1000&&resumes==1000);
    auto writes=erases,requests=pauses;
    for(int i=0;i<100;++i)save(0);
    assert(erases==writes&&pauses==requests&&isRunning);
    auto fault=[&](bool& failure){
        failure=true;applied.orbits_height=(applied.orbits_height+7)%4097;save(5);assert(shared.save_fault);failure=false;Worker();assert(isRunning&&!isPaused);shared.save_fault=0;
    };
    fault(muteTimeout);fault(pauseTimeout);fault(lockoutFail);fault(corrupt);
    // Force sampler-resume timeout after a successful write; report failure, not success.
    fault(resumeTimeout);
    applied.orbits_height=731;save(0);assert(isRunning&&!shared.save_fault&&storage.Load().orbits_height==731);
    // Real parser + SPSC settings consumption: init -> height/slope Apply -> Read.
    auto command=[&](uint8_t cmd,const uint8_t* data,size_t n){
        reply.clear();for(uint8_t b:{uint8_t(240),uint8_t(125),uint8_t(83),uint8_t(68),kProtocolVersion,cmd,uint8_t(1)})usb.Byte(b);
        for(size_t i=0;i<n;++i)usb.Byte(data[i]);usb.Byte(247);Worker();usb.FinishApply();while(!usb.tx_.Empty())usb.Pump();
    };
    Settings desired;uint8_t payload[43];uint32_t mode;
    for(int i=0;i<20;++i){
        Settings init;auto n=EncodeSettings(init,0,payload);command(2,payload,n);assert(applied.orbits_height==2048&&applied.orbits_tilt==0);
        desired.orbits_height=3072;desired.orbits_tilt=2048;desired.orbits_bearing=1024;
        n=EncodeSettings(desired,0,payload);command(2,payload,n);assert(applied.orbits_height==3072&&applied.orbits_tilt==2048);
        command(1,nullptr,0);Settings snapshot;assert(reply.size()==36&&DecodeSettings(reply.data()+7,reply.size()-8,snapshot,mode)&&mode==0&&snapshot.orbits_height==3072&&snapshot.orbits_bearing==1024&&snapshot.orbits_tilt==2048);
    }
    assert(workerCalls>1000);
    std::puts("PASS: actual save parser/storage, 1000 changed saves +100 unchanged no-write saves, mute/pause/lockout/verify/resume failures, request cleanup, failure status and successful retry; flash only while sampler paused; real init/Apply/Read handoff");
}
