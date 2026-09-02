// Mono note stacks + 4-voice poly pool (shared g_midiCs).
#pragma once

#include "pico/critical_section.h"

#include <cstdint>

struct MonoVoice
{
    static constexpr int STACK_MAX = 8;
    uint8_t held[STACK_MAX] = {0};
    int count = 0;

    void noteOn(uint8_t note);
    void noteOff(uint8_t note);
    void allOff() { count = 0; }
    bool gate() const { return count > 0; }
    uint8_t note() const { return count > 0 ? held[count - 1] : 0; }
    void copyHeld(uint8_t *dst, int *n) const;

private:
    void release(uint8_t note);
};

struct PolyVoice
{
    static constexpr int kChorusLen = 256; // ~5.3 ms @ 48 kHz, per-voice

    uint8_t note = 0;
    uint8_t bendIsB = 0;
    bool gated = false;
    bool sounding = false;
    uint8_t envStage = 0;
    uint32_t envLevel = 0;
    uint32_t phase = 0;
    uint32_t phase2 = 0;
    uint32_t phase3 = 0;
    uint32_t age = 0;
    uint32_t gen = 0;
    int32_t amp = 0; // 0..4096 clickless edge (always); ADSR is separate

    // Voice-local character FX (never applied on the mix bus).
    int32_t filtLp = 0;
    int32_t filtBp = 0;
    uint32_t filtEnv = 0;   // 0..65535 filter/FM brightness envelope
    uint32_t glideInc = 0;  // slewed phase increment (0 = snap next)
    uint32_t chorusLfo = 0;
    uint32_t chorusLfo2 = 0;
    uint16_t chorusWr = 0;
    int16_t chorusBuf[kChorusLen] = {0};
};

constexpr int kPolyMax = 4;

extern MonoVoice g_voiceA;
extern MonoVoice g_voiceB;
extern PolyVoice g_poly[kPolyMax];
extern uint32_t g_polyAge;
extern critical_section_t g_midiCs;

void voicesInit();
void polyNoteOnUnlocked(uint8_t note, bool bendIsB);
void polyNoteOffUnlocked(uint8_t note);
void polyAllOffUnlocked();
void reseedPolyFromMonoUnlocked();
void silenceAllVoicesUnlocked();
void silenceAllVoices();
void forcePlayModeCleanup();
