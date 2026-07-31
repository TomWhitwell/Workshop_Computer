//
// Copyright(C) 2026 ws-doom contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// DESCRIPTION:
//   Workshop System Computer backend: audio DAC, panel input scanning,
//   CV / pulse / LED outputs, game-state taps.
//

#include "config.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/audio.h"

#include "ws_hw.h"

#include "doomtype.h"
#include "d_event.h"
#include "doomkeys.h"
#include "doomstat.h"
#include "d_items.h"
#include "doom/sounds.h"
#include "tables.h"

// ---------------------------------------------------------------- audio

#define MCP4822_CFG      0x3000u  // gain 1x, active
#define MCP4822_CHAN_A   0x0000u
#define MCP4822_CHAN_B   0x8000u

static struct audio_buffer_pool *ws_pool;
static audio_buffer_t *ws_cur_buf;
static uint32_t ws_cur_pos;

// The pool's built-in default connection routes through
// connection->producer_pool, which is only assigned by
// audio_complete_connection (normally done by the I2S driver we removed).
// Left unwired, the mixer's first take_audio_buffer dereferences NULL and
// spins forever on a garbage lock with interrupts off. Wire the producer
// side straight to our pool; the DAC interrupt consumes with the
// get_full/queue_free calls directly.
static audio_buffer_t *ws_conn_take(audio_connection_t *connection, bool block) {
    (void) connection;
    return get_free_audio_buffer(ws_pool, block);
}

static void ws_conn_give(audio_connection_t *connection, audio_buffer_t *buffer) {
    (void) connection;
    queue_full_audio_buffer(ws_pool, buffer);
}

static audio_connection_t ws_connection = {
    .producer_pool_take = ws_conn_take,
    .producer_pool_give = ws_conn_give,
};

// counteract the module's inverting output op-amps (as ComputerCard does)
static inline uint16_t ws_dac_word(uint16_t chan, int32_t val) {
    if (val < -2048) val = -2048;
    if (val > 2047) val = 2047;
    return (chan | MCP4822_CFG) | (uint16_t)((-val + 0x800) & 0x0FFF);
}

// ---- 48kHz engine, modeled on Chris Johnson's ComputerCard: the ADC
// free-runs round robin over audio in R/L and the two mux lines at
// 384kHz; DMA collects blocks of 8 (= one audio frame) and its interrupt
// is the sample clock. It advances the input mux (knobs/switch/CVs),
// runs the normalisation probe for jack detection, mixes the output
// sample and hands it to a second DMA feeding the MCP4822.

static uint16_t ws_adc_correct(uint16_t value);

// panel state, updated by the engine interrupt
static volatile int32_t knob_main = 2048, knob_x = 2048, knob_y = 2048, switch_raw = 2048;
static volatile int32_t cv_in[2];

static uint16_t ws_adc_buf[2][8];
static uint16_t ws_spi_buf[2][2];
static uint8_t ws_adc_dma, ws_spi_dma;
static uint8_t ws_dma_phase;

// input jack indices (ComputerCard order)
enum { WS_IN_AUDIO1, WS_IN_AUDIO2, WS_IN_CV1, WS_IN_CV2, WS_IN_PULSE1, WS_IN_PULSE2 };
static volatile bool ws_in_connected[6];
static volatile int16_t ws_audio_in[2]; // L, R (post-notch, 0 if unpatched)

// audio-in 2 gate envelope, retriggered by player gunshots
static volatile int32_t ws_in2_gate;

// user mix settings (persisted; see settings section)
volatile uint8_t ws_music_vol = 90;  // base music level, 128 = old default
volatile uint8_t ws_sfx_vol = 255;
volatile uint8_t ws_turn_sens = 128; // 128 = nominal response, 255 = 2x
volatile uint8_t ws_move_sens = 128;

extern volatile int16_t ws_music_gain[2]; // defined in the beacon section

// 12kHz notch to remove mux interference from the audio inputs
typedef struct { int32_t mix1, mix2, mixf1, mixf2; } ws_notch_t;
static ws_notch_t ws_notch_l, ws_notch_r;
static inline int32_t ws_notch(ws_notch_t *n, int32_t val) {
    const int32_t ooa0 = 16302, a2oa0 = 16221; // Q=100 notch at 12kHz
    int32_t mixf = (ooa0 * (val + n->mix2) - a2oa0 * n->mixf2) >> 14;
    n->mix2 = n->mix1;
    n->mix1 = val;
    n->mixf2 = n->mixf1;
    n->mixf1 = mixf;
    return mixf;
}

// pseudo-random bit for the normalisation probe
static inline uint32_t ws_next_probe(void) {
    static uint32_t lcg = 1;
    lcg = 1664525 * lcg + 1013904223;
    return lcg >> 31;
}

