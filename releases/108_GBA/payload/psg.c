// psg.c — GBA PSG register access. See psg.h for the rules that matter.

#include "psg.h"

#define REG_BASE 0x04000000
#define R16(off) (*(volatile uint16_t *)(REG_BASE + (off)))

#define REG_SOUND1CNT_L R16(0x0060)   // sweep
#define REG_SOUND1CNT_H R16(0x0062)   // duty / length / envelope
#define REG_SOUND1CNT_X R16(0x0064)   // frequency / trigger
#define REG_SOUND2CNT_L R16(0x0068)   // duty / length / envelope
#define REG_SOUND2CNT_H R16(0x006C)   // frequency / trigger
#define REG_SOUND3CNT_L R16(0x0070)   // wave dimension / bank / enable
#define REG_SOUND3CNT_H R16(0x0072)   // length / volume
#define REG_SOUND3CNT_X R16(0x0074)   // rate / trigger
#define REG_SOUND4CNT_L R16(0x0078)   // length / envelope
#define REG_SOUND4CNT_H R16(0x007C)   // divisor / width / shift / trigger
#define REG_SOUNDCNT_L  R16(0x0080)   // master volume + per-channel pan
#define REG_SOUNDCNT_H  R16(0x0082)   // PSG:DMA mix ratio
#define REG_SOUNDCNT_X  R16(0x0084)   // master enable
#define REG_SOUNDBIAS   R16(0x0088)

#define WAVE_RAM ((volatile uint16_t *)(REG_BASE + 0x0090))

#define TRIGGER (1 << 15)

