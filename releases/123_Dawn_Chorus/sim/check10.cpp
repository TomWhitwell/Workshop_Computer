#define main card_main
#include "card.cpp"
#undef main
#include <cstdio>
static DawnTest c;
static void run(int samples){ for(int i=0;i<samples;i++){ if((i&255)==0) PlanService(); c.ProcessSample(); } }
int main(){
  g_card=&c; c.sw=ComputerCard::Middle; c.knob[0]=2048; c.knob[1]=2048; c.knob[2]=2048;
  int T[4]={0,1,2,2}; int Sd[4]={31052,65108,36272,56804};
  for(int k=0;k<4;k++){ g_birdType[k]=T[k]; g_birdSeed[k]=Sd[k]; g_param[P_W0+k]=65536; } g_castN=4;
  g_param[P_LI]=65536; g_param[P_CH]=65536;
  run(48000);
  const char*inst[6]={"marimba","harp","xylophone","bell","vibraphone","glockenspiel"};
  // 1. X and Y at 12 o'clock, slopes 0 -> 5 V on CV 1 and CV 2 (5 V = 1706 counts)
  c.conn[ComputerCard::CV1]=true; c.conn[ComputerCard::CV2]=true;
  printf("X and Y at 12 o'clock, slope on CV In 1 and 2:\n");
  for(double v=0; v<=5.01; v+=0.625){ c.cv1=c.cv2=(int16_t)(v*1706/5); run(480);
    printf("  %4.2f V  ->  X position %d (%s)   Y -> %s\n", v, c.birdZone, c.birdZone? "bird leads":"all four", inst[c.presetZone]); }
  // 2. Unplugged: knobs as before
  c.conn[ComputerCard::CV1]=false; c.conn[ComputerCard::CV2]=false; c.cv1=c.cv2=0;
  printf("Unplugged, knobs only:\n");
  for(int k: {0, 1024, 2048, 3072, 4095}){ c.knob[1]=k; c.knob[2]=k; run(480); printf("  knob %4d -> X position %d, Y %s\n", k, c.birdZone, inst[c.presetZone]); }
  // 3. Rapidly wiggling Y between zones: auditions limited to 4 a second
  unsigned ch0=g_chimes; g_param[P_LI]=0; g_param[P_CH]=0; run(48000); ch0=g_chimes;
  for(int i=0;i<40;i++){ c.knob[2]= (i&1)? 500:3500; run(1200); }   // 40 flips in 1 s
  printf("Y flipped 40 times in 1 s at night -> %u audition chimes (limit 4)\n", (unsigned)(g_chimes-ch0));
  // 4. Switch: a 5 ms glitch to Down does nothing; a 30 ms tap starts the preview
  run(48000); c.sw=ComputerCard::Down; run(240); c.sw=ComputerCard::Middle; run(480);
  printf("5 ms Down glitch -> preview %s\n", c.auditionTicks>0? "STARTED (bad)":"not started (good)");
  c.sw=ComputerCard::Down; run(1440); c.sw=ComputerCard::Middle; run(480);
  printf("30 ms tap Down -> preview %s\n", c.auditionTicks>0? "started (good)":"NOT started (bad)");
  // 5. Birds still sing with plans made on core 1; X picks the lead
  g_param[P_LI]=65536; g_param[P_CH]=65536; c.auditionTicks=0; c.knob[1]=2048; run(48000*2);
  int cnt[5]={0};
  for(int i=0;i<48000*40;i++){ if((i&255)==0) PlanService(); uint8_t prev[3]; for(int k=0;k<3;k++) prev[k]=c.birds[k].state; c.ProcessSample();
    for(int k=0;k<3;k++) if(prev[k]==0 && c.birds[k].state!=0){ int sl=c.birds[k].slot; cnt[sl>=0?sl:4]++; } }
  printf("X at 12 o'clock (position %d) for 40 s: swift %d, blackbird %d, robin %d, blackcap %d, general %d\n", c.birdZone, cnt[0],cnt[1],cnt[2],cnt[3],cnt[4]);
  long clip=0; for(auto v:c.o2) if(abs(v)>=2047) clip++; printf("clipped samples: %ld\n", clip);
}
