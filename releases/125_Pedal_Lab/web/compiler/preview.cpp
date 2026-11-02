// SPDX-License-Identifier: MPL-2.0
#include "audio.hpp"
static uint32_t program[128];
static unsigned length=128;
static spin::DecodedInstruction decoded[128];
static void interpret(spin::Machine&s){uint32_t pc=0;while(pc<length){bool skip=false;s.execute_instruction(decoded[pc],pc,skip);}}
static spin::Audio audio;
static int32_t input[48000*12*2],output[48000*12*2];
extern "C" {
int abs(int x){return x<0?-x:x;}
void* memset(void*p,int value,__SIZE_TYPE__ n){auto b=(volatile unsigned char*)p;while(n--)*b++=value;return p;}
void* memcpy(void*p,const void*q,__SIZE_TYPE__ n){auto b=(volatile unsigned char*)p;auto a=(const volatile unsigned char*)q;while(n--)*b++=*a++;return p;}
unsigned program_buffer(){return (unsigned)program;}
void program_length(unsigned n){length=n>128?128:n;for(unsigned i=0;i<length;++i)decoded[i]=spin::decode(program[i]);}
unsigned audio_state(){return (unsigned)&audio;}
unsigned audio_size(){return sizeof(audio);}
void process_frame(){auto y=audio.process({input[0],input[1]},interpret);output[0]=y.l;output[1]=y.r;}
unsigned input_buffer(){return (unsigned)input;}
unsigned output_buffer(){return (unsigned)output;}
void render(unsigned frames,int rate,unsigned p0,unsigned p1,unsigned p2){
 if(frames>48000*12)return;
 audio.reset();audio.set_rate(rate);audio.pots[0]=p0;audio.pots[1]=p1;audio.pots[2]=p2;
 for(unsigned i=0;i<frames;++i){auto y=audio.process({input[2*i],input[2*i+1]},interpret);output[2*i]=y.l;output[2*i+1]=y.r;}
}
}