// ---- built-in channel-3 wavetables ----
// 32 four-bit samples packed two per byte, HIGH nybble played first.
const uint8_t psg_wave_preset[PSG_WAVE_PRESETS][16] = {
    // Sine-ish
    { 0x89, 0xAB, 0xCD, 0xEE, 0xFF, 0xEE, 0xDC, 0xBA,
      0x98, 0x76, 0x54, 0x32, 0x11, 0x00, 0x11, 0x23 },
    // Triangle
    { 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xDC, 0xBA, 0x98,
      0x76, 0x54, 0x32, 0x10, 0x01, 0x23, 0x45, 0x67 },
    // Saw up
    { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
      0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF },
    // Saw down
    { 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
      0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10 },
    // Square
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    // Narrow pulse
    { 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    // Quarter pulse
    { 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    // Organ: two stacked octaves
    { 0x8B, 0xDE, 0xFE, 0xDB, 0x85, 0x21, 0x01, 0x25,
      0x8B, 0xDE, 0xFE, 0xDB, 0x85, 0x21, 0x01, 0x25 },
    // Half sine, one lobe then silence — hollow and clarinet-ish
    { 0x8A, 0xCD, 0xEF, 0xFE, 0xDC, 0xA8, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    // Bell: a strong upper partial over the fundamental
    { 0x8C, 0xFC, 0x8C, 0xF8, 0x4C, 0xF8, 0x48, 0xC4,
      0x84, 0xC0, 0x40, 0x84, 0x08, 0x40, 0x04, 0x48 },
    // Vox: formant-ish double bump
    { 0x9C, 0xEF, 0xEC, 0x9A, 0xBD, 0xDB, 0x86, 0x43,
      0x34, 0x56, 0x54, 0x32, 0x23, 0x45, 0x43, 0x21 },
    // Steps: a four-level staircase, buzzy and very chip
    { 0x00, 0x00, 0x55, 0x55, 0xAA, 0xAA, 0xFF, 0xFF,
      0xFF, 0xFF, 0xAA, 0xAA, 0x55, 0x55, 0x00, 0x00 },
};

const char *psg_wave_name[PSG_WAVE_PRESETS] = {
    "SINE", "TRI", "SAW UP", "SAW DN", "SQUARE", "PULSE 12",
    "PULSE 25", "ORGAN", "HALF SIN", "BELL", "VOX", "STEPS"
};

void psg_init(void)
{
    // MASTER ENABLE FIRST. Every other write below is a no-op until this is set.
    REG_SOUNDCNT_X = 0x0080;

    REG_SOUNDBIAS  = 0x0200;          // 9-bit / 32.768 kHz, the BIOS default made explicit
    REG_SOUNDCNT_H = 0x0002;          // PSG at 100%, no DMA sound
    REG_SOUNDCNT_L = 0;               // synth.c sets volume and pan

    REG_SOUND1CNT_L = 0;              // sweep off
    REG_SOUND1CNT_H = 0;
    REG_SOUND1CNT_X = 0;
    REG_SOUND2CNT_L = 0;
    REG_SOUND2CNT_H = 0;
    REG_SOUND3CNT_L = 0;
    REG_SOUND3CNT_H = 0;
    REG_SOUND3CNT_X = 0;
    REG_SOUND4CNT_L = 0;
    REG_SOUND4CNT_H = 0;

    psg_wave_load(psg_wave_preset[0]);
}

void psg_master(uint8_t volL, uint8_t volR, uint8_t ratio)
{
    uint16_t cnt = REG_SOUNDCNT_L;
    cnt = (uint16_t)((cnt & 0xFF00u) | ((volL & 7u) << 4) | (volR & 7u));
    REG_SOUNDCNT_L = cnt;
    REG_SOUNDCNT_H = (uint16_t)((REG_SOUNDCNT_H & ~0x3u) | (ratio & 0x3u));
}

void psg_enable(uint8_t maskL, uint8_t maskR)
{
    uint16_t cnt = REG_SOUNDCNT_L;
    cnt = (uint16_t)((cnt & 0x00FFu) | ((maskL & 0xFu) << 12) | ((maskR & 0xFu) << 8));
    REG_SOUNDCNT_L = cnt;
}

// ---- squares ----
// Envelope step is parked at 0 (no hardware sweep of volume) and direction at 0, so the value
// written here IS the instantaneous level. Length is left disabled so the note sustains until
// we say otherwise.
//
// A BARE ZERO IN NRx2 IS HOW YOU SILENCE A PSG CHANNEL. Do not "improve" it.
//
// The channel DAC maps digital 0 to a RAIL, not to the centre, so volume 0 with the DAC still
// running is a DC offset rather than silence - and since a volume only reaches the channel on a
// trigger, the channel also carries on sounding at its previous level until something triggers
// it. Writing a plain zero switches the DAC off instead, which disables the channel and is the
// only thing that gives true silence. Every Game Boy engine mutes this way.
//
// The cost is the rule below it, and it is the whole reason this function's callers are ordered
// the way they are: turning the DAC back on does NOT re-enable the channel, only a trigger does.
// So synth_tick() writes every volume first and triggers afterwards, at the very end.
//
// (An earlier attempt held the DAC alive by setting the envelope direction bit at volume 0. It
// left both squares permanently audible - the step to zero is the one step that never earns a
// trigger - and toggling that bit is itself a zombie-mode event that recomputes the volume as
// 16 - volume. Two faults for no gain.)
//
// Envelope step is parked at 0, so the hardware envelope never runs and the value written here
// IS the instantaneous level. Length is left disabled so a note sustains until we say otherwise.
void psg_sq_voice(int ch, uint8_t duty, uint8_t vol)
{
    uint16_t v = (uint16_t)(((vol & 0xFu) << 12) | ((duty & 0x3u) << 6));
    if (ch == PSG_CH1) REG_SOUND1CNT_H = v;
    else               REG_SOUND2CNT_L = v;
}

void psg_sq_period(int ch, uint16_t period)
{
    if (ch == PSG_CH1) REG_SOUND1CNT_X = (uint16_t)(period & 0x7FFu);
    else               REG_SOUND2CNT_H = (uint16_t)(period & 0x7FFu);
}

void psg_sq_trigger(int ch, uint16_t period)
{
    if (ch == PSG_CH1) REG_SOUND1CNT_X = (uint16_t)((period & 0x7FFu) | TRIGGER);
    else               REG_SOUND2CNT_H = (uint16_t)((period & 0x7FFu) | TRIGGER);
}

void psg_sq_sweep(uint8_t shift, uint8_t dir, uint8_t time)
{
    REG_SOUND1CNT_L = (uint16_t)((shift & 7u) | ((dir & 1u) << 3) | ((time & 7u) << 4));
}

// ---- channel 3, wave ----
// SOUND3CNT_L bit 6 selects the bank that PLAYS; the CPU sees the OTHER one. So we write the
// idle bank and then flip, which also means the swap is glitch-free.
void psg_wave_load(const uint8_t *wave16)
{
    uint16_t cnt  = REG_SOUND3CNT_L;
    uint16_t bank = (uint16_t)((cnt >> 6) & 1u);

    for (int i = 0; i < 8; i++) {
        WAVE_RAM[i] = (uint16_t)(wave16[i * 2] | (wave16[i * 2 + 1] << 8));
    }

    // Play the bank we just filled, keep 32-step (one bank) mode, DAC on.
    REG_SOUND3CNT_L = (uint16_t)((cnt & ~((1u << 5) | (1u << 6))) | ((bank ^ 1u) << 6) | 0x80u);
}

// Scale one nybble toward silence, WITHOUT LEAVING A DC OFFSET.
//
// The wave DAC's centre sits between samples 7 and 8, not on 8. Scaling toward 8 therefore left
// every quiet table half a step above centre - a fixed DC offset that did not shrink as the
// envelope did. The channel then sounded silent long before it was silent, and the mute at the
// very bottom removed the offset in one step: a click, arriving a second or so after the note
// had apparently already stopped.
//
// So scale about 7.5 instead and split the half-step between neighbouring samples - even samples
// round down, odd ones round up. A fully faded table is 7,8,7,8..., whose mean is exactly the
// DAC centre and whose only content is at half the wave clock, far above hearing and filtered by
// SOUNDBIAS anyway. Silence is now genuinely silent and there is nothing left to click.
static uint8_t scale_nib(uint8_t v, uint8_t amp, int odd)
{
    int d2 = ((int)v * 2 - 15) * (int)amp / 15;   // deviation from 7.5, doubled. Constant divisor.
    int o  = (15 + d2 + (odd ? 1 : 0)) / 2;       // back to a nybble, rounding alternately
    return (uint8_t)(o < 0 ? 0 : (o > 15 ? 15 : o));
}

void psg_wave_load_scaled(const uint8_t *wave16, uint8_t amp)
{
    uint16_t cnt  = REG_SOUND3CNT_L;
    uint16_t bank = (uint16_t)((cnt >> 6) & 1u);

    for (int i = 0; i < 8; i++) {
        uint8_t b0 = wave16[i * 2], b1 = wave16[i * 2 + 1];
        // High nybble is the earlier sample, so within every byte the parity runs even, odd.
        uint8_t o0 = (uint8_t)((scale_nib((uint8_t)(b0 >> 4), amp, 0) << 4)
                             |  scale_nib((uint8_t)(b0 & 0xF), amp, 1));
        uint8_t o1 = (uint8_t)((scale_nib((uint8_t)(b1 >> 4), amp, 0) << 4)
                             |  scale_nib((uint8_t)(b1 & 0xF), amp, 1));
        WAVE_RAM[i] = (uint16_t)(o0 | (o1 << 8));
    }

    REG_SOUND3CNT_L = (uint16_t)((cnt & ~((1u << 5) | (1u << 6))) | ((bank ^ 1u) << 6) | 0x80u);
}

void psg_wave_voice(uint8_t volSel)
{
    REG_SOUND3CNT_L = (uint16_t)(REG_SOUND3CNT_L | 0x80u);        // DAC on
    REG_SOUND3CNT_H = (uint16_t)((volSel & 0x3u) << 13);
}

void psg_wave_period(uint16_t period) { REG_SOUND3CNT_X = (uint16_t)(period & 0x7FFu); }
void psg_wave_trigger(uint16_t period) { REG_SOUND3CNT_X = (uint16_t)((period & 0x7FFu) | TRIGGER); }

// ---- channel 4, noise ----
void psg_noise_voice(uint8_t vol)
{
    // A bare zero at volume 0, for the reason spelled out over psg_sq_voice.
    REG_SOUND4CNT_L = (uint16_t)((vol & 0xFu) << 12);
}

static uint8_t g_noiseCtl = 0;

void psg_noise_set(uint8_t divisor, uint8_t shift, uint8_t width)
{
    g_noiseCtl = (uint8_t)((divisor & 0x7u) | ((width & 1u) << 3) | ((shift & 0xFu) << 4));
    REG_SOUND4CNT_H = g_noiseCtl;
}

void psg_noise_trigger(void)
{
    REG_SOUND4CNT_H = (uint16_t)(g_noiseCtl | TRIGGER);
}
