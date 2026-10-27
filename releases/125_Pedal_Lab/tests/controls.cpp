// SPDX-License-Identifier: MPL-2.0
#define main firmware_main
#include "../src/main.cpp"
#undef main
#include <memory>
#include <iostream>
#include <stdexcept>
static void require(bool c,const char*m){if(!c)throw std::runtime_error(m);}
static void ms(PedalLab &c,int count){for(int n=0;n<count*48;++n)c.ProcessSample();}
int main() try {
    auto c=std::make_unique<PedalLab>();ms(*c,30);require(c->leds[0],"startup selection");
    for(unsigned slot=1;slot<=spin::program_count;++slot) {
        c->sw=ComputerCard::Down;ms(*c,50);c->sw=ComputerCard::Middle;ms(*c,30);
        for(unsigned i=0;i<6;++i)require(i==4||c->leds[i]==(i==slot%spin::program_count),"tap/wrap selection");
    }
    c->sw=ComputerCard::Down;c->knobs[0]=0;ms(*c,450);
    c->sw=ComputerCard::Middle;ms(*c,40);require(c->leds[0],"long hold must not cycle");
    c->plugged[ComputerCard::CV1]=true;c->cv[0]=2047;ms(*c,100);
    require(c->cvout[0]>=2032,"positive CV1 must raise POT0");
    c->cv[0]=-2048;ms(*c,100);require(c->cvout[0]==-2048,"negative CV1 must clamp POT0");
    c->plugged[ComputerCard::CV2]=true;c->cv[1]=2047;ms(*c,100);require(c->cvout[1]>=2032,"CV2 maps to POT1");
    c->plugged[ComputerCard::Audio1]=true;c->in[0]=2047;ms(*c,500);
    require(c->out[0]>=-2048&&c->out[0]<=2047&&c->out[1]>=-2048&&c->out[1]<=2047,"DAC range");
    std::cout<<"PASS firmware control integration: tap/wrap, hold, CV polarity/clamp and DAC bounds\n";
    return 0;
} catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}
