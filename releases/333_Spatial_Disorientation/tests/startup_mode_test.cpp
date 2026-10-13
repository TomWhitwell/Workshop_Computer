// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "startup_mode.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace spatial;
int main(){
    for(int main: {0,2048,4095}){
        StartupMode normal;for(int i=0;i<50;++i)normal.Tick(false,main);
        assert(normal.Ready()&&normal.Selected()==Mode::Orbits);
        normal.Tick(true,4095);assert(normal.Selected()==Mode::Orbits);
    }
    StartupMode held;
    for(int i=0;i<49;++i)held.Tick(true,0);
    assert(!held.Ready()&&!held.Selecting());held.Tick(true,0);
    assert(held.Selecting()&&held.LedMask()==0);
    held.Tick(true,2048);assert(held.Selected()==Mode::Mixer&&held.LedMask()==0x15);
    held.Tick(true,4095);assert(held.Selected()==Mode::Disorientation&&held.LedMask()==0x2a);
    held.Tick(true,2710);assert(held.Selected()==Mode::Disorientation); // Hysteresis.
    held.Tick(true,2600);assert(held.Selected()==Mode::Mixer);
    for(int i=0;i<5;++i)held.Tick(false,0);assert(!held.Ready());
    held.Tick(true,2048);assert(!held.Ready()); // Release bounce cannot confirm.
    for(int i=0;i<6;++i)held.Tick(false,4095);
    assert(held.Ready()&&!held.Selecting()&&held.Selected()==Mode::Mixer);
    held.Tick(true,0);assert(held.Selected()==Mode::Mixer); // Locked until reset.
    std::puts("PASS: default boot, ADC warmup, three selection LED columns, hysteresis, release debounce and locked mode");
}