static void __not_in_flash_func(ws_engine_irq)(void) {
    static int mux_state;
    static int norm_count;
    static uint32_t np;
    static uint32_t plug_state[6];
    static int32_t knob_sm[4];
    static int32_t cv_sm[2];

    adc_select_input(0);

    int next_mux = (mux_state + 1) & 3;
    gpio_put(WS_MUX_A_PIN, next_mux & 1);
    gpio_put(WS_MUX_B_PIN, next_mux & 2);

    uint8_t cpu_phase = ws_dma_phase;
    ws_dma_phase ^= 1;
    dma_hw->ints1 = 1u << ws_adc_dma;
    dma_channel_set_write_addr(ws_adc_dma, ws_adc_buf[ws_dma_phase], true);
    dma_channel_set_read_addr(ws_spi_dma, ws_spi_buf[ws_dma_phase], true);

    uint16_t *b = ws_adc_buf[cpu_phase];
    b[0] = ws_adc_correct(b[0]);
    b[1] = ws_adc_correct(b[1]);
    b[4] = ws_adc_correct(b[4]);
    b[5] = ws_adc_correct(b[5]);
    b[6] = ws_adc_correct(b[6]);
    b[7] = ws_adc_correct(b[7]);

    // CV inputs (~240Hz LPF), alternating with the mux
    int cvi = mux_state & 1;
    cv_sm[cvi] = (15 * cv_sm[cvi] + 16 * b[7]) >> 4;
    cv_in[cvi] = 2048 - (cv_sm[cvi] >> 4);

    // audio inputs: average both samples, undo the inverting input stage
    int32_t in_r = -(((int32_t)(b[0] + b[4]) - 0x1000) >> 1);
    int32_t in_l = -(((int32_t)(b[1] + b[5]) - 0x1000) >> 1);
    in_r = ws_notch(&ws_notch_r, in_r);
    in_l = ws_notch(&ws_notch_l, in_l);

    // knobs / switch (each sampled at 12kHz, ~60Hz LPF)
    int k = mux_state;
    knob_sm[k] = (127 * knob_sm[k] + 16 * b[6]) >> 7;
    int32_t kv = knob_sm[k] >> 4;
    switch (k) {
        case 0: knob_main = kv; break;
        case 1: knob_x = kv; break;
        case 2: knob_y = kv; break;
        default: switch_raw = kv; break;
    }

    // normalisation probe: unpatched jacks follow the probe bit stream
    if (norm_count == 0) {
        uint32_t p = ws_next_probe();
        gpio_put(WS_NORM_PROBE_PIN, p);
        np = (np << 1) | p;
    }
    if (norm_count == 14 || norm_count == 15) {
        plug_state[WS_IN_CV1 + cvi] = (plug_state[WS_IN_CV1 + cvi] << 1) | (b[7] < 1800);
    }
    if (norm_count == 15) {
        plug_state[WS_IN_AUDIO1] = (plug_state[WS_IN_AUDIO1] << 1) | (b[5] < 1800);
        plug_state[WS_IN_AUDIO2] = (plug_state[WS_IN_AUDIO2] << 1) | (b[4] < 1800);
        plug_state[WS_IN_PULSE1] = (plug_state[WS_IN_PULSE1] << 1) | (!gpio_get(WS_PULSE1_IN_PIN) ? 1 : 0);
        plug_state[WS_IN_PULSE2] = (plug_state[WS_IN_PULSE2] << 1) | (!gpio_get(WS_PULSE2_IN_PIN) ? 1 : 0);
        for (int i = 0; i < 6; i++) {
            ws_in_connected[i] = (np != plug_state[i]);
        }
    }
    if (!ws_in_connected[WS_IN_AUDIO1]) in_l = 0;
    if (!ws_in_connected[WS_IN_AUDIO2]) in_r = 0;
    if (!ws_in_connected[WS_IN_CV1]) cv_in[0] = 0;
    if (!ws_in_connected[WS_IN_CV2]) cv_in[1] = 0;
    ws_audio_in[0] = (int16_t) in_l;
    ws_audio_in[1] = (int16_t) in_r;

    // ---- output mix: doom buffer (sfx + internal music) ...
    int32_t l = 0, r = 0;
    if (!ws_cur_buf && ws_pool) {
        ws_cur_buf = get_full_audio_buffer(ws_pool, false);
        ws_cur_pos = 0;
    }
    if (ws_cur_buf) {
        const int16_t *s = (const int16_t *) ws_cur_buf->buffer->bytes;
        l = s[ws_cur_pos * 2];
        r = s[ws_cur_pos * 2 + 1];
        if (++ws_cur_pos >= ws_cur_buf->sample_count) {
            queue_free_audio_buffer(ws_pool, ws_cur_buf);
            ws_cur_buf = NULL;
        }
    }
    // ... plus audio in 1 as external music, panned by the beacon
    // (>>5 keeps headroom: hot pan gains would clip at >>4)
    if (ws_in_connected[WS_IN_AUDIO1]) {
        l += (in_l * ws_music_gain[0]) >> 5;
        r += (in_l * ws_music_gain[1]) >> 5;
    }
    // ... plus audio in 2, gated open by each gunshot, tracking the
    // effects volume (it stands in for the shot sound when patched)
    int32_t gate = ws_in2_gate;
    if (ws_in_connected[WS_IN_AUDIO2] && gate > 0) {
        ws_in2_gate = gate - 2; // ~680ms decay
        int32_t v = (in_r * (gate >> 8)) >> 5;
        v = (v * ws_sfx_vol) >> 8;
        l += v;
        r += v;
    }
    if (l > 32767) l = 32767;
    if (l < -32768) l = -32768;
    if (r > 32767) r = 32767;
    if (r < -32768) r = -32768;
    ws_spi_buf[cpu_phase][0] = ws_dac_word(MCP4822_CHAN_A, l >> 4);
    ws_spi_buf[cpu_phase][1] = ws_dac_word(MCP4822_CHAN_B, r >> 4);

    mux_state = next_mux;
    norm_count = (norm_count + 1) & 15;
}

bool ws_ext_music(void) {
    return ws_in_connected[WS_IN_AUDIO1];
}

