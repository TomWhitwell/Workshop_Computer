// SPDX-License-Identifier: MPL-2.0
#include "runtime.hpp"
#include "bank.hpp"
#include <fstream>
#include <iostream>
#include <memory>
int main(int argc,char**argv){
    if(argc<2)return 1;std::ofstream out(argv[1],std::ios::binary);bool resample=argc>2;
    uint32_t header[]={uint32_t(sizeof(spin::Audio)),spin::program_count,24,8};out.write((char*)header,sizeof(header));
    auto a=std::make_unique<spin::Audio>();uint32_t random=9943;
    for(unsigned b=0;b<spin::program_count;++b){
        a->reset();for(auto&v:a->machine.delay16){random=random*1664525+1013904223;v=int16_t(random>>16);}
        for(unsigned i=0;i<24;++i){
            a->set_rate(i<8?8000:i<16?32768:40000);
            // Exercise both no-virtual-tick and virtual-tick callbacks.
            a->phase=(i%3)*16000;
            a->pots[0]=uint16_t(i*21);a->pots[1]=uint16_t(511-i*20);a->pots[2]=256;
            spin::Stereo input{i%2?4192256:-4194304,int32_t(i*100000)-1000000},output;
            out.write((char*)a.get(),sizeof(*a));out.write((char*)&input,sizeof(input));
            if(resample)spin_resample_frame(a.get(),&input,&output,&input);
            else spin_process_frame(a.get(),&input,&output,spin::programs[b]);
            out.write((char*)a.get(),sizeof(*a));out.write((char*)&output,sizeof(output));
        }
    }
    std::cout<<spin::program_count*24<<" complete audio-path ARM vectors; "<<sizeof(*a)<<" bytes/state\n";return out?0:2;
}
