#include "synth.h"

#include "adsr.h"
#include "config_store.h"
#include "protocol.h"
#include "voice_matrix.h"
#include "voices.h"

namespace {

uint32_t g_midiPhaseInc[128];
int16_t g_sinLut[256];
int32_t g_cutoffFLut[128];

constexpr uint64_t kPwmWidthMul = (0xF0000000ull << 32) / 127ull;
constexpr int32_t kThirdScale = 21845; // 1/3 in Q16

constexpr uint32_t kInv127Q32 = 33701899u; // (1ull << 32) / 127
constexpr uint64_t kSemiQ32 = 4540449315ull; // round(2^(1/12) * 2^32)
constexpr int32_t kThirdQ16 = 21845;       // round(65536 / 3)
constexpr int32_t kSvfStateLim = 4096 << 7;

constexpr uint32_t kInv127Q32 = 33701899u; // (1ull << 32) / 127
constexpr uint64_t kSemiQ32 = 4540449315ull; // round(2^(1/12) * 2^32)
constexpr int32_t kThirdQ16 = 21845;       // round(65536 / 3)
constexpr int32_t kSvfStateLim = 4096 << 7;

uint32_t noteIncrement(int32_t note)
{
    if (note < 0)
        note = 0;
    if (note > 127)
        note = 127;
    return g_midiPhaseInc[note];
}

int32_t polyblepCorr(uint32_t phase, uint32_t invInc)
{
    if (invInc == 0)
        return 0;
    if ((uint64_t)phase * invInc < (1ull << 32))
    {
        uint32_t x = (uint32_t)(((uint64_t)phase * invInc) >> 16);
        int32_t corr =
            (int32_t)(2 * x) - (int32_t)((x * x) >> 16) - 65536;
        return (corr * 2048) >> 16;
    }
    uint32_t inv = 0u - phase;
    if ((uint64_t)inv * invInc < (1ull << 32))
    {
        uint32_t xn = (uint32_t)(((uint64_t)inv * invInc) >> 16);
        int32_t up1 = 65536 - (int32_t)xn;
        int32_t corr = (up1 * up1) >> 16;
        return (corr * 2048) >> 16;
    }
    return 0;
}

int16_t oscSaw(uint32_t phase, uint32_t inc, uint32_t invRecip)
{
    int32_t saw = (int32_t)(phase >> 20) - 2048;
    if (inc != 0)
    {
        uint32_t invInc = (uint32_t)((1ull << 32) / inc);
        saw -= polyblepCorr(phase, invInc);
    }
    return (int16_t)clamp12(saw);
}

int16_t oscSquare(uint32_t phase)
{
    return (phase & 0x80000000u) ? (int16_t)2047 : (int16_t)-2048;
}

// pwmWidth 0..127 → duty ~3%..97% (avoids stuck rails).
int16_t oscPulse(uint32_t phase, uint8_t pwm)
{
    uint32_t width =
        (uint32_t)(((uint64_t)pwm * kPwmWidthMul) >> 32) + 0x08000000u;
    return (phase < width) ? (int16_t)2047 : (int16_t)-2048;
}

int16_t oscSine(uint32_t phase) { return g_sinLut[phase >> 24]; }

int16_t oscTriangle(uint32_t phase)
{
    uint16_t ph = (uint16_t)(phase >> 16);
    int32_t t = (ph < 0x8000) ? (((int32_t)ph << 1) - 32768)
                               : (98304 - ((int32_t)ph << 1));
    t >>= 4;
    return (int16_t)clamp12(t);
}

void tickFiltEnv(PolyVoice &v, uint8_t decayFeel)
{
    // Fast decay for acid, medium for moog/FM brightness.
    uint32_t dec = 40u + (uint32_t)decayFeel * (uint32_t)decayFeel / 2u;
    if (dec < 20)
        dec = 20;
    if (v.filtEnv <= dec)
        v.filtEnv = 0;
    else
        v.filtEnv -= dec;
}

// Chamberlin-style SVF (integer). f/q in Q15-ish units; <<7 headroom per directive.
int32_t voiceSvfLp(int32_t in, int32_t &lp, int32_t &bp, int32_t f, int32_t q)
{
    if (f < 80)
        f = 80;
    if (f > 12000)
        f = 12000;
    if (q < 2000)
        q = 2000;
    if (q > 30000)
        q = 30000;

    in <<= 7;
    lp += (f * bp) >> 15;
    int32_t hp = in - lp - ((bp * q) >> 15);
    bp += (f * hp) >> 15;

    if (lp > kSvfStateLim)
        lp = kSvfStateLim;
    if (lp < -kSvfStateLim)
        lp = -kSvfStateLim;
    if (bp > kSvfStateLim)
        bp = kSvfStateLim;
    if (bp < -kSvfStateLim)
        bp = -kSvfStateLim;
    return clamp12(lp >> 7);
}

int32_t cutoffToF(int32_t cut01_127)
{
    if (cut01_127 < 0)
        cut01_127 = 0;
    if (cut01_127 <= 127)
        return g_cutoffFLut[cut01_127];
    if (cut01_127 > 140)
        cut01_127 = 140;
    return 180 + cut01_127 * cut01_127 / 2;
}

void voiceChorusStereo(PolyVoice &v, int32_t x, int32_t &outL, int32_t &outR)
{
    v.chorusBuf[v.chorusWr] = (int16_t)clamp12(x);
    // ~0.8 Hz and ~1.1 Hz LFO pair (Juno-ish ensemble).
    v.chorusLfo += 71500u;
    v.chorusLfo2 += 98000u;
    int32_t lfoA = g_sinLut[v.chorusLfo >> 24];
    int32_t lfoB = g_sinLut[v.chorusLfo2 >> 24];
    int delayL = 48 + (((lfoA + 2048) * 90) >> 12); // ~1–3 ms
    int delayR = 64 + (((lfoB + 2048) * 100) >> 12);
    int idxL = (int)v.chorusWr + PolyVoice::kChorusLen - delayL;
    int idxR = (int)v.chorusWr + PolyVoice::kChorusLen - delayR;
    idxL &= (PolyVoice::kChorusLen - 1);
    idxR &= (PolyVoice::kChorusLen - 1);
    int32_t wetL = v.chorusBuf[idxL];
    int32_t wetR = v.chorusBuf[idxR];
    v.chorusWr = (uint16_t)((v.chorusWr + 1u) & (PolyVoice::kChorusLen - 1));
    // Mild wet — character without washing out the DCO.
    outL = clamp12((x * 3 + wetL) >> 2);
    outR = clamp12((x * 3 + wetR) >> 2);
}

uint32_t applyGlide(PolyVoice &v, uint32_t targetInc, bool enable)
{
    if (!enable)
    {
        v.glideInc = targetInc;
        return targetInc;
    }
    if (v.glideInc == 0)
    {
        v.glideInc = targetInc;
        return targetInc;
    }
    int64_t d = (int64_t)targetInc - (int64_t)v.glideInc;
    // ~slide rate — faster when far (303-ish).
    int64_t step = d >> 5;
    if (step == 0)
        step = (d > 0) ? 1 : ((d < 0) ? -1 : 0);
    v.glideInc = (uint32_t)((int64_t)v.glideInc + step);
    return v.glideInc;
}

} // namespace