void ws_audio_init(struct audio_buffer_pool *pool) {
    ws_pool = pool;
    pool->connection = &ws_connection;

    adc_select_input(0);
    adc_set_round_robin(0b1111);
    adc_fifo_setup(true, true, 1, false, false);
    adc_set_clkdiv(124); // 48MHz / 125 = 384kHz = 8 x 48kHz

    ws_adc_dma = dma_claim_unused_channel(true);
    ws_spi_dma = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(ws_adc_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_ADC);
    dma_channel_configure(ws_adc_dma, &c, ws_adc_buf[0], &adc_hw->fifo, 8, true);

    // DMA_IRQ_1: something in the doom runtime already owns DMA_IRQ_0's
    // vector, and registering there hard-asserts
    dma_channel_set_irq1_enabled(ws_adc_dma, true);
    irq_set_exclusive_handler(DMA_IRQ_1, ws_engine_irq);
    // highest priority: a late interrupt overflows the 4-deep ADC FIFO
    // and slips the channel alignment (audible as crackle)
    irq_set_priority(DMA_IRQ_1, 0x00);
    irq_set_enabled(DMA_IRQ_1, true);

    dma_channel_config s = dma_channel_get_default_config(ws_spi_dma);
    channel_config_set_transfer_data_size(&s, DMA_SIZE_16);
    channel_config_set_dreq(&s, DREQ_SPI0_TX);
    dma_channel_configure(ws_spi_dma, &s, &spi_get_hw(spi0)->dr, NULL, 2, false);

    adc_run(true);
}

// ---------------------------------------------------------------- init

