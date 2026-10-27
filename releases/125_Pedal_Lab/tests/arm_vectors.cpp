// SPDX-License-Identifier: MPL-2.0
#include "bank.hpp"
#include <fstream>
#include <memory>
#include <iostream>
int main(int argc,char **argv) {
    if(argc!=2)return 1;
    std::ofstream out(argv[1],std::ios::binary);
    uint32_t header[3]={uint32_t(sizeof(spin::Machine)),spin::program_count,16};
    out.write(reinterpret_cast<char*>(header),sizeof(header));
    auto s=std::make_unique<spin::Machine>();uint32_t random=123456;
    for(unsigned b=0;b<spin::program_count;++b) {
        s->reset();
        for(auto &v:s->delay16) {random=random*1664525u+1013904223;v=int16_t(random>>16);}
        for(int i=0;i<16;++i) {
            uint16_t pots[3]={uint16_t((i*33)%512),uint16_t(511-i*31),uint16_t(i*17)};
            s->begin(i%2?8388607:-8388608,(i*1234567)%8388608,pots);
            out.write(reinterpret_cast<char*>(s.get()),sizeof(*s));
            spin::programs[b](*s);
            out.write(reinterpret_cast<char*>(s.get()),sizeof(*s));
            s->end_sample();
        }
    }
    std::cout<<"ARM vectors: "<<spin::program_count*16<<", "<<sizeof(*s)<<" bytes/state\n";
    return out?0:2;
}
