#include "arp.h"

#include "config_store.h"
#include "glyph_leds.h"
#include "voices.h"

namespace {

uint8_t g_arpNotes[8];
int g_arpCount = 0;
int g_arpIndex = 0;
int g_arpDir = 1;
uint32_t g_arpTimer = 0;

} // namespace

uint8_t g_arpNoteOut = 60;
bool g_arpGateOut = false;

void arpClearUnlocked()
{
    g_arpGateOut = false;
    g_arpCount = 0;
    g_arpTimer = 0;
}

uint32_t arpPeriodSamples(uint8_t mode)
{
    if (mode >= 5)
        return 4800;
    return 9600;
}

void rebuildArpList()
{
    int prevCount = g_arpCount;
    g_voiceA.copyHeld(g_arpNotes, &g_arpCount);
    if (g_arpCount <= 0)
    {
        g_arpGateOut = false;
        return;
    }
    uint8_t mode = g_ext.arpMode;
    uint8_t pattern = mode;
    if (pattern >= 5)
        pattern = (uint8_t)(pattern - 4);
    if (pattern == 1 || pattern == 3)
    {
        for (int i = 0; i < g_arpCount - 1; ++i)
            for (int j = i + 1; j < g_arpCount; ++j)
                if (g_arpNotes[j] < g_arpNotes[i])
                {
                    uint8_t t = g_arpNotes[i];
                    g_arpNotes[i] = g_arpNotes[j];
                    g_arpNotes[j] = t;
                }
    }
    else if (pattern == 2)
    {
        for (int i = 0; i < g_arpCount - 1; ++i)
            for (int j = i + 1; j < g_arpCount; ++j)
                if (g_arpNotes[j] > g_arpNotes[i])
                {
                    uint8_t t = g_arpNotes[i];
                    g_arpNotes[i] = g_arpNotes[j];
                    g_arpNotes[j] = t;
                }
    }
    if (g_arpIndex >= g_arpCount)
        g_arpIndex = 0;
    if (prevCount <= 0)
    {
        g_arpIndex = 0;
        g_arpNoteOut = g_arpNotes[0];
        g_arpGateOut = true;
        g_arpTimer = 0;
    }
}

void setArpMode(uint8_t a, bool announce)
{
    if (a > 8)
        a = 0;
    critical_section_enter_blocking(&g_midiCs);
    if (a == g_ext.arpMode)
    {
        critical_section_exit(&g_midiCs);
        return;
    }
    if (a != 0 && g_ext.arpMode == 0)
    {
        polyAllOffUnlocked();
        g_arpTimer = 0;
        g_arpIndex = 0;
        g_arpDir = 1;
        rebuildArpList();
        if (g_voiceA.gate())
        {
            g_arpNoteOut = g_voiceA.note();
            g_arpGateOut = true;
        }
        else
            g_arpGateOut = false;
    }
    else if (a == 0 && g_ext.arpMode != 0)
    {
        g_arpGateOut = false;
        g_arpCount = 0;
        g_arpTimer = 0;
        reseedPolyFromMonoUnlocked();
    }
    g_ext.arpMode = a;
    critical_section_exit(&g_midiCs);
    if (announce)
        g_glyph.playNumber(a);
}

void arpTick(uint8_t &noteA, bool &gateA)
{
    critical_section_enter_blocking(&g_midiCs);
    if (g_voiceA.gate())
    {
        rebuildArpList();
        uint32_t period = arpPeriodSamples(g_ext.arpMode);
        if (++g_arpTimer >= period)
        {
            g_arpTimer = 0;
            uint8_t pattern = g_ext.arpMode;
            if (pattern >= 5)
                pattern = (uint8_t)(pattern - 4);
            if (g_arpCount > 0)
            {
                if (pattern == 3)
                {
                    g_arpIndex += g_arpDir;
                    if (g_arpIndex >= g_arpCount - 1)
                    {
                        g_arpIndex = g_arpCount - 1;
                        g_arpDir = -1;
                    }
                    else if (g_arpIndex <= 0)
                    {
                        g_arpIndex = 0;
                        g_arpDir = 1;
                    }
                }
                else
                {
                    g_arpIndex = (g_arpIndex + 1) % g_arpCount;
                }
                g_arpNoteOut = g_arpNotes[g_arpIndex];
                g_arpGateOut = true;
            }
        }
        if (g_arpTimer > period / 2)
            g_arpGateOut = false;
        noteA = g_arpNoteOut;
        gateA = g_arpGateOut && g_voiceA.gate();
    }
    else
    {
        g_arpCount = 0;
        g_arpGateOut = false;
    }
    critical_section_exit(&g_midiCs);
}
