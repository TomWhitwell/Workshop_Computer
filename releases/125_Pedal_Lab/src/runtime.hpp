// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "audio.hpp"
// A stable entry point shared by the ISR and compiled-ARM timing checks.
// No peripheral access: the benchmark measures the complete stereo DSP path.
extern "C" __attribute__((noinline,used)) inline void spin_process_frame(
    spin::Audio* audio,const spin::Stereo* in,spin::Stereo* out,spin::Program program) {
    *out=audio->process(*in,program);
}
struct SpinFrameInput {spin::Stereo audio;uint16_t pots[3];};
extern "C" __attribute__((noinline,used)) inline void spin_virtual_frame(
    spin::Machine* machine,const SpinFrameInput* in,spin::Stereo* out,spin::Program program) {
    machine->begin(in->audio.l,in->audio.r,in->pots);program(*machine);
    *out={machine->regs[0x16],machine->regs[0x17]};machine->end_sample();
}
extern "C" __attribute__((noinline,used)) inline void spin_resample_frame(
    spin::Audio* audio,const spin::Stereo* in,spin::Stereo* out,const spin::Stereo* virtual_output) {
    *out=audio->process_with(*in,[&](spin::Stereo){return *virtual_output;});
}
