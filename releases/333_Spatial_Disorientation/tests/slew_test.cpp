// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "dsp/fixed.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
template<int K> void Check(){
    for(int d=-262144;d<=262144;++d){
        for(int from : {-10000,0,10000})
            assert(spatial::Slew(from,from+d,1<<K)==spatial::SlewPower<K>(from,from+d));
    }
    // Compare entire increasing/decreasing convergence curves, including zero.
    for(int start : {-100000,-257,-1,0,1,257,100000}){
        for(int target : {-100000,-257,-1,0,1,257,100000}){
            int old=start,fresh=start;
            while(old!=target){old=spatial::Slew(old,target,1<<K);fresh=spatial::SlewPower<K>(fresh,target);assert(old==fresh);}
            assert(spatial::SlewPower<K>(fresh,target)==target);
        }
    }
}
int main(){Check<2>();Check<7>();Check<8>();Check<9>();std::puts("PASS 6291468 exact slew comparisons plus full settling curves");}
