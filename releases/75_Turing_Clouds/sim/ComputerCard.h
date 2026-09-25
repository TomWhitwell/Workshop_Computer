// Host-side stub of ComputerCard.h for simulating Turing_Clouds behavior.
// Scenario is driven from sim_main.cpp via these globals.
#pragma once
#include <cstdint>
#include <cmath>

enum class Knob { Main, X, Y };
enum Switch { Down, Middle, Up };
enum Input { Audio1, Audio2, CV1, CV2, Pulse1, Pulse2 };

// Scenario hooks (defined in sim_main.cpp)
extern int16_t simAudioIn1();
extern Switch simSwitchVal();
extern int32_t simKnobVal(Knob k);
extern int16_t simCVIn(int ch);

class ComputerCard
{
public:
    virtual void ProcessSample() = 0;
    void Run();  // loops ProcessSample at "48kHz" for simSamples samples

    int16_t AudioIn1() { return simAudioIn1(); }
    int16_t CVIn1() { return simCVIn(1); }
    int16_t CVIn2() { return simCVIn(2); }
    int32_t KnobVal(Knob k) { return simKnobVal(k); }
    Switch SwitchVal() { return simSwitchVal(); }
    bool Connected(Input) { return false; }
    bool PulseIn1RisingEdge() { return false; }
    void PulseOut1(bool) {}
    void AudioOut1(int16_t v) { out1_ = v; if (recording) recordOut1(v); }
    void AudioOut2(int16_t v) { out2_ = v; if (recording) recordOut2(v); }
    void CVOut1(int16_t v) { if (recording) recordCV1(v); }   // card reports activeGrains*512-2048
    void CVOut2(int16_t) {}
    void LedOn(int, bool) {}

    int16_t out1_ = 0, out2_ = 0;
    // instrumentation
    static bool recording;
    static void recordOut1(int16_t v);
    static void recordOut2(int16_t v);
    static void recordCV1(int16_t v);
};

inline void set_sys_clock_khz(int, bool) {}
