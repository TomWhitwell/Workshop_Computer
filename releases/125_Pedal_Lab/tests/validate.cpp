// SPDX-License-Identifier: MPL-2.0
#include "bank.hpp"
#include "audio.hpp"
#include <processor/processor.h>
#include <processor/reference_model.hpp>
#include <fstream>
#include <iostream>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <memory>
static void require(bool condition,const char *message) {if(!condition)throw std::runtime_error(message);}
static void compare_snapshot(const spin::Machine& s,const fv1_snapshot &snap) {
    require(s.acc==snap.acc&&s.pacc==snap.pacc&&s.lr==snap.lr,"ACC/PACC/LR mismatch");
    for(int j=0;j<64;++j)require(s.regs[j]==snap.regs[j],"register mismatch");
    require(s.delay_ptr==snap.delay_pointer&&s.first_run==bool(snap.first_run),"time state mismatch");
    for(int j=0;j<2;++j)require(s.sine_wide(j,false)==snap.sin_lfo[j]&&s.sine_wide(j,true)==snap.cos_lfo[j]&&s.ramp[j].value(s.regs[4+j*2+1],false)==snap.ramp_lfo[j],"LFO mismatch");
}
static void compare(const spin::Machine& s,fv1_engine *r) {fv1_snapshot snap{};fv1_get_snapshot(r,&snap);compare_snapshot(s,snap);}
int main(int argc,char **argv) try {
    require(argc==2,"supply generated directory");
    auto m=std::make_unique<spin::Machine>();
    uint32_t random=12345;
    uint64_t comparisons=0;
    for(unsigned bank=0;bank<spin::program_count;++bank) {
        std::ifstream f(std::string(argv[1])+"/program_"+std::to_string(bank)+".bin",std::ios::binary);
        std::vector<uint8_t> image((std::istreambuf_iterator<char>(f)),{});
        fv1_config cfg{32768,FV1_DELAY_REFERENCE_16};
        auto *ref=fv1_create(&cfg);require(ref,"reference allocation");
        fv1::ReferenceModel golden(cfg);require(golden.load_bytes(image.data(),image.size())==FV1_OK,"golden load");
        require(fv1_load_bytes(ref,image.data(),image.size())==FV1_OK,"load reference image");
        m->reset();
        // 2 seconds/program: impulse, noise, silence/tail, control ramps and endpoints.
        for(unsigned i=0;i<65536;++i) {
            random=random*1664525u+1013904223u;
            int32_t l=i==0?2097152:(i<16384?int32_t(random>>9)-4194304:0);
            random=random*1664525u+1013904223u;
            int32_t r=i<16384?int32_t(random>>9)-4194304:0;
            uint16_t pots[3]={uint16_t((i/127)%512),uint16_t((i/53)%512),uint16_t((i/83)%512)};
            fv1_set_pots(ref,float(pots[0])/511,float(pots[1])/511,float(pots[2])/511);
            golden.set_pots(float(pots[0])/511,float(pots[1])/511,float(pots[2])/511);
            m->begin(l,r,pots);spin::programs[bank](*m);m->end_sample();
            float ol,or_;require(fv1_process_sample(ref,float(l)/8388608,float(r)/8388608,&ol,&or_)==FV1_OK,"reference process");
            compare(*m,ref);
            float gl,gr;golden.process_sample(float(l)/8388608,float(r)/8388608,&gl,&gr);
            fv1_snapshot gs{};golden.get_snapshot(&gs);compare_snapshot(*m,gs);
            ++comparisons;
            if(i%4096==0)for(unsigned j=0;j<32768;++j){int32_t v;fv1_read_delay_word(ref,j,&v);require(v==int32_t(m->delay16[j])*256,"delay mismatch");}
        }
        fv1_destroy(ref);std::cout<<"PASS exact state/audio: "<<spin::program_names[bank]<<"\n";
    }
    // Validate native-width multiply decomposition against 64-bit arithmetic.
    for(unsigned i=0;i<1000000;++i) {
        random=random*1664525u+1013904223u;int32_t x=int32_t(random&0xffffff)-8388608;
        random=random*1664525u+1013904223u;int32_t y=int32_t(random&0xffffff)-8388608;
        require(spin::mul_q23(x,y)==spin::sat24(int32_t((int64_t(x)*y)>>23)),"Q23 multiply mismatch");
        int32_t c=int16_t(random);
        require(spin::mul_q(x,c,14)==spin::sat24(int32_t((int64_t(x)*c)>>14)),"Q14 multiply mismatch");
        c=(c>>5);
        require(spin::mul_q(x,c,9)==spin::sat24(int32_t((int64_t(x)*c)>>9)),"Q9 multiply mismatch");
    }
    std::cout<<"PASS 3,000,000 multiply comparisons against 64-bit arithmetic\n";
    // Every legal opcode at full-scale boundaries and randomized states.
    // This checks constants/overflow independently of the demo algorithms.
    fv1_config cfg{32768,FV1_DELAY_REFERENCE_16}; auto *ref=fv1_create(&cfg);
    int max_log_error=0,max_exp_error=0;
    for(unsigned op=0;op<=20;++op)for(unsigned test=0;test<400;++test) {
        uint32_t words[128];std::fill(std::begin(words),std::end(words),0x11);
        random=random*1664525u+1013904223u;
        // Load ADCs, establish a distinct PACC and LR, then the instruction under test.
        words[0]=0x40000004u|(0x14<<5); // RDAX ADCL,1
        words[1]=0x40000004u|(0x15<<5); // RDAX ADCR,1
        words[2]=uint32_t(300<<21)|(23<<5); // RDA
        words[3]=(random&~31u)|op;
        // Use valid subfields only. Other fields may be arbitrary/reserved bits ignored.
        if(op==20)words[3]=(words[3]&~(3u<<30))|((test%3==0?0u:test%3==1?2u:3u)<<30);
        words[4]=0x40000006u|(0x16<<5); // WRAX DACL,1
        fv1_load_words(ref,words,128);m->reset();
        int32_t l=test%4==0?-8388608:test%4==1?8388607:int32_t(random&0xffffff)-8388608;
        int32_t r=test%2?-1000000:1000000;uint16_t p[3]={0,511,256};
        fv1_set_pots(ref,0,1,256.0f/511);m->begin(l,r,p);
        for(uint32_t pc=0;pc<128;) {bool skip;auto d=spin::decode(words[pc]);m->execute_instruction(d,pc,skip);}
        m->end_sample();float ol,orr;fv1_process_sample(ref,float(l)/8388608,float(r)/8388608,&ol,&orr);
        fv1_snapshot snap{};fv1_get_snapshot(ref,&snap);
        int error=std::abs(m->regs[0x16]-snap.regs[0x16]);
        if(op==11)max_log_error=std::max(max_log_error,error);
        else if(op==12)max_exp_error=std::max(max_exp_error,error);
        else compare(*m,ref);
    }
    fv1_destroy(ref);
    require(max_log_error<=4&&max_exp_error<=4,"LOG/EXP LUT error >4 Q23 LSB");
    std::cout<<"PASS opcode boundary/random cases; LOG max "<<max_log_error<<", EXP max "<<max_exp_error<<" Q23 LSB\n";
    auto a=std::make_unique<spin::Audio>();
    for(int rate:{8000,16384,32768,40000}) {
        a->reset();a->set_rate(rate);
        auto unity=[](spin::Machine &s){s.regs[0x16]=s.regs[0x14];s.regs[0x17]=s.regs[0x15];};
        spin::Stereo out;
        for(int i=0;i<48000;++i)out=a->process({1048576,-1048576},unity);
        require(a->ticks==unsigned(rate),"virtual clock tick count mismatch");
        require(std::abs(out.l-1048576)<=256&&std::abs(out.r+1048576)<=256,"resampler DC gain mismatch");
    }
    std::cout<<"PASS exact clock counts and resampler DC gain at 8/16.384/32.768/40 kHz\n";
    std::cout<<comparisons<<" exact full-state sample comparisons passed\n";
    return 0;
} catch(const std::exception &e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