void ws_hw_early_init(void) {
    // the peripheral clock follows the 270MHz system clock by default,
    // which is far out of spec for the PL022 SPI block; run it from the
    // 48MHz USB PLL instead (SPI DAC then clocks at 12MHz, UART recalcs)
    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    48 * MHZ, 48 * MHZ);

    // pulse outputs (inverted drivers: raw high = jack low)
    gpio_init(WS_PULSE1_OUT_PIN);
    gpio_set_dir(WS_PULSE1_OUT_PIN, GPIO_OUT);
    gpio_put(WS_PULSE1_OUT_PIN, true);
    gpio_init(WS_PULSE2_OUT_PIN);
    gpio_set_dir(WS_PULSE2_OUT_PIN, GPIO_OUT);
    gpio_put(WS_PULSE2_OUT_PIN, true);

    // pulse inputs (inverted; pull-up feeds the input transistor)
    gpio_init(WS_PULSE1_IN_PIN);
    gpio_set_dir(WS_PULSE1_IN_PIN, GPIO_IN);
    gpio_pull_up(WS_PULSE1_IN_PIN);
    gpio_init(WS_PULSE2_IN_PIN);
    gpio_set_dir(WS_PULSE2_IN_PIN, GPIO_IN);
    gpio_pull_up(WS_PULSE2_IN_PIN);

    // normalisation probe held low so unpatched inputs sit near 0V
    gpio_init(WS_NORM_PROBE_PIN);
    gpio_set_dir(WS_NORM_PROBE_PIN, GPIO_OUT);
    gpio_put(WS_NORM_PROBE_PIN, false);

    // LEDs: 16-bit PWM, pins paired per slice
    for (int i = 0; i < 6; i += 2) {
        uint pin = WS_LED_FIRST_PIN + i;
        gpio_set_function(pin, GPIO_FUNC_PWM);
        gpio_set_function(pin + 1, GPIO_FUNC_PWM);
        pwm_config c = pwm_get_default_config();
        pwm_config_set_wrap(&c, 65535);
        pwm_init(pwm_gpio_to_slice_num(pin), &c, true);
        pwm_set_gpio_level(pin, 0);
        pwm_set_gpio_level(pin + 1, 0);
    }
    // boot diagnostic ladder, stage 2: survived the 270MHz clock raise.
    // LEDs 0+1 on until the game loop's first tic takes over.
    pwm_set_gpio_level(WS_LED_FIRST_PIN, 1200 * 1200 >> 8);
    pwm_set_gpio_level(WS_LED_FIRST_PIN + 1, 1200 * 1200 >> 8);

    // CV outs: ~11-bit PWM into the on-board RC filters
    gpio_set_function(WS_CV1_OUT_PIN, GPIO_FUNC_PWM);
    gpio_set_function(WS_CV2_OUT_PIN, GPIO_FUNC_PWM);
    {
        pwm_config c = pwm_get_default_config();
        pwm_config_set_wrap(&c, 1999);
        pwm_init(pwm_gpio_to_slice_num(WS_CV1_OUT_PIN), &c, true);
    }
    pwm_set_gpio_level(WS_CV1_OUT_PIN, 1000); // ~0V
    pwm_set_gpio_level(WS_CV2_OUT_PIN, 1000);

    // audio ins + knobs/CVs (via 4051 mux) share the ADC round robin
    adc_init();
    adc_gpio_init(WS_ADC_AUDIO_R_PIN);
    adc_gpio_init(WS_ADC_AUDIO_L_PIN);
    adc_gpio_init(WS_ADC_MUX_KNOBS);
    adc_gpio_init(WS_ADC_MUX_CV);
    gpio_init(WS_MUX_A_PIN);
    gpio_set_dir(WS_MUX_A_PIN, GPIO_OUT);
    gpio_init(WS_MUX_B_PIN);
    gpio_set_dir(WS_MUX_B_PIN, GPIO_OUT);

    // MCP4822 on spi0, 16-bit frames, hardware CS
    spi_init(spi0, 15625000);
    spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(WS_DAC_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(WS_DAC_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(WS_DAC_CS_PIN, GPIO_FUNC_SPI);

    ws_settings_load();
}

// ---------------------------------------------------------------- misc

// pick a start level with entropy from boot timing and knob noise
int ws_random_map(void) {
    uint32_t e = time_us_32();
    e ^= (uint32_t) knob_main << 7;
    e ^= (uint32_t) knob_x << 13;
    e ^= (uint32_t) knob_y << 19;
    e = e * 1664525u + 1013904223u;
    return 1 + (int) ((e >> 8) % 9);
}

// ---------------------------------------------------------------- settings

// persisted in the spare flash sector between the code and the WAD
#define WS_SETTINGS_OFFS 0x47000
#define WS_SETTINGS_MAGIC 0x4D445357u // "WSDM"

typedef struct {
    uint32_t magic;
    uint8_t music_vol;
    uint8_t sfx_vol;
    uint8_t turn_sens;
    uint8_t move_sens;
    uint8_t csum; // guards against torn writes
    uint8_t pad[3];
} ws_settings_t;

static bool ws_save_requested;

static uint8_t ws_settings_csum(uint8_t music, uint8_t sfx,
                                uint8_t turn, uint8_t move) {
    return (uint8_t) (music ^ sfx ^ turn ^ move ^ 0x5A);
}

static ws_settings_t ws_settings_snapshot(void) {
    ws_settings_t s = {
        WS_SETTINGS_MAGIC, ws_music_vol, ws_sfx_vol, ws_turn_sens, ws_move_sens,
        ws_settings_csum(ws_music_vol, ws_sfx_vol, ws_turn_sens, ws_move_sens),
        {0xFF, 0xFF, 0xFF},
    };
    return s;
}

void ws_settings_load(void) {
    const ws_settings_t *s = (const ws_settings_t *) (XIP_BASE + WS_SETTINGS_OFFS);
    // an erased sector reads 0xFF everywhere, so the magic check alone
    // rejects "empty"; the checksum also rejects interrupted writes (and
    // records saved by older layouts, which then fall back to defaults)
    if (s->magic == WS_SETTINGS_MAGIC &&
        s->csum == ws_settings_csum(s->music_vol, s->sfx_vol,
                                    s->turn_sens, s->move_sens)) {
        ws_music_vol = s->music_vol;
        ws_sfx_vol = s->sfx_vol;
        ws_turn_sens = s->turn_sens;
        ws_move_sens = s->move_sens;
    }
}

void ws_settings_set(int which, uint8_t value) {
    // live change only; flash is written on explicit request
    switch (which) {
        case 0: ws_music_vol = value; break;
        case 1: ws_sfx_vol = value; break;
        case 2: ws_turn_sens = value; break;
        case 3: ws_move_sens = value; break;
    }
}

void ws_settings_request_save(void) {
    ws_save_requested = true;
}

// Explicit flash save, run from the game thread (input poll, inside tic
// building). Core 1 is parked between frames at that point, so nothing
// reads flash during the sector erase and no render pause is needed —
// the game resumes instantly; the ~100ms of interrupts-off just gaps the
// audio briefly. Borrows 4K of a framebuffer as staging (repainted next
// frame).
void ws_settings_task(void) {
    if (!ws_save_requested) {
        return;
    }
    ws_save_requested = false;
    extern uint8_t frame_buffer[2][320 * 168];
    extern void picoflash_sector_program(uint32_t flash_offs, const uint8_t *data);
    extern void ws_log(const char *fmt, ...);
    ws_log("saving settings");
    uint8_t *buf = frame_buffer[0];
    ws_settings_t s = ws_settings_snapshot();
    memset(buf, 0xFF, 4096);
    memcpy(buf, &s, sizeof(s));
    uint32_t save = save_and_disable_interrupts();
    picoflash_sector_program(WS_SETTINGS_OFFS, buf);
    restore_interrupts(save);
    ws_log("settings saved (music %d sfx %d turn %d move %d)",
           ws_music_vol, ws_sfx_vol, ws_turn_sens, ws_move_sens);
}

// ---------------------------------------------------------------- boot gate

// brief boot pause with USB serviced continuously so enumeration gets a
// clean window before the game hogs the CPU; LED 0 breathes during it
void ws_gate_wait(void) {
    extern void ws_stream_poll(void);
    extern void ws_log(const char *fmt, ...);
    extern void ws_panic_report_last(void);
    ws_log("usb settle, starting doom shortly");
    ws_panic_report_last();
    uint64_t end = time_us_64() + 5000000;
    uint32_t t = 0;
    while (time_us_64() < end) {
        ws_stream_poll();
        t++;
        uint32_t phase = (t >> 6) & 0x1FF;              // triangle wave
        uint32_t tri = phase < 256 ? phase : 511 - phase;
        pwm_set_gpio_level(WS_LED_FIRST_PIN, (tri * tri) >> 4);
        busy_wait_us(500);
    }
    ws_log("starting doom");
}

// ---------------------------------------------------------------- boot/panic

// light one LED of the boot ladder (0..5); stages survive until first tic
void ws_boot_stage(int led) {
    extern void ws_log(const char *fmt, ...);
    if (led >= 0 && led < 6) {
        pwm_set_gpio_level(WS_LED_FIRST_PIN + led, 1200 * 1200 >> 8);
        ws_log("boot stage %d", led);
    }
}

// clear the ladder between boot phases
void ws_boot_phase2(void) {
    extern void ws_log(const char *fmt, ...);
    for (int i = 0; i < 6; i++) {
        pwm_set_gpio_level(WS_LED_FIRST_PIN + i, 0);
    }
    ws_log("boot phase 2 (game loop startup)");
}

// pico-sdk panic() lands here too (PICO_PANIC_FUNCTION) — the WAD checks
// in w_file_memory.c use panic, not I_Error
void __attribute__((noreturn)) ws_panic(const char *msg);

// Override the SDK's anonymous "Hard assert" so the panic names the call
// site; with the flash persistence the address survives to the next boot.
// Also dump who owns the DMA interrupt vectors, since a registration
// conflict there is the known failure mode.
void __attribute__((noreturn)) hard_assertion_failure(void) {
    extern void __attribute__((noreturn)) ws_panic_fmt(const char *fmt, ...);
    ws_panic_fmt("hard assert @ %p v11=%p v12=%p",
                 __builtin_return_address(0),
                 irq_get_vtable_handler(DMA_IRQ_0),
                 irq_get_vtable_handler(DMA_IRQ_1));
}

void __attribute__((noreturn)) ws_panic_fmt(const char *fmt, ...) {
    static char buf[196];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "panic", args);
    va_end(args);
    ws_panic(buf);
}

