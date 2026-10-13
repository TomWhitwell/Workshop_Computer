// Simulates Turing_Clouds: does freeze hold the cloud when source drops to 0?
// Includes the real card source with its main() renamed away.
#define main card_main_unused
#include "../main.cpp"
#undef main

#include <cstdio>
#include <cstring>

static constexpr int SR = 48000;
static long g_t = 0;

// Scenario
double g_inputOnUntil = 1.0;   // tone plays until here (s), then source = 0
double g_freezeFrom = 1.0;     // switch Down from here (s); 1e9 = never

double g_rmsAcc = 0, g_rmsAcc2 = 0, g_cv1Sum = 0;
long g_n = 0;
bool ComputerCard::recording = false;
int16_t g_lastOut1 = 0, g_lastOut2 = 0, g_lastCV1 = 0;
void ComputerCard::recordOut1(int16_t v) { g_lastOut1 = v; }
void ComputerCard::recordOut2(int16_t v) { g_lastOut2 = v; }
void ComputerCard::recordCV1(int16_t v) { g_lastCV1 = v; }

void ComputerCard::Run()
{
    recording = true;
    long per = SR / 2;
    for (long t = 0; t < SR * 5; ++t)
    {
        g_t = t;
        ProcessSample();
        g_rmsAcc += (double)g_lastOut1 * g_lastOut1;
        g_rmsAcc2 += (double)g_lastOut2 * g_lastOut2;
        g_cv1Sum += g_lastCV1 + 2048;   // activeGrains * 512
        g_n++;
        if ((t + 1) % per == 0)
        {
            printf("t=%.1fs out1rms=%.0f out2rms=%.0f grains=%.2f\n",
                   (t + 1) / (double)SR, std::sqrt(g_rmsAcc / g_n),
                   std::sqrt(g_rmsAcc2 / g_n), (double)g_cv1Sum / g_n / 512.0);
            g_rmsAcc = g_rmsAcc2 = g_cv1Sum = 0; g_n = 0;
        }
    }
    recording = false;
}

int16_t simAudioIn1()
{
    if (g_t >= (long)(g_inputOnUntil * SR)) return 0;
    double ph = 2.0 * M_PI * 440.0 * g_t / SR;
    return (int16_t)(800.0 * std::sin(ph));
}
Switch simSwitchVal() { return (g_t >= (long)(g_freezeFrom * SR)) ? Down : Up; }
int16_t simCVIn(int) { return 0; }
int32_t g_mainKnob = 2048;
int32_t simKnobVal(Knob k)
{
    switch (k)
    {
    case Knob::Main: return g_mainKnob;
    case Knob::X:    return 2048;
    case Knob::Y:    return 3000;
    }
    return 2048;
}

int main(int argc, char** argv)
{
    const char* mode = argc > 1 ? argv[1] : "freeze_at_drop";
    if (!strcmp(mode, "freeze_at_drop")) { g_inputOnUntil = 1.0; g_freezeFrom = 1.0; }
    else if (!strcmp(mode, "freeze_late")) { g_inputOnUntil = 1.0; g_freezeFrom = 2.5; }
    else if (!strcmp(mode, "no_freeze"))   { g_inputOnUntil = 1.0; g_freezeFrom = 1e9; }
    else if (!strcmp(mode, "freeze_fast")) { g_inputOnUntil = 1.0; g_freezeFrom = 1.0; g_mainKnob = 4095; }
    else { printf("modes: freeze_at_drop | freeze_late | no_freeze\n"); return 1; }
    printf("=== %s (440Hz tone 0-1s, source=0 after) ===\n", mode);
    TuringClouds card;
    card.Run();
    return 0;
}
