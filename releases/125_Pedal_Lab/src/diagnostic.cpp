// SPDX-License-Identifier: MPL-2.0
#include "ComputerCard.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "hardware/timer.h"
#include <atomic>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
class Diagnostic:public ComputerCard{
public:
 std::atomic<uint32_t> samples{0},max_us{0},late{0},input_peak{0},output_peak{0},jacks{0};
 std::atomic<int32_t> left{0},right{0},knob0{0},knob1{0},knob2{0},switch_pos{0};
 uint32_t count=0;int32_t peak_in=0,peak_out=0;
 Diagnostic(){EnableNormalisationProbe();}
 void ProcessSample()override{
  uint32_t start=time_us_32();int32_t l=AudioIn1(),r=Connected(Audio2)?AudioIn2():l;
  // Deliberately no FV-1, resampler, scaling, allocation or worker queue.
  AudioOut1(int16_t(l));AudioOut2(int16_t(r));
  peak_in=std::max(peak_in,std::max(std::abs(l),std::abs(r)));peak_out=peak_in;
  if(++count%480==0){
   samples.store(count,std::memory_order_relaxed);input_peak.store(peak_in,std::memory_order_relaxed);output_peak.store(peak_out,std::memory_order_relaxed);
   left.store(l,std::memory_order_relaxed);right.store(r,std::memory_order_relaxed);
   knob0.store(KnobVal(Main),std::memory_order_relaxed);knob1.store(KnobVal(X),std::memory_order_relaxed);knob2.store(KnobVal(Y),std::memory_order_relaxed);switch_pos.store(SwitchVal(),std::memory_order_relaxed);
   uint32_t mask=0;for(unsigned i=0;i<6;++i)if(Connected(Input(i)))mask|=1u<<i;jacks.store(mask,std::memory_order_relaxed);
   LedOn(0,true);LedOn(1,peak_in>20);LedOn(2,count%48000<24000);LedOn(3,Connected(Audio1));LedOn(4,Connected(Audio2));LedOn(5,peak_in>2000);
  }
  uint32_t elapsed=time_us_32()-start;if(elapsed>max_us.load(std::memory_order_relaxed))max_us.store(elapsed,std::memory_order_relaxed);
  if(elapsed>=20)late.store(late.load(std::memory_order_relaxed)+1,std::memory_order_relaxed);
 }
};
static Diagnostic*card;
static void usb_loop(){
 uint32_t last=0,last_count=0;
 for(;;){
  int c=getchar_timeout_us(0);if(c=='b')reset_usb_boot(0,0);
  uint32_t now=time_us_32();if(now-last>=1000000||c=='s'){
   uint32_t n=card->samples.load();
   printf("{\"firmware\":\"spin-diagnostic-stereo-through\",\"cpu_hz\":%lu,\"samples\":%lu,\"samples_since_report\":%lu,\"interval_us\":%lu,\"callback_max_us\":%lu,\"late_callbacks\":%lu,\"input_peak\":%lu,\"output_peak\":%lu,\"input_l\":%ld,\"input_r\":%ld,\"jacks\":%lu,\"main\":%ld,\"x\":%ld,\"y\":%ld,\"switch\":%ld}\n",
    (unsigned long)clock_get_hz(clk_sys),(unsigned long)n,(unsigned long)(n-last_count),(unsigned long)(now-last),(unsigned long)card->max_us.load(),(unsigned long)card->late.load(),(unsigned long)card->input_peak.load(),(unsigned long)card->output_peak.load(),(long)card->left.load(),(long)card->right.load(),(unsigned long)card->jacks.load(),(long)card->knob0.load(),(long)card->knob1.load(),(long)card->knob2.load(),(long)card->switch_pos.load());
   last=now;last_count=n;
  }sleep_ms(2);
 }
}
int main(){set_sys_clock_khz(133000,true);stdio_init_all();static Diagnostic c;card=&c;multicore_launch_core1(usb_loop);c.Run();}