void initLuts()
{
    g_midiPhaseInc[69] = (uint32_t)((440ull << 32) / 48000ull);
    for (int n = 70; n < 128; ++n)
        g_midiPhaseInc[n] =
            (uint32_t)(((uint64_t)g_midiPhaseInc[n - 1] * kSemiQ32) >> 32);
    for (int n = 68; n >= 0; --n)
        g_midiPhaseInc[n] = (uint32_t)(((uint64_t)g_midiPhaseInc[n + 1] << 32) /
                                       kSemiQ32);

    constexpr int32_t sinStepQ15 = 804;  // round(sin(2π/256) * 32768)
    constexpr int32_t cosStepQ15 = 32757; // round(cos(2π/256) * 32768)
    int32_t s = 0;
    int32_t c = 32767;
    for (int i = 0; i < 256; ++i)
    {
        g_sinLut[i] = (int16_t)((s * 2047) >> 15);
        int32_t ns = ((s * cosStepQ15) >> 15) + ((c * sinStepQ15) >> 15);
        int32_t nc = ((c * cosStepQ15) >> 15) - ((s * sinStepQ15) >> 15);
        s = ns;
        c = nc;
    }
    for (int i = 0; i < 128; ++i)
        g_cutoffFLut[i] = 180 + i * i / 2;
}

