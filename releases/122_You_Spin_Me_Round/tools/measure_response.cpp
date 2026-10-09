// Copyright (c) 2026 soveda. SPDX-License-Identifier: MIT
// Host-only reference measurements; no claim of perceived quality or hardware timing.
#include <initializer_list>
#include "dsp/rotary.h"
#include <cmath>
#include <cstdio>
int main() {
 for(int x : {0,256,512,1024,2048,4095}) {
  spin::Rotary a,b; spin::Parameters p; p.intensity=x;p.fast=true;
  spin::Parameters q=p;q.perspective=spin::Perspective::Inside;
  double input=0,left=0,stereo=0,diff=0;
  for(int n=0;n<192000;++n){
   if(n%48==0){a.SetParameters(p);b.SetParameters(q);}
   int s=int(700*std::sin(n*6.283185307179586*440/48000));
   auto o=a.Process(s),i=b.Process(s);
   if(n>48000){input+=s*s;left+=o.left*o.left;stereo+=(o.left-o.right)*(o.left-o.right);diff+=(o.left-i.left)*(o.left-i.left);}
  }
  std::printf("X=%4d output/input %.3f stereo/output %.3f mode difference/output %.3f\n",x,std::sqrt(left/input),std::sqrt(stereo/left),std::sqrt(diff/left));
 }
}
