#pragma once
#include <cstdint>
#include <vector>
struct ComputerCard {
  enum Knob {Main, X, Y}; enum Switch {Down, Middle, Up};
  enum Input {Audio1, Audio2, CV1, CV2, Pulse1, Pulse2};
  bool conn[6]={false,false,false,false,false,false};
  bool Connected(Input i){return conn[i];}
  int32_t knob[3]={2048,2048,2048}; Switch sw=Up;
  std::vector<int16_t> o1,o2; int cvmv=0; int note=0;
  virtual void ProcessSample()=0;
  int32_t KnobVal(Knob k){return knob[k];} Switch SwitchVal(){return sw;}
  void AudioOut1(int16_t v){o1.push_back(v);} void AudioOut2(int16_t v){o2.push_back(v);}
  void PulseOut1(bool){} void PulseOut2(bool){}
  bool CVOut1Millivolts(int32_t m){cvmv=m;return false;} void CVOut2MIDINote(uint8_t n){note=n;}
  void LedBrightness(int,int){} void LedOn(int,bool=true){}
  void Run(){}
  int16_t cv1=0,cv2=0; bool p1=false,p2=false;
  int16_t CVIn1(){return cv1;} int16_t CVIn2(){return cv2;}
  bool PulseIn1RisingEdge(){bool r=p1;p1=false;return r;} bool PulseIn2RisingEdge(){bool r=p2;p2=false;return r;}
  void EnableNormalisationProbe(){}
};