// fatal error: flash all LEDs and keep USB alive so the message can be
// read in the web app's browser console (TEXT packets, once per second)
// panic record, stored alongside the settings so the message survives a
// power cycle and gets reported over USB at the next boot
#define WS_PANIC_REC_OFFS 64
#define WS_PANIC_REC_MAGIC 0x43505357u // "WSPC"
typedef struct {
    uint32_t magic;
    char msg[192];
} ws_panic_rec_t;

static void ws_panic_persist(const char *msg) {
    extern uint8_t frame_buffer[2][320 * 168];
    extern void picoflash_sector_program(uint32_t flash_offs, const uint8_t *data);
    uint8_t *buf = frame_buffer[0];
    memset(buf, 0xFF, 4096);
    ws_settings_t s = ws_settings_snapshot();
    memcpy(buf, &s, sizeof(s));
    ws_panic_rec_t *rec = (ws_panic_rec_t *) (buf + WS_PANIC_REC_OFFS);
    rec->magic = WS_PANIC_REC_MAGIC;
    strncpy(rec->msg, msg, sizeof(rec->msg) - 1);
    rec->msg[sizeof(rec->msg) - 1] = 0;
    uint32_t save = save_and_disable_interrupts();
    picoflash_sector_program(WS_SETTINGS_OFFS, buf);
    restore_interrupts(save);
}

void ws_panic_report_last(void) {
    extern void ws_log(const char *fmt, ...);
    const ws_panic_rec_t *rec =
        (const ws_panic_rec_t *) (XIP_BASE + WS_SETTINGS_OFFS + WS_PANIC_REC_OFFS);
    if (rec->magic == WS_PANIC_REC_MAGIC) {
        ws_log("last run panicked: %.72s", rec->msg);
    }
}

void __attribute__((noreturn)) ws_panic(const char *msg) {
    extern void ws_stream_poll(void);
    extern void ws_stream_unwedge(void);
    extern void ws_stream_packet(uint8_t type, const void *payload, uint16_t len);
    ws_panic_persist(msg); // readable at next boot even if USB dies here
    ws_stream_unwedge();   // USB must keep talking in the panic loop
    printf("\nWS-DOOM PANIC: %s\n", msg);
    uint16_t msglen = 0;
    while (msg[msglen] && msglen < 195) msglen++;
    uint64_t next_send = 0;
    for (;;) {
        bool on = (time_us_64() >> 17) & 1; // ~4Hz
        for (int i = 0; i < 6; i++) {
            pwm_set_gpio_level(WS_LED_FIRST_PIN + i, on ? 65535 : 0);
        }
        ws_stream_poll();
        uint64_t now = time_us_64();
        if (now > next_send) {
            next_send = now + 1000000;
            ws_stream_packet(0x04 /* WS_PKT_TEXT */, msg, msglen);
        }
    }
}

// ---------------------------------------------------------------- input

// compensate RP2040 ADC DNL errors (same correction as ComputerCard).
// RAM-resident: called 6x per sample from the engine interrupt, and an
// XIP stall here can overflow the ADC FIFO (heard as crackle)
static uint16_t __not_in_flash_func(ws_adc_correct)(uint16_t value) {
    uint16_t adc512 = value + 512;
    value += ((value & 0x3FF) == 0x1FF) << 2;
    value += (adc512 >> 10) << 3;
    return (uint32_t)(value * 520349u) >> 19;
}

static void post_key(int key, boolean down) {
    event_t ev;
    ev.type = down ? ev_keydown : ev_keyup;
    ev.data1 = key;
    ev.data2 = down ? key : 0;
    ev.data3 = 0;
    D_PostEvent(&ev);
}

// track key state so we only post edges
static void set_key(uint8_t *state, int key, boolean want) {
    if (*state != want) {
        *state = want;
        post_key(key, want);
    }
}

// map a bipolar control value to -2/-1/0/1/2 zones with hysteresis
static int zone5(int32_t v, int prev) {
    static const int32_t lo = 350, hi = 1350, hyst = 70;
    int z = prev;
    // move outward only past the threshold, inward only once back inside it
    if (v > hi + (prev >= 2 ? -hyst : hyst)) z = 2;
    else if (v > lo + (prev >= 1 ? -hyst : hyst)) z = (prev >= 2 && v > hi - hyst) ? 2 : 1;
    else if (v < -hi + (prev <= -2 ? hyst : -hyst)) z = -2;
    else if (v < -lo + (prev <= -1 ? hyst : -hyst)) z = (prev <= -2 && v < -hi + hyst) ? -2 : -1;
    else z = 0;
    return z;
}

