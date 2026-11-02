// SPDX-License-Identifier: MPL-2.0
#include "catalogue_bank.hpp"
#include <processor/processor.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
int main(int argc,char**argv){
    if(argc!=2)return 2;
    auto m=std::make_unique<spin::Machine>();
    for(unsigned b=0;b<spin::program_count;++b){
        std::ifstream f(std::string(argv[1])+"/program_"+std::to_string(b)+".bin",std::ios::binary);
        std::vector<uint8_t> image((std::istreambuf_iterator<char>(f)),{});
        fv1_config cfg{32768,FV1_DELAY_REFERENCE_16};auto*r=fv1_create(&cfg);
        if(!r||fv1_load_bytes(r,image.data(),image.size())!=FV1_OK)return 3;
        m->reset();uint32_t random=12345;unsigned mismatches=0,clips=0;int64_t peak_error=0,peak=0;double square=0;
        for(unsigned i=0;i<32768;++i){
            random=random*1664525u+1013904223u;
            int32_t l=i==0?1048576:i<8192?int32_t(random>>12)-524288:0;
            int32_t rr=i<8192?-l/2:0;
            uint16_t pots[3]={uint16_t((i/127)%512),uint16_t((i/53)%512),uint16_t((i/83)%512)};
            fv1_set_pots(r,float(pots[0])/511,float(pots[1])/511,float(pots[2])/511);
            m->begin(l,rr,pots);spin::programs[b](*m);m->end_sample();float ol,orr;
            if(fv1_process_sample(r,float(l)/8388608,float(rr)/8388608,&ol,&orr)!=FV1_OK)return 4;
            fv1_snapshot s{};fv1_get_snapshot(r,&s);bool differs=m->acc!=s.acc||m->pacc!=s.pacc||m->lr!=s.lr;
            for(unsigned j=0;j<64;++j)differs|=m->regs[j]!=s.regs[j];
            mismatches+=differs;
            for(unsigned j:{0x16u,0x17u}){
                int64_t v=m->regs[j],error=v-s.regs[j];peak_error=std::max<int64_t>(peak_error,std::abs(error));square+=double(error)*error;
                peak=std::max<int64_t>(peak,std::abs(v));clips+=v>4192256||v<-4194304;
            }
        }
        fv1_destroy(r);
        std::cout<<"{\"index\":"<<b<<",\"samples\":32768,\"state_mismatch_samples\":"<<mismatches<<",\"peak_output_error_q23\":"<<peak_error<<",\"rms_output_error_q23\":"<<std::sqrt(square/65536)<<",\"peak_output_q23\":"<<peak<<",\"output_clip_samples\":"<<clips<<"}"<<std::endl;
    }
}
