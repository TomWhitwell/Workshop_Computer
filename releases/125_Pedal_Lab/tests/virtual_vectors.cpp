// SPDX-License-Identifier: MPL-2.0
#include "runtime.hpp"
#include "bank.hpp"
#include <fstream>
#include <iostream>
#include <memory>
int main(int argc,char**argv){
 if(argc!=2)return 1;std::ofstream out(argv[1],std::ios::binary);
 uint32_t h[]={uint32_t(sizeof(spin::Machine)),spin::program_count,24,uint32_t(sizeof(SpinFrameInput))};out.write((char*)h,sizeof(h));
 auto m=std::make_unique<spin::Machine>();uint32_t random=11;
 for(unsigned b=0;b<spin::program_count;++b){m->reset();for(auto&v:m->delay16){random=random*1664525+1013904223;v=int16_t(random>>16);}
  for(unsigned i=0;i<24;++i){SpinFrameInput in{{i%2?4192256:-4194304,int32_t(i*200000)-2000000},{uint16_t(i*21),uint16_t(511-i*20),256}};spin::Stereo result;
   out.write((char*)m.get(),sizeof(*m));out.write((char*)&in,sizeof(in));spin_virtual_frame(m.get(),&in,&result,spin::programs[b]);out.write((char*)m.get(),sizeof(*m));out.write((char*)&result,sizeof(result));
  }
 }
 std::cout<<spin::program_count*24<<" virtual-engine ARM vectors\n";return out?0:2;
}
