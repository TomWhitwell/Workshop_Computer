#pragma once
#include <cstdint>
class ComputerCard {
public:
    enum Knob{Main,X,Y};enum Switch{Down,Middle,Up};enum Input{Audio1,Audio2,CV1,CV2,Pulse1,Pulse2};
    int32_t knobs[3]={2048,2048,2048};int16_t cv[2]={0,0},in[2]={0,0},out[2]={0,0},cvout[2]={0,0};
    bool plugged[6]={false,false,false,false,false,false},leds[6]={};Switch sw=Middle;bool gate=false;
    virtual void ProcessSample()=0;void Run(){};void EnableNormalisationProbe(){}
    int32_t KnobVal(Knob k){return knobs[k];}Switch SwitchVal(){return sw;}
    bool Connected(Input i){return plugged[i];}int16_t CVIn1(){return cv[0];}int16_t CVIn2(){return cv[1];}
    bool PulseIn1(){return gate;}int16_t AudioIn1(){return in[0];}int16_t AudioIn2(){return in[1];}
    void AudioOut1(int16_t v){out[0]=v;}void AudioOut2(int16_t v){out[1]=v;}
    void CVOut1(int16_t v){cvout[0]=v;}void CVOut2(int16_t v){cvout[1]=v;}
    void LedOn(unsigned i,bool v=true){leds[i]=v;}
};
inline bool set_sys_clock_khz(unsigned,bool){return true;}
