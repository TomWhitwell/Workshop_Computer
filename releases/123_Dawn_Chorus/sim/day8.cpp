#define main card_main
#include "card.cpp"
#undef main
#include <cstdio>
// A compressed June day near Bath with the four-bird cast (as the page sends it), X at a chosen position
static double ss(double a,double b,double x){double t=(x-a)/(b-a);t=t<0?0:t>1?1:t;return t*t*(3-2*t);}
static double birdsong(double el,bool r){double day=0.8-0.2*ss(15,40,el);
  if(r){double w=pow(ss(-8,1,el),1.5),pk=exp(-pow((el-2)/5,2));return w*(day+(1-day)*pk);}
  return ss(-6,3,el)*std::min(1.0,day+0.15*exp(-pow((el-5)/5,2)));}
static double avail(int t,double el){ if(t==8) return 1-ss(-8,-2,el); if(t==1||t==2) return ss(-10,-4,el); return ss(-6,0,el); }
static void wav(const char*fn,std::vector<int16_t>&a,std::vector<int16_t>&b){
  FILE*f=fopen(fn,"wb"); uint32_t n=a.size(), sr=48000, bytes=n*4;
  auto w32=[&](uint32_t v){fwrite(&v,4,1,f);}; auto w16=[&](uint16_t v){fwrite(&v,2,1,f);};
  fwrite("RIFF",1,4,f);w32(36+bytes);fwrite("WAVEfmt ",1,8,f);w32(16);w16(1);w16(2);w32(sr);w32(sr*4);w16(4);w16(16);
  fwrite("data",1,4,f);w32(bytes); for(uint32_t i=0;i<n;i++){int16_t x=a[i]*16,y=b[i]*16;fwrite(&x,2,1,f);fwrite(&y,2,1,f);} fclose(f);}
int main(int argc,char**argv){
  int x = argc>1 ? atoi(argv[1]) : 1100;
  int T[5]={0,1,2,2,8}; int Sd[5]={31052,65108,36272,56804,4867};   // swift, blackbird, robin, blackcap, tawny owl
  static DawnTest c; g_card=&c; c.sw=ComputerCard::Middle; c.knob[0]=2048; c.knob[1]=x; c.knob[2]=2400;
  g_param[P_CL]=0.3*65536; g_param[P_WI]=0.3*65536; g_param[P_TE]=0.7*65536; g_param[P_HU]=0.6*65536;
  int Tsec=180; g_castN=4;
  for(int i=0;i<Tsec*48000;i++){
    if(i%96==0){ double t=i/48000.0; bool r=t<Tsec/2.0; double el=92*sin(M_PI*t/Tsec)-30;
      g_param[P_LI]=ss(-12,12,el)*65536;
      double a[4]; for(int k=0;k<4;k++) a[k]=avail(T[k],el);
      double oa=avail(8,el); int t3=T[3], s3=Sd[3]; if(oa>a[3]){ a[3]=oa; t3=8; s3=Sd[4]; }
      for(int k=0;k<4;k++){ g_birdType[k]= k==3?t3:T[k]; g_birdSeed[k]= k==3?s3:Sd[k]; g_param[P_W0+k]=a[k]*65536; }
      g_param[P_CH]=std::max(birdsong(el,r),0.05*oa)*65536; }
    if((i&255)==0) PlanService();
    c.ProcessSample();
  }
  long clip=0; for(auto v:c.o2) if(abs(v)>=2047) clip++;
  printf("June day, X=%d: bird phrases %u, chimes %u, clipped %ld\n",x,(unsigned)g_birds,(unsigned)g_chimes,clip);
  wav("june8.wav",c.o1,c.o2);
}