// ---------------------------------------------------------------- outputs

// ~341 DAC counts per volt on the CV outs (default ComputerCard calibration)
static void ws_cv_out(uint pin, int32_t val) {
    if (val < -2048) val = -2048;
    if (val > 2047) val = 2047;
    pwm_set_gpio_level(pin, ((2047 - val) * 125) >> 8);
}

static void ws_led(int index, uint32_t bright4095) {
    pwm_set_gpio_level(WS_LED_FIRST_PIN + index, (bright4095 * bright4095) >> 8);
}

static volatile uint64_t pulse1_off_time, pulse2_off_time;

// returns nonzero when the shot sound is replaced by audio in 2
int ws_on_player_sound(void *origin, int sfx_id) {
    if (!playeringame[consoleplayer]) return 0;
    mobj_t *pmo = players[consoleplayer].mo;
    // sound origins are xy_positioned_t pointers (&mobj->xy), not mobjs —
    // compare against the player's embedded position struct
    if (!pmo || origin != (void *) &pmo->xy) return 0;
    switch (sfx_id) {
        case sfx_pistol:
        case sfx_shotgn:
        case sfx_dshtgn:
        case sfx_plasma:
        case sfx_rlaunc:
        case sfx_bfg:
        case sfx_sawful:
        case sfx_sawhit:
        case sfx_punch:
            gpio_put(WS_PULSE1_OUT_PIN, false); // raw low = jack high
            pulse1_off_time = time_us_64() + 25000;
            ws_in2_gate = 65536; // open the audio-in-2 gate
            // patched audio in 2 takes over as the gunshot sound
            return ws_in_connected[WS_IN_AUDIO2] ? 1 : 0;
        default:
            return 0;
    }
}

// ---------------------------------------------------------------- music beacon

// Per-side music gains (0..255, 128 = -6dB "unity" so effects sit on
// top). The mixer applies these to the soundtrack, turning the music
// into a direction/distance beacon toward the nearest living enemy.
volatile int16_t ws_music_gain[2] = {128, 128};

// nearest living monster distance in map units (for the spawn director)
volatile int32_t ws_nearest_dist = 0x7FFFFFFF;

extern angle_t R_PointToAngle2(fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2);
extern fixed_t P_AproxDistance(fixed_t dx, fixed_t dy);
extern thinker_t thinkercap;

static fixed_t ws_beacon_x, ws_beacon_y;
static bool ws_beacon_valid;
static bool ws_autoaim; // Z switch up: view tracks the beacon target

static void ws_music_beacon(void) {
    static uint32_t scan_countdown;
    mobj_t *pmo = playeringame[consoleplayer] ? players[consoleplayer].mo : NULL;
    if (gamestate != GS_LEVEL || !pmo) {
        ws_beacon_valid = false;
        ws_music_gain[0] = ws_music_gain[1] = ws_music_vol;
        return;
    }

    // rescan for the nearest living monster twice a second
    if (scan_countdown == 0) {
        scan_countdown = 16;
        fixed_t best = 0x7FFFFFFF;
        ws_beacon_valid = false;
        for (thinker_t *th = thinker_next(&thinkercap); th != &thinkercap;
             th = thinker_next(th)) {
            if (th->function != ThinkF_P_MobjThinker) continue;
            mobj_t *m = (mobj_t *) th;
            if (mobj_is_static(m)) continue;
            if (!(m->flags & MF_COUNTKILL) || mobj_full(m)->health <= 0) continue;
            fixed_t d = P_AproxDistance(pmo->xy.x - m->xy.x, pmo->xy.y - m->xy.y);
            if (d < best) {
                best = d;
                ws_beacon_x = m->xy.x;
                ws_beacon_y = m->xy.y;
                ws_beacon_valid = true;
            }
        }
        ws_nearest_dist = ws_beacon_valid ? (best >> FRACBITS) : 0x7FFFFFFF;
    }
    scan_countdown--;

    if (!ws_beacon_valid) {
        ws_music_gain[0] = ws_music_gain[1] = ws_music_vol;
        if (playeringame[consoleplayer] && players[consoleplayer].health > 0) {
            for (int i = 0; i < 6; i++) ws_led(i, 0); // radar clear: no target
        }
        return;
    }

    // pan by relative bearing (same convention as the sfx panner) and
    // fade with distance: loud when close, quiet when far
    angle_t rel = R_PointToAngle2(pmo->xy.x, pmo->xy.y, ws_beacon_x, ws_beacon_y)
                  - mobj_angle(pmo);
    int sep = 128 - (FixedMul(96 * FRACUNIT, finesine(rel >> ANGLETOFINESHIFT)) >> FRACBITS);
    int dist = P_AproxDistance(pmo->xy.x - ws_beacon_x, pmo->xy.y - ws_beacon_y) >> FRACBITS;
    int vol = 128;
    if (dist > 256) {
        vol = 128 - ((dist - 256) * 88) / 2744; // reaches 40 at ~3000 units
        if (vol < 40) vol = 40;
    }
    int gl = (vol * (254 - sep)) / 127;
    int gr = (vol * sep) / 127;
    if (gl < 20) gl = 20; // never fully silent on either side
    if (gr < 20) gr = 20;
    if (gl > 255) gl = 255;
    if (gr > 255) gr = 255;
    // scale by the user's music level (128 = nominal)
    gl = (gl * ws_music_vol) >> 7;
    gr = (gr * ws_music_vol) >> 7;
    if (gl > 255) gl = 255;
    if (gr > 255) gr = 255;
    ws_music_gain[0] = (int16_t) gl;
    ws_music_gain[1] = (int16_t) gr;

    // ---- LED radar: row = distance (top far, bottom near), column =
    // bearing (left / right / both when centered, dim both when behind).
    // Skipped while dead so the death blink shows instead.
    if (playeringame[consoleplayer] && players[consoleplayer].health > 0) {
        int32_t srel = (int32_t) rel;         // >0 = target to the left
        uint32_t arel = srel < 0 ? (uint32_t) -srel : (uint32_t) srel;
        int row = dist < 400 ? 4 : (dist < 1200 ? 2 : 0);
        bool behind = arel > ANG90 + ANG45;   // more than 135 degrees off
        bool center = arel < ANG45 / 2;       // within 22.5 degrees
        uint32_t bright = behind ? 700 : 3200;
        for (int i = 0; i < 6; i++) ws_led(i, 0);
        if (center || behind || srel > 0) ws_led(row, bright);
        if (center || behind || srel < 0) ws_led(row + 1, bright);
    }
}

