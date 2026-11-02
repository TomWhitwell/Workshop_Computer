#define main card_main
#include "card.cpp"
#undef main
#include <cstdio>
static DawnTest c;
static void run(int n){ for(int i=0;i<n;i++){ if((i&255)==0) PlanService(); c.ProcessSample(); } }
int main(){
  g_card=&c; c.sw=ComputerCard::Middle; c.knob[0]=2048; c.knob[1]=300; c.knob[2]=2048;
  int T[4]={0,1,2,2}; int Sd[4]={31052,65108,36272,56804};
  for(int k=0;k<4;k++){ g_birdType[k]=T[k]; g_birdSeed[k]=Sd[k]; g_param[P_W0+k]=65536; } g_castN=4;
  g_param[P_LI]=0; g_param[P_CH]=0;              // night: no chimes or birds of their own
  run(48000*2);
  unsigned ch0=g_chimes, b0=g_birds;
  c.knob[0]=2500;                                // turn Main up one key, then leave it alone
  printf("Turned Main once, then left it. Chimes heard, second by second:");
  for(int s=0;s<6;s++){ unsigned a=g_chimes; run(48000); printf(" %u", (unsigned)(g_chimes-a)); }
  printf("   (should be 1 then 0s)\n");
  // sweep Main right round in one second, then leave it
  { unsigned a=g_chimes; for(int i=0;i<48;i++){ c.knob[0]= i*85; run(1000); } unsigned during=g_chimes-a; a=g_chimes; run(48000*3);
    printf("Swept Main through all 12 keys in 1 s: %u chimes during the turn, %u in the 3 s after   (at most 4-5, then 0)\n", during, (unsigned)(g_chimes-a)); }
  b0=g_birds; c.knob[1]=1229;                    // turn X to bird 1, then leave it
  g_param[P_W0]=65536;
  printf("Turned X once, then left it. Bird phrases, second by second:");
  for(int s=0;s<6;s++){ unsigned a=g_birds; run(48000); printf(" %u", (unsigned)(g_birds-a)); }
  printf("   (should be 1 then 0s)\n");
}
