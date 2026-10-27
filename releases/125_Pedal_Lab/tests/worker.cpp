// SPDX-License-Identifier: MPL-2.0
#include "worker.hpp"
#include <memory>
#include <iostream>
#include <stdexcept>
#include <thread>
static void require(bool b,const char*s){if(!b)throw std::runtime_error(s);}
int main()try{
 for(int v=-32768;v<=32767;++v){
  for(int c:{-8388608,-8388607,-1000001,-32769,-1,0,1,32767,32768,1000001,8388607})
   require(spin::mul_delay_q23(v*256,c)==spin::sat24(int32_t((int64_t(v)*c)>>15)),"delay Q23 multiply");
  for(int c:{-1024,-1023,-513,-512,-1,0,1,511,512,1023})
   require(spin::mul_delay_q9(v*256,c)==spin::sat24(int32_t((int64_t(v)*c)>>1)),"delay Q9 multiply");
 }
 auto a=std::make_unique<spin::Audio>();auto reference=std::make_unique<spin::Audio>();SpinWorker w(*a);spin::Stereo expected[SpinWorker::latency]{};uint16_t pots[]={256,256,256};
 auto unity=[](spin::Machine&s){s.regs[0x16]=s.regs[0x14];s.regs[0x17]=s.regs[0x15];};
 for(unsigned i=0;i<10000;++i){
  auto input=spin::Stereo{int(i)*128,-int(i)*128};unsigned slot=i%w.latency;
  unsigned rate=8000+(i*37)%32001;
  pots[0]=i%512;pots[1]=(i*7)%512;pots[2]=(i*13)%512;
  auto out=w.submit(input,pots,unity,rate);
  require(out.l==expected[slot].l&&out.r==expected[slot].r,"full DSP pipeline latency/order");
  reference->set_rate(rate);for(unsigned p=0;p<3;++p)reference->pots[p]=pots[p];
  expected[slot]=reference->process(input,unity);w.step([](){return 0u;});
 }
 require(w.misses==0,"unexpected pipeline underrun");
 w.begin_reset();require(!w.reset_ready(),"reset completed before worker");w.step([](){return 0u;});require(w.reset_ready(),"reset acknowledgement");w.restart_stream();
 for(unsigned i=0;i<w.latency;++i){auto out=w.submit({100,100},pots,unity);require(out.l==0&&out.r==0,"startup silence");w.step([](){return 0u;});}
 const auto old=w.misses;for(int i=0;i<40;++i)w.submit({0,0},pots,unity);require(w.misses>old,"underrun must be observable and nonblocking");
 SpinQueue<unsigned,4> q;for(unsigned i=0;i<4;++i)require(q.push(i),"queue accepts capacity");require(!q.push(4),"queue capacity");for(unsigned i=0;i<4;++i){unsigned v;require(q.pop(v)&&v==i,"queue ordering");}unsigned v;require(!q.pop(v),"empty queue");
 SpinQueue<unsigned,16> threaded;std::atomic<bool> failed{false};
 std::thread consumer([&](){for(unsigned expected=0;expected<100000;++expected){unsigned value;while(!threaded.pop(value))std::this_thread::yield();if(value!=expected)failed.store(true);}});
 for(unsigned i=0;i<100000;++i)while(!threaded.push(i))std::this_thread::yield();consumer.join();require(!failed.load(),"concurrent SPSC order");
 std::cout<<"PASS fixed-latency worker order, reset handshake, bounded underrun, capacity and 100000 concurrent queue transfers\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