int16_t phaseSin(uint32_t phase) { return g_sinLut[phase >> 24]; }

int32_t clamp12(int32_t x)
{
    if (x < -2048)
        return -2048;
    if (x > 2047)
        return 2047;
    return x;
}

int32_t softClip12(int32_t x)
{
    if (x > 1536)
    {
        int32_t e = x - 1536;
        x = 1536 + (e >> 2);
        if (x > 2047)
            x = 2047;
    }
    else if (x < -1536)
    {
        int32_t e = x + 1536;
        x = -1536 + (e >> 2);
        if (x < -2048)
            x = -2048;
    }
    return x;
}

uint32_t noteBendIncrement(uint8_t note, int16_t bend14, uint8_t bendSemitones)
{
    int32_t note8 =
        ((int32_t)note << 8) +
        (((int32_t)bend14 * (int32_t)bendSemitones * 256) >> 13);
    if (note8 < 0)
        note8 = 0;
    if (note8 > (127 << 8))
        note8 = 127 << 8;
    int32_t n0 = note8 >> 8;
    int32_t frac = note8 & 0xFF;
    uint32_t i0 = noteIncrement(n0);
    uint32_t i1 = noteIncrement(n0 + 1);
    return i0 + (uint32_t)((((int64_t)i1 - (int64_t)i0) * frac) >> 8);
}

int16_t oscSample(uint32_t phase, uint32_t inc, uint8_t voiceType)
{
    switch (voiceType)
    {
    case 1:
        return oscSine(phase);
    case 2:
        return oscSaw(phase, inc, phaseIncRecip(inc));
    case 3:
        return oscTriangle(phase);
    case 0:
    default:
        return oscPulse(phase, g_ext.pwmWidth);
    }
}

// Basic engines 0–7 (no voice FX). Famous engines 8–12 use renderPolyVoiceAudio.
int32_t renderVoiceSample(uint32_t &phase, uint32_t &phase2, uint32_t inc,
                          uint8_t voiceType)
{
    switch (voiceType)
    {
    case 4: // dual saw, slight detune
    {
        uint32_t inc2 = inc + (inc >> 7);
        uint32_t invRecip = phaseIncRecip(inc);
        uint32_t invRecip2 = phaseIncRecip(inc2);
        phase += inc;
        phase2 += inc2;
        return ((int32_t)oscSaw(phase, inc, invRecip) +
                (int32_t)oscSaw(phase2, inc2, invRecip2)) >>
               1;
    }
    case 5: // pulse + octave-down square
    {
        phase += inc;
        phase2 += inc >> 1;
        return ((int32_t)oscPulse(phase, g_ext.pwmWidth) +
                (int32_t)oscSquare(phase2)) >>
               1;
    }
    case 6: // dual sine, mild detune
    {
        uint32_t inc2 = inc + (inc >> 8);
        phase += inc;
        phase2 += inc2;
        return ((int32_t)oscSine(phase) + (int32_t)oscSine(phase2)) >> 1;
    }
    case 7: // saw + octave-down square
    {
        uint32_t invRecip = phaseIncRecip(inc);
        phase += inc;
        phase2 += inc >> 1;
        return ((int32_t)oscSaw(phase, inc, invRecip) + (int32_t)oscSquare(phase2)) >> 1;
    }
    default:
        phase += inc;
        return oscSample(phase, inc, voiceType);
    }
}

