// ADSR envelope helpers (poly envTick + mono MonoEnv).
#pragma once

#include <cstdint>

void initAdsrLuts();
void updateEnvSusLevel(uint8_t sustain);

uint32_t adsrInc(uint8_t t);
uint32_t envSustainLevel();
uint32_t envTick(uint8_t &stage, uint32_t &level, bool gated);

extern uint32_t g_envSusLevel;

struct MonoEnv
{
    uint8_t stage = 0;
    uint32_t level = 0;
    bool wasGate = false;

    uint32_t process(bool gate);
};

extern MonoEnv g_envA;
extern MonoEnv g_envB;
