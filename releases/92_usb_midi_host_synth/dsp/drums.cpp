#include "drums.h"

#include "synth.h"

namespace {

enum class DrumKind : uint8_t
{
    Kick = 0,
    Snare,
    HatClosed,
    HatOpen,
    TomLow,
    TomHigh,
    Crash,
    Ride,
    Count
};

constexpr int kDrumVoiceMax = 8;

int32_t drumHp(int32_t n, int32_t &hp, int32_t coef)
{
    int32_t n7 = n << 7;
    hp += ((n7 - hp) * coef) >> 15;
    return (n7 - hp) >> 7;
}

struct DrumVoice
{
    bool active = false;
    DrumKind kind = DrumKind::Kick;
    uint32_t phase = 0;
    uint32_t phase2 = 0;
    uint32_t pitch = 0; // phase increment
    uint32_t pitchDec = 0;
    uint32_t amp = 0; // 0..65535
    uint32_t ampDec = 0;
    uint32_t noise = 1;
    int32_t hp = 0;
    int32_t bp = 0;
    uint8_t vel = 100;
    uint32_t age = 0;
};

DrumVoice g_drums[kDrumVoiceMax];
uint32_t g_drumAge = 1;

// Approximate phase increments at 48 kHz (freq / 48000 * 2^32).
constexpr uint32_t kInc150 = 13421773u;  // ~150 Hz kick start
constexpr uint32_t kInc50 = 4473924u;    // ~50 Hz kick end-ish
constexpr uint32_t kInc180 = 16106127u;  // snare body
constexpr uint32_t kInc120 = 10737418u;  // low tom start
constexpr uint32_t kInc200 = 17895697u;  // high tom
constexpr uint32_t kInc800 = 71582788u;  // ride ping
constexpr uint32_t kInc1200 = 107374182u;

uint32_t nextNoise(uint32_t &s)
{
    // xorshift32
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

int32_t noiseSample(uint32_t &s)
{
    return (int32_t)((nextNoise(s) >> 20) & 0xFFF) - 2048;
}

DrumKind kindFromNote(uint8_t note)
{
    switch (note)
    {
    case 36:
        return DrumKind::Kick;
    case 37:
        return DrumKind::Snare;
    case 38:
        return DrumKind::HatClosed;
    case 39:
        return DrumKind::HatOpen;
    case 40:
        return DrumKind::TomLow;
    case 41:
        return DrumKind::TomHigh;
    case 42:
        return DrumKind::Crash;
    case 43:
    default:
        return DrumKind::Ride;
    }
}

int allocVoice(DrumKind kind)
{
    // Prefer free; else steal oldest of same kind; else oldest overall.
    int free = -1;
    int same = -1;
    uint32_t sameAge = 0xFFFFFFFFu;
    int oldest = 0;
    uint32_t oldestAge = 0xFFFFFFFFu;
    for (int i = 0; i < kDrumVoiceMax; ++i)
    {
        if (!g_drums[i].active)
        {
            free = i;
            break;
        }
        if (g_drums[i].kind == kind && g_drums[i].age < sameAge)
        {
            sameAge = g_drums[i].age;
            same = i;
        }
        if (g_drums[i].age < oldestAge)
        {
            oldestAge = g_drums[i].age;
            oldest = i;
        }
    }
    if (free >= 0)
        return free;
    if (same >= 0)
        return same;
    return oldest;
}

void chokeKind(DrumKind kind)
{
    for (int i = 0; i < kDrumVoiceMax; ++i)
    {
        if (g_drums[i].active && g_drums[i].kind == kind)
        {
            // Fast fade instead of hard cut.
            g_drums[i].ampDec = 4000;
        }
    }
}

void startVoice(DrumVoice &v, DrumKind kind, uint8_t velocity)
{
    v.active = true;
    v.kind = kind;
    v.phase = 0;
    v.phase2 = 0x80000000u;
    v.hp = 0;
    v.bp = 0;
    v.noise = 0xA5A5A5A5u ^ ((uint32_t)kind << 16) ^ velocity;
    v.vel = velocity < 1 ? 1 : velocity;
    v.age = g_drumAge++;
    v.amp = 50000u + (uint32_t)v.vel * 120u;
    if (v.amp > 65535)
        v.amp = 65535;

    switch (kind)
    {
    case DrumKind::Kick:
        v.pitch = kInc150 + ((uint32_t)v.vel << 14);
        v.pitchDec = 180;
        v.ampDec = 55 + (40 - (v.vel >> 3));
        break;
    case DrumKind::Snare:
        v.pitch = kInc180;
        v.pitchDec = 40;
        v.ampDec = 90 + (50 - (v.vel >> 3));
        break;
    case DrumKind::HatClosed:
        v.pitch = 0;
        v.pitchDec = 0;
        v.ampDec = 420 + (200 - v.vel);
        break;
    case DrumKind::HatOpen:
        v.pitch = 0;
        v.pitchDec = 0;
        v.ampDec = 70 + (40 - (v.vel >> 3));
        break;
    case DrumKind::TomLow:
        v.pitch = kInc120 + ((uint32_t)v.vel << 12);
        v.pitchDec = 55;
        v.ampDec = 70;
        break;
    case DrumKind::TomHigh:
        v.pitch = kInc200 + ((uint32_t)v.vel << 12);
        v.pitchDec = 70;
        v.ampDec = 85;
        break;
    case DrumKind::Crash:
        v.pitch = kInc800;
        v.pitchDec = 2;
        v.ampDec = 28;
        break;
    case DrumKind::Ride:
        v.pitch = kInc1200;
        v.pitchDec = 8;
        v.ampDec = 45;
        break;
    default:
        break;
    }
}

int32_t renderVoice(DrumVoice &v)
{
    if (!v.active)
        return 0;

    int32_t s = 0;
    switch (v.kind)
    {
    case DrumKind::Kick:
    {
        v.phase += v.pitch;
        if (v.pitch > kInc50 + v.pitchDec)
            v.pitch -= v.pitchDec;
        else
            v.pitch = kInc50;
        s = phaseSin(v.phase);
        // Soft click at start via tiny noise.
        if (v.amp > 50000)
            s += noiseSample(v.noise) >> 4;
        break;
    }
    case DrumKind::Snare:
    {
        v.phase += v.pitch;
        if (v.pitch > v.pitchDec)
            v.pitch -= v.pitchDec;
        int32_t body = phaseSin(v.phase) >> 1;
        int32_t n = noiseSample(v.noise);
        // Mild HP on noise.
        int32_t hn = drumHp(n, v.hp, 20000);
        s = body + (hn >> 1);
        break;
    }
    case DrumKind::HatClosed:
    case DrumKind::HatOpen:
    {
        int32_t n = noiseSample(v.noise);
        // Aggressive HP.
        s = drumHp(n, v.hp, 28000);
        if (v.kind == DrumKind::HatClosed)
            s = (s * 3) >> 2;
        break;
    }
    case DrumKind::TomLow:
    case DrumKind::TomHigh:
    {
        v.phase += v.pitch;
        if (v.pitch > v.pitchDec * 8)
            v.pitch -= v.pitchDec;
        s = phaseSin(v.phase);
        // A little noise for stick.
        if (v.amp > 45000)
            s += noiseSample(v.noise) >> 5;
        break;
    }
    case DrumKind::Crash:
    {
        int32_t n = noiseSample(v.noise);
        int32_t hn = drumHp(n, v.hp, 18000);
        v.phase += v.pitch;
        v.phase2 += v.pitch + (v.pitch >> 2);
        int32_t metal =
            (phaseSin(v.phase) + (phaseSin(v.phase2) >> 1)) >> 2;
        s = (hn >> 1) + metal;
        break;
    }
    case DrumKind::Ride:
    {
        int32_t n = noiseSample(v.noise);
        int32_t hn = drumHp(n, v.hp, 24000) >> 2;
        v.phase += v.pitch;
        int32_t ping = phaseSin(v.phase) >> 2;
        // Shorter ping: decay pitch amp via overall amp already.
        s = hn + ping;
        break;
    }
    default:
        break;
    }

    s = (s * (int32_t)v.amp) >> 16;
    s = (s * (int32_t)v.vel) >> 7;
    s = clamp12(s);

    if (v.amp <= v.ampDec)
    {
        v.amp = 0;
        v.active = false;
    }
    else
        v.amp -= v.ampDec;

    return s;
}

} // namespace

void drumsInit()
{
    for (int i = 0; i < kDrumVoiceMax; ++i)
        g_drums[i] = DrumVoice{};
    g_drumAge = 1;
}

void drumNoteOn(uint8_t note, uint8_t velocity)
{
    if (note < kDrumNoteMin || note > kDrumNoteMax)
        return;
    DrumKind kind = kindFromNote(note);

    // Closed hat chokes open; new open replaces open.
    if (kind == DrumKind::HatClosed || kind == DrumKind::HatOpen)
        chokeKind(DrumKind::HatOpen);
    if (kind == DrumKind::HatClosed)
        chokeKind(DrumKind::HatClosed);

    int slot = allocVoice(kind);
    startVoice(g_drums[slot], kind, velocity);
}

void drumNoteOff(uint8_t note)
{
    // Only open hat is chokeable by note-off (pad release).
    if (note != 39)
        return;
    chokeKind(DrumKind::HatOpen);
}

void drumsAllOff()
{
    for (int i = 0; i < kDrumVoiceMax; ++i)
        g_drums[i].active = false;
}

void drumsRenderMix(int32_t &mixL, int32_t &mixR)
{
    int32_t sumL = 0;
    int32_t sumR = 0;
    int active = 0;
    for (int i = 0; i < kDrumVoiceMax; ++i)
    {
        if (!g_drums[i].active)
            continue;
        int32_t s = renderVoice(g_drums[i]);
        ++active;
        // Pan: kick/toms center; snare slight L; hats/crash/ride slight R.
        switch (g_drums[i].kind)
        {
        case DrumKind::Kick:
        case DrumKind::TomLow:
        case DrumKind::TomHigh:
            sumL += s;
            sumR += s;
            break;
        case DrumKind::Snare:
            sumL += s;
            sumR += (s * 3) >> 2;
            break;
        default:
            sumL += (s * 3) >> 2;
            sumR += s;
            break;
        }
    }
    if (active > 2)
    {
        sumL = (sumL * 3) >> 2;
        sumR = (sumR * 3) >> 2;
    }
    // Leave headroom vs melodic poly.
    sumL = (sumL * 3) >> 2;
    sumR = (sumR * 3) >> 2;
    mixL = clamp12(mixL + sumL);
    mixR = clamp12(mixR + sumR);
}
