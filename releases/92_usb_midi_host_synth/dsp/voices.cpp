#include "voices.h"

#include "arp.h"
#include "drums.h"
#include "protocol.h"
#include "runtime_state.h"

MonoVoice g_voiceA;
MonoVoice g_voiceB;
PolyVoice g_poly[kPolyMax];
uint32_t g_polyAge = 1;
critical_section_t g_midiCs;

void MonoVoice::noteOn(uint8_t note)
{
    release(note);
    if (count == STACK_MAX)
    {
        for (int i = 1; i < STACK_MAX; ++i)
            held[i - 1] = held[i];
        --count;
    }
    held[count++] = note;
}

void MonoVoice::noteOff(uint8_t note) { release(note); }

void MonoVoice::copyHeld(uint8_t *dst, int *n) const
{
    *n = count;
    for (int i = 0; i < count; ++i)
        dst[i] = held[i];
}

void MonoVoice::release(uint8_t note)
{
    for (int i = 0; i < count; ++i)
    {
        if (held[i] != note)
            continue;
        for (int j = i + 1; j < count; ++j)
            held[j - 1] = held[j];
        --count;
        return;
    }
}

void voicesInit() { critical_section_init(&g_midiCs); }

namespace {

void resetVoiceFx(PolyVoice &v, bool keepGlide)
{
    v.phase = 0;
    v.phase2 = 0;
    v.phase3 = 0;
    v.filtLp = 0;
    v.filtBp = 0;
    v.filtEnv = 65535;
    if (!keepGlide)
        v.glideInc = 0;
    v.chorusLfo = 0;
    v.chorusLfo2 = 0x40000000u;
    v.chorusWr = 0;
    for (int i = 0; i < PolyVoice::kChorusLen; ++i)
        v.chorusBuf[i] = 0;
}

} // namespace

void polyNoteOnUnlocked(uint8_t note, bool bendIsB)
{
    // Reclaim any existing voice for this note (gated or fading out).
    int found = -1;
    for (int i = 0; i < kPolyMax; ++i)
    {
        if (!(g_poly[i].sounding && g_poly[i].note == note))
            continue;
        if (found < 0)
            found = i;
        else
        {
            g_poly[i].gated = false;
            g_poly[i].sounding = false;
            g_poly[i].amp = 0;
            g_poly[i].envStage = 0;
            g_poly[i].envLevel = 0;
        }
    }
    if (found >= 0)
    {
        g_poly[found].bendIsB = bendIsB ? 1 : 0;
        g_poly[found].gated = true;
        g_poly[found].sounding = true;
        resetVoiceFx(g_poly[found], false);
        g_poly[found].amp = 0;
        g_poly[found].envStage = 1;
        g_poly[found].envLevel = 0;
        g_poly[found].age = g_polyAge++;
        g_poly[found].gen++;
        return;
    }

    int slot = -1;
    for (int i = 0; i < kPolyMax; ++i)
    {
        if (!g_poly[i].sounding)
        {
            slot = i;
            break;
        }
    }
    bool keepGlide = false;
    if (slot < 0)
    {
        uint32_t oldest = 0xFFFFFFFFu;
        for (int i = 0; i < kPolyMax; ++i)
        {
            if (g_poly[i].age < oldest)
            {
                oldest = g_poly[i].age;
                slot = i;
            }
        }
        // Voice steal: keep glide for acid-style slides into the new note.
        keepGlide = g_poly[slot].sounding && g_poly[slot].glideInc != 0;
    }
    g_poly[slot].note = note;
    g_poly[slot].bendIsB = bendIsB ? 1 : 0;
    g_poly[slot].gated = true;
    g_poly[slot].sounding = true;
    resetVoiceFx(g_poly[slot], keepGlide);
    g_poly[slot].amp = 0;
    g_poly[slot].envStage = 1;
    g_poly[slot].envLevel = 0;
    g_poly[slot].age = g_polyAge++;
    g_poly[slot].gen++;
}

void polyNoteOffUnlocked(uint8_t note)
{
    for (int i = 0; i < kPolyMax; ++i)
    {
        if (g_poly[i].sounding && g_poly[i].note == note && g_poly[i].gated)
        {
            g_poly[i].gated = false;
            if (g_poly[i].envStage != 0 && g_poly[i].envStage != 4)
                g_poly[i].envStage = 4;
        }
    }
}

void polyAllOffUnlocked()
{
    for (int i = 0; i < kPolyMax; ++i)
    {
        g_poly[i].gated = false;
        g_poly[i].sounding = false;
        g_poly[i].amp = 0;
        g_poly[i].envStage = 0;
        g_poly[i].envLevel = 0;
    }
}

void reseedPolyFromMonoUnlocked()
{
    polyAllOffUnlocked();
    uint8_t notes[MonoVoice::STACK_MAX];
    int n = 0;
    g_voiceA.copyHeld(notes, &n);
    for (int i = 0; i < n; ++i)
        polyNoteOnUnlocked(notes[i], false);
    g_voiceB.copyHeld(notes, &n);
    for (int i = 0; i < n; ++i)
        polyNoteOnUnlocked(notes[i], true);
}

void silenceAllVoicesUnlocked()
{
    g_voiceA.allOff();
    g_voiceB.allOff();
    g_gateA = false;
    g_gateB = false;
    polyAllOffUnlocked();
    arpClearUnlocked();
    drumsAllOff();
}

void silenceAllVoices()
{
    critical_section_enter_blocking(&g_midiCs);
    silenceAllVoicesUnlocked();
    critical_section_exit(&g_midiCs);
}

void forcePlayModeCleanup()
{
    critical_section_enter_blocking(&g_midiCs);
    silenceAllVoicesUnlocked();
    g_appMode = (uint8_t)AppMode::Play;
    critical_section_exit(&g_midiCs);
}