// ---------------------------------------------------------------- analog ticcmd

static int32_t ws_deadzone(int32_t v, int32_t dz) {
    if (v > dz) return v - dz;
    if (v < -dz) return v + dz;
    return 0;
}

// Called from the end of G_BuildTiccmd: injects the analog panel state.
// Main knob + CV1 = turn rate (quadratic curve, up to full run-turn),
// X knob + CV2 = forward/back; auto-aim overrides the turn.
void ws_adjust_ticcmd(ticcmd_t *cmd) {
    if (gamestate != GS_LEVEL || !playeringame[consoleplayer]) return;
    mobj_t *pmo = players[consoleplayer].mo;
    if (!pmo) return;

    // sensitivity scale factors: squared so the sliders bite hard —
    // 128 = nominal (x1), 255 = ~x4, 32 = ~x1/16
    int32_t msc = ((int32_t) ws_move_sens * ws_move_sens) >> 7;
    int32_t tsc = ((int32_t) ws_turn_sens * ws_turn_sens) >> 7;

    // forward/back: X knob + CV2 (each with its own deadzone so an
    // unpatched-but-noisy CV can't cause drift), +-50 is run speed
    int32_t mv = ws_deadzone(knob_x - 2048, 150) + ws_deadzone(cv_in[1], 150);
    int32_t fwd = cmd->forwardmove + (mv * msc * 50) / (1900 * 128);
    if (fwd > 50) fwd = 50;
    if (fwd < -50) fwd = -50;
    cmd->forwardmove = (signed char) fwd;

    if (ws_autoaim && ws_beacon_valid) {
        // track the beacon target: proportional turn toward it
        angle_t bearing = R_PointToAngle2(pmo->xy.x, pmo->xy.y,
                                          ws_beacon_x, ws_beacon_y);
        int32_t err = (int32_t) (bearing - mobj_angle(pmo));
        int32_t at = err >> 16; // exact correction in angleturn units
        if (at > 1152) at = 1152;
        if (at < -1152) at = -1152;
        cmd->angleturn = (int16_t) at;
    } else {
        // turn rate: Main knob + CV1, squared for fine aim near center
        int32_t tv = (knob_main - 2048) + cv_in[0];
        if (tv > 2047) tv = 2047;
        if (tv < -2048) tv = -2048;
        tv = ws_deadzone(tv, 120);
        int32_t mag = tv < 0 ? -tv : tv;                // 0..1928
        int32_t turn = (tv < 0 ? 1 : -1) * (mag * mag) / 2900; // right = negative
        turn = (turn * tsc) >> 7;
        int32_t at = cmd->angleturn + turn;
        if (at > 1400) at = 1400;
        if (at < -1400) at = -1400;
        cmd->angleturn = (int16_t) at;
    }
}

// ---------------------------------------------------------------- poll

