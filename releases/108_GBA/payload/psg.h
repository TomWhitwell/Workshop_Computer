// psg.h — the GBA's four PSG channels, as a small register abstraction.
//
// Register map and formulas per GBATEK "GBA Sound Controller":
//   ch1/ch2 square   f = 131072 / (2048 - n)
//   ch3 wave         f =  65536 / (2048 - n)   (one octave below the squares for the same n)
//   ch4 noise        f = 524288 / r / 2^(s+1)
//
// THE ORDERING RULE THAT CAUSES SILENT FAILURES: SOUNDCNT_X bit 7 (master enable) must be set
// BEFORE any other sound register is written. With the master disabled the sound registers
// are not writable at all, and clearing bit 7 resets them. psg_init() does this first.
//
// ENVELOPES ARE DONE IN SOFTWARE, not by the hardware envelope generator. The GB/GBA envelope
// runs once per trigger and cannot sustain-then-release, which is precisely the shape a gate
// input needs. So the hardware envelope is parked (step 0, constant volume) and synth.c writes
// the volume nibble every control tick instead. This is what tracker engines on this hardware
// do; it is only a 16-bit store per channel per tick.
#pragma once

#include <stdint.h>

#define PSG_CH1 0
#define PSG_CH2 1
#define PSG_CH3 2
#define PSG_CH4 3

// Duty values for the square channels.
#define PSG_DUTY_12 0
#define PSG_DUTY_25 1
#define PSG_DUTY_50 2
#define PSG_DUTY_75 3

// Channel 3 volume selector (SOUND3CNT_H bits 14:13).
#define PSG_WAVE_MUTE 0
#define PSG_WAVE_100  1
#define PSG_WAVE_50   2
#define PSG_WAVE_25   3

void psg_init(void);

// Master volume 0..7 per side; ratio 0 = 25%, 1 = 50%, 2 = 100%.
void psg_master(uint8_t volL, uint8_t volR, uint8_t ratio);
// One bit per channel, per side.
void psg_enable(uint8_t maskL, uint8_t maskR);

// Squares. Setting the period does NOT retrigger — that separation is essential, because the
// pitch register is rewritten a thousand times a second and a trigger on each write would
// restart the waveform phase continuously and the note would never sound settled.
void psg_sq_voice(int ch, uint8_t duty, uint8_t vol);   // vol 0..15
void psg_sq_period(int ch, uint16_t period);
void psg_sq_trigger(int ch, uint16_t period);
// Channel 1 only. The one hardware modulator worth keeping: it sweeps faster than our 1 kHz
// control tick can.
void psg_sq_sweep(uint8_t shift, uint8_t dir, uint8_t time);

// Channel 3, wave. `wave16` is 16 bytes = 32 nybble samples, high nybble first.
void psg_wave_load(const uint8_t *wave16);

// Load a wavetable scaled about its centre, `amp` 0..15.
//
// SOUND3CNT_H gives the wave channel only four volume steps (mute / 25 / 50 / 100 %), which is
// far too coarse for a drum's decay. Scaling the SAMPLES instead gives sixteen, and costs eight
// halfword writes — the trick every Game Boy tracker uses on this channel. The write goes to the
// idle bank and then flips, so the amplitude change is glitch-free.
void psg_wave_load_scaled(const uint8_t *wave16, uint8_t amp);
void psg_wave_voice(uint8_t volSel);
void psg_wave_period(uint16_t period);
void psg_wave_trigger(uint16_t period);

// Channel 4, noise. width 0 = 15-bit (hiss), 1 = 7-bit (metallic).
void psg_noise_voice(uint8_t vol);
void psg_noise_set(uint8_t divisor, uint8_t shift, uint8_t width);
void psg_noise_trigger(void);

// Built-in channel-3 wavetables, indexed by patch waveSel.
#define PSG_WAVE_PRESETS 12
extern const uint8_t psg_wave_preset[PSG_WAVE_PRESETS][16];
extern const char   *psg_wave_name[PSG_WAVE_PRESETS];