namespace {

int32_t sampleRowWave(uint8_t row, uint32_t phase, uint32_t inc,
                      uint32_t invRecip)
{
    switch (row)
    {
    case 0:
        return oscPulse(phase, g_ext.pwmWidth);
    case 1:
        return oscSquare(phase);
    case 2:
        return oscSine(phase);
    case 3:
        return oscSaw(phase, inc, invRecip);
    case 4:
        return oscTriangle(phase);
    case 5:
        return oscPulse(phase, 19);
    case 6:
        return softClip12(oscSaw(phase, inc, invRecip));
    case 7:
        return ((int32_t)oscTriangle(phase) * 3 + (int32_t)oscSquare(phase)) >>
               2;
    default:
        return 0;
    }
}

uint32_t noiseLfsr(PolyVoice &v)
{
    uint32_t x = v.chorusLfo;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    v.chorusLfo = x;
    return x;
}

void applyOptionalTone(int32_t &s, PolyVoice &v, bool forceLp)
{
    if (!forceLp && g_ext.cutoff >= 126)
        return;
    int32_t f = cutoffToF((int32_t)g_ext.cutoff);
    s = voiceSvfLp(s, v.filtLp, v.filtBp, f, 26000);
}

void applyResonantLp(int32_t &s, PolyVoice &v)
{
    tickFiltEnv(v, g_ext.decay < 20 ? (uint8_t)40 : g_ext.decay);
    int32_t cutMod =
        (int32_t)g_ext.cutoff + (int32_t)((v.filtEnv * 90u) >> 16);
    int32_t f = cutoffToF(cutMod);
    int32_t q = 9000 - ((int32_t)g_ext.cutoff * 30);
    s = voiceSvfLp(s, v.filtLp, v.filtBp, f, q);
}

void renderFmBell(PolyVoice &v, uint8_t col, uint32_t targetInc, int32_t &s)
{
    const uint8_t cut = g_ext.cutoff;
    const uint8_t pwm = g_ext.pwmWidth;
    uint32_t inc = applyGlide(v, targetInc, col == 9);
    uint32_t ratio = 1u + ((uint32_t)pwm / 32u);
    if (col == 10)
        ratio = 2u + ((uint32_t)pwm / 16u);
    uint32_t modInc = inc * ratio;
    v.phase2 += modInc;
    int32_t mod = oscSine(v.phase2);
    tickFiltEnv(v, (uint8_t)(30 + (g_ext.decay >> 1)));
    int32_t index =
        ((int32_t)cut * 48) + (int32_t)((v.filtEnv * 80u) >> 16);
    if (col == 6)
        index = (index * 3) >> 2;
    int32_t pm = (mod * index) >> 3;
    v.phase += inc + (uint32_t)pm;
    s = oscSine(v.phase);
    if (col == 1)
    {
        v.phase3 += inc;
        s = (s + oscSine(v.phase3)) >> 1;
    }
}

void renderOrgan(PolyVoice &v, uint8_t col, uint32_t targetInc, int32_t &s)
{
    uint32_t inc = applyGlide(v, targetInc, col == 9);
    v.phase += inc;
    v.phase2 += inc >> 1;
    v.phase3 += inc >> 2;
    int32_t o =
        oscSine(v.phase) + (oscSine(v.phase2) >> 1) + (oscSine(v.phase3) >> 2);
    if (col == 5 || col == 10)
        o += oscSine(v.phase3 << 1) >> 2;
    if (col == 3 || col == 6 || col == 10)
    {
        v.chorusLfo2 += inc + (inc >> 8);
        o = (o + oscSine(v.chorusLfo2)) >> 1;
    }
    s = o >> 1;
}

void renderNoiseHybrid(PolyVoice &v, uint8_t col, uint32_t targetInc,
                       int32_t &s)
{
    uint32_t inc = applyGlide(v, targetInc, col == 9);
    v.phase += inc;
    int32_t tone = sampleRowWave(2, v.phase, inc, phaseIncRecip(inc)) >> 2;
    int32_t n = (int32_t)(noiseLfsr(v) & 4095u) - 2048;
    if (col == 7 || col == 10)
        n = voiceSvfLp(n, v.filtLp, v.filtBp, cutoffToF((int32_t)g_ext.cutoff),
                       18000);
    tickFiltEnv(v, g_ext.decay);
    if (col == 0)
        s = (tone + ((n * (int32_t)v.filtEnv) >> 16)) >> 1;
    else if (col == 4)
        s = (n >> 1) + (oscSquare(v.phase >> 1) >> 2);
    else
        s = (tone + n) >> 1;
}

void renderSyncFmCol(PolyVoice &v, uint8_t row, uint32_t inc,
                     const uint8_t pwm, int32_t &s)
{
    if (row == 2 || row == 4 || row == 7)
    {
        uint32_t ratio = 1u + ((uint32_t)pwm / 32u);
        v.phase2 += inc * ratio;
        int32_t mod = oscSine(v.phase2);
        int32_t index = ((int32_t)g_ext.cutoff * 40);
        v.phase += inc + (uint32_t)((mod * index) >> 4);
        s = sampleRowWave(row, v.phase, inc, phaseIncRecip(inc));
        return;
    }
    uint32_t ratio = 64u + (uint32_t)pwm;
    uint32_t slaveInc = (uint32_t)(((uint64_t)inc * ratio) >> 6);
    uint32_t prev = v.phase;
    v.phase += inc;
    if (v.phase < prev)
        v.phase2 = 0;
    v.phase2 += slaveInc;
    s = oscSaw(v.phase2, slaveInc, phaseIncRecip(slaveInc));
}

void renderWaveMatrix(PolyVoice &v, uint8_t row, uint8_t col,
                      uint32_t targetInc, int32_t &outL, int32_t &outR)
{
    const uint8_t cut = g_ext.cutoff;
    bool glide = (col == 9);
    uint32_t inc = applyGlide(v, targetInc, glide);
    uint32_t incDet = inc + (inc >> 7);
    uint32_t incDet2 = inc - (inc >> 8);
    uint32_t incOct = inc << 1;
    uint32_t invRecip = phaseIncRecip(inc);
    uint32_t invRecipDet = phaseIncRecip(incDet);
    uint32_t invRecipDet2 = phaseIncRecip(incDet2);
    uint32_t invRecipOct = phaseIncRecip(incOct);
    int32_t s = 0;

    if (col == 10)
    {
        renderSyncFmCol(v, row, inc, g_ext.pwmWidth, s);
        applyOptionalTone(s, v, false);
        outL = outR = s;
        return;
    }

    switch (col)
    {
    case 0:
        v.phase += inc;
        s = sampleRowWave(row, v.phase, inc, invRecip);
        applyOptionalTone(s, v, false);
        break;
    case 1:
        v.phase += inc;
        v.phase2 += inc;
        s = (sampleRowWave(row, v.phase, inc, invRecip) +
             sampleRowWave(row, v.phase2, inc, invRecip)) >>
            1;
        break;
    case 2:
        v.phase += inc;
        v.phase2 += inc;
        v.phase3 += inc;
        s = (int32_t)((sampleRowWave(row, v.phase, inc) +
                       sampleRowWave(row, v.phase2, inc) +
                       sampleRowWave(row, v.phase3, inc)) *
                      (int64_t)kThirdScale >>
                      16);
        break;
    case 3:
        v.phase += inc;
        v.phase2 += incDet;
        s = (sampleRowWave(row, v.phase, inc, invRecip) +
             sampleRowWave(row, v.phase2, incDet, invRecipDet)) >>
            1;
        break;
    case 4:
        v.phase += inc;
        v.phase2 += inc >> 1;
        s = (sampleRowWave(row, v.phase, inc, invRecip) + oscSquare(v.phase2)) >> 1;
        break;
    case 5:
        v.phase += inc;
        v.phase2 += incOct;
        s = (sampleRowWave(row, v.phase, inc, invRecip) +
             sampleRowWave(row, v.phase2, incOct, invRecipOct)) >>
            1;
        break;
    case 6:
        v.phase += inc;
        v.phase2 += incDet;
        v.phase3 += incDet2;
        s = (sampleRowWave(row, v.phase, inc, invRecip) +
             sampleRowWave(row, v.phase2, incDet, invRecipDet) +
             sampleRowWave(row, v.phase3, incDet2, invRecipDet2)) >>
            1;
        break;
    case 7:
        v.phase += inc;
        s = sampleRowWave(row, v.phase, inc, invRecip);
        applyResonantLp(s, v);
        break;
    case 8:
        v.phase += inc;
        s = sampleRowWave(row, v.phase, inc, invRecip);
        applyOptionalTone(s, v, cut < 126);
        voiceChorusStereo(v, s, outL, outR);
        return;
    case 9:
        v.phase += inc;
        s = sampleRowWave(row, v.phase, inc, invRecip);
        break;
    default:
        v.phase += inc;
        s = sampleRowWave(row, v.phase, inc, invRecip);
        break;
    }

    outL = outR = s;
}

void renderVoiceMatrixBody(PolyVoice &v, uint32_t targetInc, uint8_t row,
                           uint8_t col, int32_t &outL, int32_t &outR)
{
    if (row >= kVoiceMatrixRows)
        row = 0;
    if (col >= kVoiceMatrixCols)
        col = 0;

    if (row == 8)
    {
        int32_t s = 0;
        renderFmBell(v, col, targetInc, s);
        if (col == 7)
            applyResonantLp(s, v);
        else if (col == 8)
        {
            voiceChorusStereo(v, s, outL, outR);
            return;
        }
        outL = outR = s;
        return;
    }
    if (row == 9)
    {
        int32_t s = 0;
        renderOrgan(v, col, targetInc, s);
        if (col == 7)
            applyOptionalTone(s, v, true);
        else if (col == 8)
        {
            voiceChorusStereo(v, s, outL, outR);
            return;
        }
        outL = outR = s;
        return;
    }
    if (row == 10)
    {
        int32_t s = 0;
        renderNoiseHybrid(v, col, targetInc, s);
        if (col == 8)
        {
            voiceChorusStereo(v, s, outL, outR);
            return;
        }
        outL = outR = s;
        return;
    }

    renderWaveMatrix(v, row, col, targetInc, outL, outR);
}

} // namespace

void renderVoiceMatrix(PolyVoice &v, uint32_t targetInc, uint8_t row,
                       uint8_t col, int32_t &outL, int32_t &outR)
{
    renderVoiceMatrixBody(v, targetInc, row, col, outL, outR);
}

void renderPolyVoiceAudio(PolyVoice &v, uint32_t targetInc, uint8_t voiceId,
                          int32_t &outL, int32_t &outR)
{
    VoiceMatrixCoord c = decodeVoiceMatrix(voiceId);
    renderVoiceMatrix(v, targetInc, c.row, c.col, outL, outR);
}

int32_t applyCutoff(int32_t x, int32_t &state)
{
    if (g_ext.cutoff >= 126)
    {
        state = x << 7;
        return x;
    }
    int32_t c = 280 + ((int32_t)g_ext.cutoff * (int32_t)g_ext.cutoff * 2);
    if (c > 32767)
        c = 32767;
    int32_t x7 = x << 7;
    state += ((x7 - state) * c) >> 15;
    return state >> 7;
}
