#define main card_main
#include "card.cpp"
#undef main
#include <cstdio>
static void wav(const char*fn,std::vector<int16_t>&a){
  FILE*f=fopen(fn,"wb"); uint32_t n=a.size(), sr=48000, bytes=n*2;
  auto w32=[&](uint32_t v){fwrite(&v,4,1,f);}; auto w16=[&](uint16_t v){fwrite(&v,2,1,f);};
  fwrite("RIFF",1,4,f);w32(36+bytes);fwrite("WAVEfmt ",1,8,f);w32(16);w16(1);w16(1);w32(sr);w32(sr*2);w16(2);w16(16);
  fwrite("data",1,4,f);w32(bytes); for(uint32_t i=0;i<n;i++){int16_t x=a[i]*16;fwrite(&x,2,1,f);} fclose(f);}
int main(){
  static DawnTest c; g_card=&c; c.sw=ComputerCard::Middle; c.knob[0]=2048; c.knob[2]=2400;
  // June cast near Bath, as the page sends it
  int T[4]={0,1,2,2}; int S_[4]={31052,65108,36272,56804}; const char*nm[4]={"swift","blackbird","robin","blackcap"};
  for(int k=0;k<4;k++){ g_birdType[k]=T[k]; g_birdSeed[k]=S_[k]; g_param[P_W0+k]=65536; }
  g_castN=4; g_param[P_LI]=0; g_param[P_CH]=65536;   // birds only, no drone or chimes
  // Step X through its five positions, 15 s each, for a listening preview
  c.o2.clear(); int xs[5]={300,1229,2048,2867,3900};
  for(int i=0;i<48000*75;i++){ c.knob[1]=xs[i/(48000*15)]; if((i&255)==0) PlanService(); c.ProcessSample(); }
  long clip=0; for(auto v:c.o2) if(abs(v)>=2047) clip++; printf("clipped %ld\n",clip);
  wav("pick.wav",c.o2);
}