void ws_input_poll(void) {
    static uint8_t k_fire, k_use;
    static int last_weapon_slot = -1;
    static int32_t last_health = -1000;
    static uint32_t tic;
    static uint8_t weapon_key_down;
    extern void ws_log(const char *fmt, ...);

    tic++;
    // knob/CV/switch values are maintained by the engine interrupt
    ws_settings_task();

    // report audio-in patch changes so the routing is observable
    static uint8_t last_conn = 0xFF;
    uint8_t conn = (ws_in_connected[WS_IN_AUDIO1] ? 1 : 0) |
                   (ws_in_connected[WS_IN_AUDIO2] ? 2 : 0);
    if (conn != last_conn) {
        last_conn = conn;
        ws_log("audio in: 1=%s 2=%s",
               (conn & 1) ? "patched" : "-", (conn & 2) ? "patched" : "-");
    }

    // Movement is analog now: Main knob + CV1 = turn rate, X knob =
    // forward/back (CV2 adds) — injected straight into the ticcmd by
    // ws_adjust_ticcmd. Only discrete actions post key events here.

    // ---- switch + pulse ins
    // switch: down (momentary) = fire, up (latched) = auto-aim
    boolean sw_down = switch_raw < 1000;
    ws_autoaim = switch_raw > 3000;
    // unpatched pulse jacks are normalled to the (randomly toggling)
    // normalisation probe — only count pulses when a jack is present
    boolean pulse1 = ws_in_connected[WS_IN_PULSE1] && !gpio_get(WS_PULSE1_IN_PIN);
    boolean pulse2 = ws_in_connected[WS_IN_PULSE2] && !gpio_get(WS_PULSE2_IN_PIN);
    set_key(&k_fire, KEY_RCTRL, sw_down || pulse1);
    // while auto-aiming, tap "use" once a second so doors open hands-free
    boolean auto_use = ws_autoaim && gamestate == GS_LEVEL && (tic % 35) < 3;
    set_key(&k_use, ' ', pulse2 || auto_use);

    // ---- Y knob selects weapon 1..7 (debounced, with hysteresis so a
    // knob resting on a slot boundary can't oscillate); a confirmation
    // shot fires ~1s after the switch so each weapon announces itself
    int slot = 1 + (knob_y * 7) / 4096;
    if (slot < 1) slot = 1;
    if (slot > 7) slot = 7;
    // only treat the reading as valid when it sits well inside the slot
    int slot_pos = knob_y - ((slot - 1) * 4096) / 7; // 0..~585 within slot
    boolean slot_solid = slot_pos > 90 && slot_pos < 495;
    static int slot_cand, slot_cand_ticks;
    static int confirm_fire_time;
    static uint8_t confirm_fire_down;
    if (slot == slot_cand) {
        if (slot_cand_ticks < 100) slot_cand_ticks++;
    } else if (slot_solid) {
        slot_cand = slot;
        slot_cand_ticks = 0;
    }
    if (weapon_key_down) {
        post_key('0' + last_weapon_slot, false);
        weapon_key_down = 0;
    } else if (last_weapon_slot == -1) {
        last_weapon_slot = slot; // swallow the boot-time reading
        slot_cand = slot;
    } else if (slot_cand != last_weapon_slot && slot_cand_ticks >= 6) {
        last_weapon_slot = slot_cand;
        post_key('0' + slot_cand, true);
        weapon_key_down = 1;
        confirm_fire_time = leveltime + 40; // after the raise animation
    }
    if (gamestate != GS_LEVEL) {
        if (confirm_fire_down) {
            post_key(KEY_RCTRL, false); // never leave the tap held
            confirm_fire_down = 0;
        }
        confirm_fire_time = 0;
    } else if (confirm_fire_down) {
        post_key(KEY_RCTRL, false);
        confirm_fire_down = 0;
        confirm_fire_time = 0;
    } else if (confirm_fire_time && leveltime >= confirm_fire_time && !k_fire) {
        post_key(KEY_RCTRL, true);
        confirm_fire_down = 1;
    }

    // ---- game state -> CV outs, pulse 2, LEDs
    uint64_t now = time_us_64();
    if (pulse1_off_time && now > pulse1_off_time) {
        gpio_put(WS_PULSE1_OUT_PIN, true);
        pulse1_off_time = 0;
    }
    if (pulse2_off_time && now > pulse2_off_time) {
        gpio_put(WS_PULSE2_OUT_PIN, true);
        pulse2_off_time = 0;
    }

    if (gamestate == GS_LEVEL && playeringame[consoleplayer]) {
        player_t *p = &players[consoleplayer];
        int health = p->health;
        if (health < 0) health = 0;
        if (health > 100) health = 100;

        int ammo_frac = 0; // 0..100
        ammotype_t at = weaponinfo[p->readyweapon].ammo;
        if (at != am_noammo && p->maxammo[at] > 0) {
            ammo_frac = 100 * p->ammo[at] / p->maxammo[at];
            if (ammo_frac > 100) ammo_frac = 100;
        }

        ws_cv_out(WS_CV1_OUT_PIN, health * 17);    // 0..100 -> 0..~5V
        ws_cv_out(WS_CV2_OUT_PIN, ammo_frac * 17);

        if (last_health > -1000 && health < last_health) {
            gpio_put(WS_PULSE2_OUT_PIN, false);
            pulse2_off_time = now + 25000;
        }
        last_health = health;

        // LEDs are the enemy radar (drawn by ws_music_beacon) while
        // alive; dead = slow blink. Respawn takes the USE button
        // (P_DeathThink checks BT_USE, not attack): tap it when the
        // switch is flicked down, or automatically after 10s.
        static int dead_tics;
        static uint8_t dead_tap;
        if (health == 0) {
            uint32_t on = (tic >> 4) & 1;
            for (int i = 0; i < 6; i++) ws_led(i, on ? 1200 : 0);
            dead_tics++;
            if (dead_tap) {
                post_key(' ', false);
                dead_tap = 0;
                dead_tics = 0;
            } else if (((sw_down && dead_tics > 18) || dead_tics > 10 * 35) && !k_use) {
                post_key(' ', true);
                dead_tap = 1;
            }
        } else {
            dead_tics = 0;
            if (dead_tap) {
                post_key(' ', false); // release even if revived instantly
                dead_tap = 0;
            }
        }
    } else {
        ws_cv_out(WS_CV1_OUT_PIN, 0);
        ws_cv_out(WS_CV2_OUT_PIN, 0);
        last_health = -1000;
        // attract/loading: gentle LED chase
        for (int i = 0; i < 6; i++) {
            ws_led(i, ((tic >> 2) % 6) == (uint32_t) i ? 1500 : 0);
        }
    }
    ws_music_beacon();
}
