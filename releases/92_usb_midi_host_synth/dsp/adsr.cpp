#include "adsr.h"

#include "config_store.h"

uint32_t adsrInc(uint8_t t)
{
    uint32_t samples = 48u + (uint32_t)t * (uint32_t)t * 8u;
    return (65535u + samples - 1u) / samples;
}

uint32_t envSustainLevel()
{
    return ((uint32_t)g_ext.sustain * 65535u) / 127u;
}

uint32_t envTick(uint8_t &stage, uint32_t &level, bool gated)
{
    if (!gated && (stage == 1 || stage == 2 || stage == 3))
        stage = 4;
    uint32_t sus = envSustainLevel();
    switch (stage)
    {
    case 1:
        if (g_ext.attack == 0)
            level = 65535;
        else
        {
            level += adsrInc(g_ext.attack);
            if (level >= 65535)
                level = 65535;
        }
        if (level >= 65535)
            stage = 2;
        break;
    case 2:
        if (g_ext.decay == 0 || level <= sus)
        {
            level = sus;
            stage = 3;
        }
        else
        {
            uint32_t inc = adsrInc(g_ext.decay);
            if (level <= sus + inc)
            {
                level = sus;
                stage = 3;
            }
            else
                level -= inc;
        }
        break;
    case 3:
        level = sus;
        if (!gated)
            stage = 4;
        break;
    case 4:
        if (g_ext.releaseAmp == 0)
            level = 0;
        else
        {
            uint32_t inc = adsrInc(g_ext.releaseAmp);
            if (level <= inc)
                level = 0;
            else
                level -= inc;
        }
        if (level == 0)
            stage = 0;
        break;
    default:
        level = 0;
        stage = 0;
        break;
    }
    return level;
}

uint32_t MonoEnv::process(bool gate)
{
    if (gate && !wasGate)
    {
        stage = 1;
        level = 0;
    }
    else if (!gate && wasGate && stage != 0)
        stage = 4;
    wasGate = gate;
    if (stage == 0)
        return 0;
    uint32_t lv = envTick(stage, level, gate);
    if (stage == 0)
        level = 0;
    return lv;
}

MonoEnv g_envA;
MonoEnv g_envB;
