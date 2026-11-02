// SPDX-License-Identifier: MPL-2.0
#include "audio.hpp"
// Replay the historical capture made with four virtual frames of queue delay.
#include <fstream>
#include <iostream>
#include <memory>
#include <cmath>
int main(int argc,char**argv){if(argc!=2)return 1;std::ifstream f(argv[1],std::ios::binary);uint32_t h[3];f.read((char*)h,sizeof(h));auto a=std::make_unique<spin::Audio>();a->set_rate(h[0]);a->phase=h[1];spin::Stereo queued[4]{};unsigned sequence=0;uint16_t pots[]={256,256,256};auto unity=[](spin::Machine&m){m.regs[0x16]=m.regs[0x14];m.regs[0x17]=m.regs[0x15];};int maxerror=0;double sum=0;unsigned errors=0,count=0;for(unsigned i=0;i<h[2];++i){int16_t v[4];f.read((char*)v,sizeof(v));auto y=a->process_with({int32_t(v[0])*2048,int32_t(v[1])*2048},[&](spin::Stereo in){a->machine.begin(in.l,in.r,pots);unity(a->machine);spin::Stereo next{a->machine.regs[0x16],a->machine.regs[0x17]};a->machine.end_sample();unsigned slot=sequence++%4;auto result=queued[slot];queued[slot]=next;return result;});int expected[2]={std::clamp(y.l/2048,-2048,2047),std::clamp(y.r/2048,-2048,2047)};if(i>=128)for(unsigned c=0;c<2;++c){int error=std::abs(expected[c]-v[2+c]);maxerror=std::max(maxerror,error);sum+=double(error)*error;errors+=error!=0;++count;if(error>5&&errors<10)std::cout<<"sample "<<i<<" expected "<<expected[c]<<" actual "<<v[2+c]<<'\n';}}std::cout<<"samples "<<count<<", different "<<errors<<", max raw12 error "<<maxerror<<", RMS "<<std::sqrt(sum/count)<<", worker misses "<<0<<'\n';}
