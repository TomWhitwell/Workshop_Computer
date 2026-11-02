// SPDX-License-Identifier: MPL-2.0
// Streaming Q23 stereo int32 PCM. The Python CLI supplies WAV I/O and options.
#include "audio.hpp"
#include "bank.hpp"
#include <iostream>
#include <memory>
#include <cstdlib>
int main(int argc,char **argv) {
    if(argc!=7){std::cerr<<"render slot rate pot0 pot1 pot2 native|hardware\n";return 1;}
    int slot=std::atoi(argv[1]),rate=std::atoi(argv[2]);
    if(slot<0||unsigned(slot)>=spin::program_count||rate<8000||rate>40000)return 2;
    auto a=std::make_unique<spin::Audio>();a->set_rate(rate);
    for(int i=0;i<3;++i) {int v=std::atoi(argv[3+i]);if(v<0||v>511)return 2;a->pots[i]=uint16_t(v);}
    bool native=std::string(argv[6])=="native";
    spin::Stereo in,out;
    while(std::cin.read(reinterpret_cast<char*>(&in),sizeof(in))) {
        if(native) {
            a->machine.begin(in.l,in.r,a->pots);spin::programs[slot](a->machine);
            out={a->machine.regs[0x16],a->machine.regs[0x17]};a->machine.end_sample();
        } else out=a->process(in,spin::programs[slot]);
        std::cout.write(reinterpret_cast<const char*>(&out),sizeof(out));
    }
    return std::cin.eof()?0:3;
}
