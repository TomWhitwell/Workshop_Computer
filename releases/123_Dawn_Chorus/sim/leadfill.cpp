#define main card_main
#include "card.cpp"
#undef main
#include <cstdio>
int main(int argc,char**argv){ if(argc>1) rngState=strtoul(argv[1],0,16);
  static DawnTest c; g_card=&c; c.sw=ComputerCard::Middle; c.knob[0]=2048; c.knob[2]=2400;
  int T[4]={0,1,2,5}; int Sd[4]={31052,65108,36272,2210};   // swift, blackbird, robin, wood pigeon (long coos)
  for(int k=0;k<4;k++){ g_birdType[k]=T[k]; g_birdSeed[k]=Sd[k]; g_param[P_W0+k]=65536; } g_castN=4;
  g_param[P_LI]=0; g_param[P_CH]=0.8*65536;                // birds only, morning birdsong level
  int xs[5]={300,1229,2048,2867,3900}; const char*lbl[5]={"all four    ","1 swift     ","2 blackbird ","3 robin     ","4 pigeon    "};
  for(int xi=0;xi<5;xi++){
    c.knob[1]=xs[xi]; int cnt[5]={0}; long tsing[5]={0};
    for(int i=0;i<48000*2;i++){ if((i&255)==0) PlanService(); c.ProcessSample(); }
    for(int i=0;i<48000*300;i++){
      if((i&255)==0) PlanService();
      uint8_t prev[3]; for(int k=0;k<3;k++) prev[k]=c.birds[k].state;
      c.ProcessSample();
      for(int k=0;k<3;k++) if(prev[k]==0 && c.birds[k].state!=0){ int sl=c.birds[k].slot; cnt[sl>=0?sl:4]++; }
      for(int k=0;k<3;k++) if(c.birds[k].state==1 && c.birds[k].env>0){ int sl=c.birds[k].slot; tsing[sl>=0?sl:4]++; }
    }
    int tot=cnt[0]+cnt[1]+cnt[2]+cnt[3]; int lead=xi-1;
    printf("X %s total %3d   swift %2d blackbird %2d robin %2d pigeon %2d", lbl[xi], tot, cnt[0],cnt[1],cnt[2],cnt[3]);
    long tt=tsing[0]+tsing[1]+tsing[2]+tsing[3];
    if(lead>=0) printf("   lead: %3.0f%% of phrases, %3.0f%% of singing time", 100.0*cnt[lead]/tot, 100.0*tsing[lead]/tt);
    printf("   (birds audible %2.0f%% of the time)\n", 100.0*tt/(48000.0*300));
  }
}
