// SPDX-License-Identifier: MPL-2.0
#include "ComputerCard.h"
#include "runtime.hpp"
#include "worker.hpp"
#include "bank.hpp"
#include "hardware/timer.h"
#ifdef PICO_ON_DEVICE
#include "pico/multicore.h"
#include "pico/stdlib.h"
#if PEDAL_TELEMETRY
#include "pico/bootrom.h"
#include <cstdio>
#endif
#endif

class PedalLab : public ComputerCard {
    spin::Audio audio;
    SpinWorker worker{audio};
    uint16_t pots[3]={256,256,256};
    inline static PedalLab* instance=nullptr;
    unsigned selected=0, pending=0;
    unsigned control_counter=0, down_ticks=0;
    int32_t saved_main=2048, clock_hz=32768, clock_target=32768;
    int32_t smooth[3]={2048*256,2048*256,2048*256};
    bool was_down=false;
    enum class Transition {Playing, FadeOut, Clear, Warmup, FadeIn};
    Transition transition=Transition::Warmup;
    unsigned fade=0, clear_pos=0, warmup_count=0,clip_hold=0, overload_hold=0;
public:
    // Visible through SWD; no serial or USB work in the audio interrupt.
    volatile uint32_t max_callback_us=0, deadline_misses=0;
    volatile uint32_t actual_clock_hz=8000,clock_ceiling_hz=8000,worker_misses=0;
    volatile uint32_t sample_count=0,input_peak=0,output_peak=0;
    bool diagnostic_bypass=false,diagnostic_mute=false;
#if defined(PICO_ON_DEVICE) && PEDAL_TELEMETRY
    std::atomic<uint32_t> capture_state{0};unsigned capture_pos=0;
    int16_t capture[512][4]{};uint32_t capture_phase=0,capture_rate=0;
#endif
    PedalLab() {EnableNormalisationProbe();}
    void StartWorker(){
#ifdef PICO_ON_DEVICE
        instance=this;
        multicore_launch_core1([](){for(;;)if(!instance->worker.step(time_us_32))__wfe();});
#endif
    }
#if defined(PICO_ON_DEVICE) && PEDAL_TELEMETRY
    void MainLoop()override{
        static uint32_t last=0,last_count=0;int c=getchar_timeout_us(0);
        if(c=='b')reset_usb_boot(0,0);
        if(c=='d'){diagnostic_bypass=true;diagnostic_mute=false;}if(c=='e'){diagnostic_bypass=false;diagnostic_mute=false;}
        if(c=='m')diagnostic_mute=true;
        if(c>='1'&&c<='6')request(unsigned(c-'1'));
        if(c=='c')capture_state.store(1,std::memory_order_release);
        if(capture_state.load(std::memory_order_acquire)==3){
            printf("{\"capture_rate\":%lu,\"capture_phase\":%lu,\"capture\":[",(unsigned long)capture_rate,(unsigned long)capture_phase);
            for(unsigned i=0;i<512;++i)printf("%s[%d,%d,%d,%d]",i?",":"",capture[i][0],capture[i][1],capture[i][2],capture[i][3]);
            printf("]}\n");capture_state.store(0,std::memory_order_release);
        }
        uint32_t now=time_us_32();if(now-last<1000000&&c!='s')return;
        uint32_t n=sample_count;
        printf("{\"firmware\":\"spin-fv1-dual-core\",\"cpu_hz\":%lu,\"samples\":%lu,\"samples_since_report\":%lu,\"interval_us\":%lu,\"callback_max_us\":%lu,\"full_irq_max_us\":%lu,\"irq_gap_max_us\":%lu,\"adc_overflow_seen\":%lu,\"late_callbacks\":%lu,\"worker_max_us\":%lu,\"resample_max_us\":%lu,\"virtual_max_us\":%lu,\"queue_misses\":%lu,\"clock_hz\":%lu,\"clock_ceiling_hz\":%lu,\"input_peak\":%lu,\"output_peak\":%lu,\"slot\":%u,\"bypass\":%u}\n",
          (unsigned long)clock_get_hz(clk_sys),(unsigned long)n,(unsigned long)(n-last_count),(unsigned long)(now-last),(unsigned long)max_callback_us,(unsigned long)FullAudioIRQMaxUs,(unsigned long)AudioIRQGapMaxUs,(unsigned long)ADCFifoOverflowSeen,(unsigned long)deadline_misses,(unsigned long)worker.max_work_us.load(),(unsigned long)worker.resample_max_us.load(),(unsigned long)worker.virtual_max_us.load(),(unsigned long)worker_misses,(unsigned long)actual_clock_hz,(unsigned long)clock_ceiling_hz,(unsigned long)input_peak,(unsigned long)output_peak,selected+1,unsigned(diagnostic_bypass));
        last=now;last_count=n;
    }
#endif
    void request(unsigned next) {
        if(transition!=Transition::Playing)return;
        pending=next%spin::program_count;transition=Transition::FadeOut;fade=96;
    }
    void controls() {
        const bool down=SwitchVal()==Switch::Down;
        if(down) {
            if(!was_down)down_ticks=0;
            ++down_ticks;
            if(down_ticks>=400) {
                // Hold Z down: Main is temporarily the virtual clock control.
                // Piecewise linear mapping makes the exact stock clock reachable.
                int k=KnobVal(Knob::Main);
                clock_target=k<=2048?8000+(k*24768)/2048:32768+((k-2048)*7232)/2047;
            }
        } else {
            if(was_down&&down_ticks<400)request(selected+1);
            saved_main=KnobVal(Knob::Main);
        }
        was_down=down;
        // CV1 -> Main/POT0, CV2 -> X/POT1, Pulse1 gate -> Y/POT2 (+half range).
        int target[3]={saved_main,KnobVal(Knob::X),KnobVal(Knob::Y)};
        if(Connected(Input::CV1))target[0]+=CVIn1()*2;
        if(Connected(Input::CV2)&&SwitchVal()!=Switch::Up)target[1]+=CVIn2()*2;
        if(Connected(Input::Pulse1)&&PulseIn1())target[2]+=2048;
        for(int i=0;i<3;++i) {
            smooth[i]+=(std::clamp(target[i],0,4095)*256-smooth[i])>>3;
            pots[i]=uint16_t(std::clamp<int32_t>((smooth[i]+1024)>>11,0,511));
        }
        clock_hz+=(clock_target-clock_hz)/8;
        if(std::abs(clock_target-clock_hz)<8)clock_hz=clock_target;
        int32_t modulated_clock=clock_hz;
        if(SwitchVal()==Switch::Up&&Connected(Input::CV2))modulated_clock+=CVIn2()*8;
        // Both timing classes are measured on core 1. Reserve 15% of that
        // core and count resampling at 48 kHz even when the FV-1 runs slower.
        const uint32_t base=worker.resample_max_us.load(std::memory_order_acquire);
        const uint32_t tick=worker.virtual_max_us.load(std::memory_order_acquire);
        const uint32_t available=base<17?850000-48000*base:0;
        clock_ceiling_hz=(!tick||!base)?8000:(tick>base?std::clamp<uint32_t>(available/(tick-base),8000,40000):40000);
        actual_clock_hz=std::clamp<int32_t>(modulated_clock,8000,int32_t(clock_ceiling_hz));
        // Rate changes and FIR coefficient calculation run exclusively on core 1.
        for(unsigned i=0;i<6;++i)LedOn(i,i==selected);
        if(clip_hold) {--clip_hold;LedOn(5);}
        if(overload_hold) {--overload_hold;LedOn(4);LedOn(5);}
        if(modulated_clock>int32_t(clock_ceiling_hz))LedOn(4);
        CVOut1(int16_t(pots[0]*8-2048));
        CVOut2(int16_t(pots[1]*8-2048));
    }
    void ProcessSample() override {
        uint32_t start=time_us_32();
        ++sample_count;
        if(++control_counter==48){control_counter=0;controls();}
        // The worker owns delay RAM. Wait silently for its reset acknowledgement;
        // the ISR never clears 64KiB or waits for another core.
        if(transition==Transition::Clear) {
            ++clear_pos;
            AudioOut1(0);AudioOut2(0);
            if(clear_pos>=256&&worker.reset_ready()) {
                worker.restart_stream();selected=pending;actual_clock_hz=8000;clock_ceiling_hz=8000;
                fade=0;warmup_count=0;transition=Transition::Warmup;
            }
        } else {
            const int32_t l=Connected(Input::Audio1)?AudioIn1():0;
            const int32_t r=Connected(Input::Audio2)?AudioIn2():l;
#if defined(PICO_ON_DEVICE) && PEDAL_TELEMETRY
            if(capture_state.load(std::memory_order_relaxed)==1){capture_pos=0;capture_phase=0;capture_rate=actual_clock_hz;capture_state.store(2,std::memory_order_relaxed);}
#endif
            input_peak=std::max<uint32_t>(uint32_t(input_peak),std::max(std::abs(l),std::abs(r)));
            // 6dB internal headroom, recovered at output for nominal unity gain.
            spin::Stereo in{l*2048,r*2048},out;
            out=worker.submit(in,pots,spin::programs[selected],actual_clock_hz);
            unsigned gain=96;
            if(transition==Transition::Warmup){
                gain=0;
                if(++warmup_count>=960){transition=Transition::FadeIn;fade=0;}
            }else if(transition==Transition::FadeOut) {
                gain=fade;
                if(--fade==0){transition=Transition::Clear;clear_pos=0;worker.begin_reset();}
            } else if(transition==Transition::FadeIn) {
                gain=fade;
                if(++fade==96)transition=Transition::Playing;
            }
            int32_t ol=gain==96?out.l/2048:int32_t((out.l*int32_t(gain))/(96*2048));
            int32_t or_=gain==96?out.r/2048:int32_t((out.r*int32_t(gain))/(96*2048));
            if(diagnostic_bypass){ol=l;or_=r;}
            if(diagnostic_mute){ol=0;or_=0;}
            output_peak=std::max<uint32_t>(uint32_t(output_peak),std::max(std::abs(ol),std::abs(or_)));
            if(ol>2047||ol<-2048||or_>2047||or_<-2048)clip_hold=100;
            AudioOut1(int16_t(std::clamp<int32_t>(ol,-2048,2047)));
            AudioOut2(int16_t(std::clamp<int32_t>(or_,-2048,2047)));
#if defined(PICO_ON_DEVICE) && PEDAL_TELEMETRY
            if(capture_state.load(std::memory_order_relaxed)==2){
                capture[capture_pos][0]=l;capture[capture_pos][1]=r;
                capture[capture_pos][2]=std::clamp<int32_t>(ol,-2048,2047);capture[capture_pos][3]=std::clamp<int32_t>(or_,-2048,2047);
                if(++capture_pos==512)capture_state.store(3,std::memory_order_release);
            }
#endif
        }
#ifdef PICO_ON_DEVICE
        __sev();
#else
        worker.step(time_us_32);
#endif
        if(worker.misses!=worker_misses){worker_misses=worker.misses;if(transition!=Transition::Warmup)overload_hold=1000;}
        uint32_t elapsed=time_us_32()-start;
        if(elapsed>max_callback_us)max_callback_us=elapsed;
        if(elapsed>=20){++deadline_misses;overload_hold=1000;}
    }
};
int main() {set_sys_clock_khz(133000,true);
#if defined(PICO_ON_DEVICE) && PEDAL_TELEMETRY
stdio_init_all();
#endif
static PedalLab card;
#ifdef PICO_ON_DEVICE
irq_set_priority(DMA_IRQ_0,0);
#endif
card.StartWorker();card.Run();return 0;}
