// cathode_ray — PAL composite video synthesizer for Workshop Computer
// (1-bit image rendered through a 2-bit resistor DAC on Pulse Out 1 + 2)
//
// Core 0: ComputerCard framework (ProcessSample ISR @ 48 kHz).
//         Reads all Eurorack I/O, writes to volatile shared state.
// Core 1: Dedicated video loop. Owns PIO0/SM0 + DMA. Runs at full speed.
//         Builds frame word-stream, updates framebuffer during vblank.
//
// Video output: 2-bit resistor DAC on GPIO 8 (Pulse Out 1) + GPIO 9 (Pulse Out 2).
//   Pu1 ──[1k ]──┐
//                ├── RCA centre ── composite in (TV internal 75Ω to GND)
//   Pu2 ──[220R]──┘
//   GND ───────────── RCA shell
// 4 summed levels (3 used): sync 0V / black ~0.3V / white ~1.0V. Pulse Out 2 is
// now consumed by video — it is no longer a usable normal pulse output.
//
// PAL timing (144 MHz sys clock, SM @ 144/20.571 = 7.000 MHz):
//   1 pixel = 1 SM cycle = 20.571 sys-cycles = 142.857 ns
//   Line:    448 pixels = 64.000 µs  (target 64.000 µs, 0%)
//   Frame:   312 lines  = 19.968 ms  (target 20.000 ms = 50 Hz, -0.16%)

#include "ComputerCard.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "composite.pio.h"
#include <cstring>

// ─── Hardware pin macros ──────────────────────────────────────────────────────
#define VIDEO_GPIO          8       // PULSE_1_RAW_OUT — video DAC bit 0 (Pu1).
                                    // GPIO 9 (PULSE_2_RAW_OUT) = video DAC bit 1 (Pu2).
                                    // PIO drives both via 'out pins,2' (consecutive 8,9).
#define PULSE_IN_1_GPIO     2       // PULSE_1_INPUT   — rising edge = clear frame
#define PULSE_IN_2_GPIO     3       // PULSE_2_INPUT   — high = invert video output

// ─── Framebuffer ─────────────────────────────────────────────────────────────
#define FB_WIDTH            360     // pixels per row (360 × 144ns = 51.84µs active)
#define FB_HEIGHT           256     // rows (fills most of visible PAL height)
#define FB_STRIDE           45      // bytes per row (360/8 = 45, exact)
#define FB_SIZE             11520   // total bytes (45 × 256)
#define PIXEL_STRETCH       1       // each pixel = 1 clock tick (~144ns)

// Pixel set/clear helpers. Row r, column c (0-based, left=MSB).
#define FB_SET(buf, r, c)   ((buf)[(r)*FB_STRIDE + (c)/8] |=  (0x80u >> ((c)&7)))
#define FB_CLEAR(buf, r, c) ((buf)[(r)*FB_STRIDE + (c)/8] &= ~(0x80u >> ((c)&7)))

// ─── Greyscale working buffer ────────────────────────────────────────────────
// All drawing happens in a half-resolution grey buffer where each cell holds a
// brightness 0..GREY_LEVELS-1. Each frame it is expanded into the 1-bit
// frame_buffer via a GREY_SCALE×GREY_SCALE spatial dither, giving fake greyscale
// on a 1-bit display (ZX-Spectrum-style). The scan-out path (build_frame_words)
// is unchanged — it still reads frame_buffer bit by bit.
//
// GREY_SCALE is the downscale factor. It MUST divide both FB_WIDTH and FB_HEIGHT.
// Legal values (common divisors of 360 and 256): 1, 2, 4, 8. Start at 2 (180×128);
// raise toward 1 (full res) later only after checking RAM (grey buffer = GREY_SIZE).
#define GREY_SCALE   2
#define GREY_W       (FB_WIDTH  / GREY_SCALE)   // 180 at scale 2
#define GREY_H       (FB_HEIGHT / GREY_SCALE)   // 128 at scale 2
#define GREY_SIZE    (GREY_W * GREY_H)          // 23040 bytes at scale 2
#define GREY_LEVELS  5                          // 0=black .. 4=white (count of lit 2×2 subpx)
static_assert(FB_WIDTH  % GREY_SCALE == 0, "GREY_SCALE must divide FB_WIDTH");
static_assert(FB_HEIGHT % GREY_SCALE == 0, "GREY_SCALE must divide FB_HEIGHT");

#define GREY_SET(buf, r, c, lvl) ((buf)[(r)*GREY_W + (c)] = (uint8_t)(lvl))

// ─── TV timing: PAL (default) or NTSC (build with -DTV_NTSC) ──────────────────
// BOTH formats share the SAME 7.000 MHz pixel clock (144MHz / (144/7), 142.857 ns/tick)
// and the SAME 360×256 framebuffer + 180×128 grey buffer — so ALL drawing code (scope,
// etch, spectrum, every alt-boot mode) is identical for both. Only the line/frame TIMING
// and the number of framebuffer rows scanned out (a small crop for NTSC) differ. This is
// the ONE place PAL and NTSC diverge — everything else derives automatically.
//   Frame-structure macros are format-neutral (TV_*) so build_frame_words() is shared.
#ifdef TV_NTSC
// NTSC: line = 63.556µs. At 7MHz → 445 ticks (63.571µs, +0.02%). 262 lines → 60.04Hz.
//   fp=10 + hs=33 + bp=32 + av=370 = 445.  Active = 240 (a centred crop of the 256-row FB).
#define LINE_FP_PX          10      // front porch (~1.5µs)
#define LINE_HS_PX          33      // h-sync (~4.7µs)
#define LINE_BP_PX          32      // back porch (~4.5µs)
#define LINE_AV_PX          370     // active video (FB_WIDTH=360 + 10 px right padding)
#define LINE_TOTAL_PX       445     // 10+33+32+370 = 445 ✓
#define VSYNC_LOW_PX        190     // broad sync pulse LOW (~27µs)
#define TV_VSYNC_LINES      6
#define TV_BLANK_TOP        8
#define TV_ACTIVE_LINES     240     // scan out 240 of the 256 FB rows (crop 8 top / 8 bot)
#define TV_ACTIVE_ROW0      8       // first FB row scanned → centres the crop
#define TV_BLANK_BOT        8       // 6+8+240+8 = 262 ✓
#define TV_TOTAL_LINES      262
#else
// PAL: line = 64µs = 448 ticks. Segments sum to exactly 448. 312 lines → 50Hz.
//   fp=12 + hs=33 + bp=40 + av=363 = 448 ✓  (av: FB_WIDTH=360 + 3 px right pad)
#define LINE_FP_PX          12      // front porch (1.65µs → 1.714µs)
#define LINE_HS_PX          33      // h-sync (4.7µs → 4.714µs)
#define LINE_BP_PX          40      // back porch (5.7µs → 5.714µs)
#define LINE_AV_PX          363     // active video (FB_WIDTH=360 + 3 px right padding)
#define LINE_TOTAL_PX       448     // 12+33+40+363 = 448 ✓
#define VSYNC_LOW_PX        191     // V-sync long pulse: 27.3µs → 191 ticks LOW
#define TV_VSYNC_LINES      5
#define TV_BLANK_TOP        33      // picture vertical position (down vs default)
#define TV_ACTIVE_LINES     FB_HEIGHT   // 256 — all FB rows
#define TV_ACTIVE_ROW0      0
#define TV_BLANK_BOT        18      // 5+33+256+18 = 312 ✓
#define TV_TOTAL_LINES      312
#endif
#define VSYNC_HIGH_PX       (LINE_TOTAL_PX - VSYNC_LOW_PX)

// Bottom-anchored drawing (spectrum bar bases, Lunar ground) sits at the very bottom of the
// 256-row framebuffer. NTSC crops a few rows off the bottom, so on NTSC nudge such content up
// by this many GREY cells so it isn't clipped. PAL = 0 (no crop).
#ifdef TV_NTSC
#define NTSC_BOTTOM_INSET   6
#else
#define NTSC_BOTTOM_INSET   0
#endif

// ─── DMA word stream ─────────────────────────────────────────────────────────
// 2 bits/pixel, packed contiguously (16 px/word). Words per frame = ceil(px/16), where
// px = LINE_TOTAL_PX × TV_TOTAL_LINES. This is the DMA transfer count and MUST match the
// format exactly, or the frame period (and thus refresh rate / sync) is wrong.
//   PAL : 448×312 = 139776 px → 8736 words.   NTSC: 445×262 = 116590 → 7287 words.
#define FRAME_WORDS         ((LINE_TOTAL_PX * TV_TOTAL_LINES + 15) / 16)
// Buffers are sized for the LARGER (PAL) frame so one allocation serves both formats.
#define FRAME_WORDS_MAX     8736

// Double-buffered word streams: Core 1 writes to back buffer, DMA reads from front.
static uint32_t __attribute__((aligned(4))) word_buf[2][FRAME_WORDS_MAX];
static volatile int active_buf = 0;  // which buffer DMA is currently reading

// ─── Framebuffer (written by Core 1 during vblank) ───────────────────────────
static uint8_t frame_buffer[FB_SIZE];

// ─── Grey working buffer (Core-1-private; expanded into frame_buffer each frame)
static uint8_t grey_buffer[GREY_SIZE];

// ─── Etch CV ring buffer (Core 0 pushes @48kHz, Core 1 drains each frame) ─────
// Captures sub-frame CV motion: each frame Core 1 plots every sample Core 0 queued.
// Core 0 produces 48000/50 = 960 samples per frame; the ring MUST hold ≥ that or the
// overrun guard drops the oldest ~half each frame → sparse points → visible straight
// lines between them. 2048 (≈42.7ms) covers a full frame with comfortable margin.
#define ETCH_RING_SIZE   2048
#define ETCH_RING_MASK   (ETCH_RING_SIZE - 1)
static volatile int16_t etch_x_ring[ETCH_RING_SIZE];
static volatile int16_t etch_y_ring[ETCH_RING_SIZE];
static volatile uint32_t etch_write_idx = 0;  // Core 0 increments after each push

// ─── Audio ring buffer (Core 0 pushes AudioIn1 @48kHz; scope pulls 1/column) ──
// Lets the scope draw a real waveform at fast sweep: each drawn column reads a fresh
// audio sample instead of reusing one per frame (which made bars fat at speed).
#define AUDIO_RING_SIZE  1024     // ≈21ms @48kHz — covers a full frame's worth + margin
#define AUDIO_RING_MASK  (AUDIO_RING_SIZE - 1)
static volatile int16_t  audio_ring[AUDIO_RING_SIZE];
static volatile uint32_t audio_write_idx = 0;

// ─── Shared state (Core 0 ISR → Core 1 video loop) ───────────────────────────
struct SharedState {
    volatile int32_t  audio_y;       // AudioIn1() raw: -2048..2047
    volatile int32_t  cv_x;          // CVIn1()   raw: -2048..2047
    volatile int32_t  cv_y;          // CVIn2()   raw: -2048..2047
    // Etch X/Y mapping, resolved on Core 0 (pickup hysteresis) → consumed by Core 1.
    // pos = offset + (cv * cvgain) >> 12.  offset in grey coords; cvgain is 12.12 fixed.
    volatile int32_t  etch_offset_x; // 0..GREY_W-1 base position
    volatile int32_t  etch_offset_y; // 0..GREY_H-1
    volatile int32_t  etch_cvgain_x; // CV→grey gain (>>12): scale·(GREY_W-1)·4096/(256·4095)
    volatile int32_t  etch_cvgain_y; // (>>12): scale·(GREY_H-1)·4096/(256·4095)
    volatile int32_t  scope_audio_scale; // Y knob in scope: 0..512 (256 = 1×, max 2×) audio gain
    volatile int32_t  scope_baseline;    // X knob in scope: 0..GREY_H-1 centre-line row
    volatile int32_t  spec_gain;         // Y knob in spectrum: own gain 0..4095 (pickup)
    volatile int32_t  spec_rot;          // X knob in spectrum: own rotate/pos 0..4095 (pickup)
    volatile int32_t  knob_main;     // KnobVal(Main): 0..4095 (far-CCW=etch, else scope speed)
    volatile int32_t  knob_x;        // KnobVal(X): 0..4095 raw (alt-mode hybrids read this)
    volatile int32_t  knob_y;        // KnobVal(Y): 0..4095 raw (alt-mode hybrids read this)
    volatile uint8_t  sw_position;   // 0=UP(fade) 1=MID(static) 2=DOWN(effect / menu)
    // Trigger sources (Core 0 latches edges; Core 1 reads, clears the *_rising latches).
    // Three independent: switch-DOWN→cfg_sw, PU1→cfg_pu1, PU2→cfg_pu2.
    volatile bool     down_held;     // switch DOWN currently held
    volatile bool     down_rising;   // switch DOWN just pressed (latch)
    volatile bool     pu1_held;      // Pulse In 1 currently high
    volatile bool     pu1_rising;    // Pulse In 1 rising edge (latch)
    volatile bool     pu2_held;      // Pulse In 2 currently high
    volatile bool     pu2_rising;    // Pulse In 2 rising edge (latch)
    // Audio inputs as triggers (FourTrig alt-mode): edge-detect a rising level crossing.
    volatile bool     ain1_rising;   // Audio In 1 crossed the trigger threshold (latch)
    volatile bool     ain2_rising;   // Audio In 2 crossed the trigger threshold (latch)
    // Config (set in menu; RAM-only). Three independent triggers. Defaults reproduce
    // original firmware (DOWN cycles effects, PU2 inverts).
    volatile uint8_t  cfg_sw;        // Behaviour for switch-DOWN (Main knob; def CYCLE FX)
    volatile uint8_t  cfg_pu1;       // Behaviour for Pulse In 1  (Knob X; def CYCLE FX)
    volatile uint8_t  cfg_pu2;       // Behaviour for Pulse In 2  (Knob Y; def INVERT)
    volatile bool     menu_active;   // true while the config menu is showing
    volatile bool     alt_mode;      // true if DOWN held at boot → screensaver
    // Patchteroids CV bridge (game on Core 1 ↔ CV I/O on Core 0).
    volatile int32_t  ast_note;      // current pitch, semitones above base (Core 1 writes)
    volatile uint32_t ast_gate_seq;  // bumped by Core 1 on each hit/arp-step → Core 0 gates
    volatile bool     ast_fire;      // PU1 fire latch: Core 0 sets, Core 1 reads-and-clears
    // Which alt-boot hybrid is selected (published by Core 1). Core 0 uses this to decide
    // how the alt-mode CV bridge behaves (only Patchteroids steers via CV1 / outputs pitch).
    volatile int32_t  alt_hybrid;    // selected alt-boot mode index (see ALT_NAMES order)
    // Generic CV1-out value for non-Patchteroids hybrids (Core 1 writes, Core 0 → CVOut1).
    // Boing uses it for ball height. Range -2048..2047.
    volatile int32_t  alt_cv1;
};
static SharedState shared;

// ─── Multi-function knob pickup (Core 0) ──────────────────────────────────────
// A knob's destination depends on mode+switch. When it changes, the newly-bound param
// holds its stored value until the knob is physically moved past PICKUP_THRESH
// ("pickup"), preventing a jump. Each destination keeps its own stored value.
// Destinations: SCALE (etch CV scale, UP), OFFSET (etch position, MID),
//   AUDIO (scope Y audio gain), BASELINE (scope X = centre-line vertical position).
#define PICKUP_THRESH   64       // raw knob movement (of 4095) needed to grab control
#define MENU_MOVE_THRESH 96      // knob move while DOWN held that opens the config menu
#define PICK_SCALE      0
#define PICK_OFFSET     1
#define PICK_AUDIO      2
#define PICK_BASELINE   3
#define PICK_SPEC_GAIN  4        // spectrum Y: own gain (distinct from scope's PICK_AUDIO)
#define PICK_SPEC_ROT   5        // spectrum X: own rotate/position
struct KnobPick {
    int32_t stored_offset;       // grey coords — etch X/Y position
    int32_t stored_scale;        // 0..1024 (256 = 1×)  — etch CV scale
    int32_t stored_audio;        // 0..512 (256 = 1×, max 2×) — scope audio scale (Y only)
    int32_t stored_baseline;     // 0..GREY_H-1 — scope centre-line vertical pos (X only)
    int32_t stored_specgain;     // 0..4095 — spectrum audio gain (Y, own value)
    int32_t stored_specrot;      // 0..4095 — spectrum rotate/position (X, own value)
    uint8_t bound;               // current destination (PICK_*)
    int32_t bind_raw;            // raw knob value captured at last bind change
    bool    captured;            // false until knob moves past threshold after a switch
};

// ─── DMA channel ─────────────────────────────────────────────────────────────
static int dma_chan = -1;
static volatile bool vblank_ready = false;  // set by DMA IRQ at frame end

// ─── LCG random (Core 1 only, no locking needed) ─────────────────────────────
static uint32_t lcg_state = 0xDEADBEEF;
static inline uint32_t lcg_rand() {
    lcg_state = lcg_state * 1664525u + 1013904223u;
    return lcg_state;
}

// ─────────────────────────────────────────────────────────────────────────────
// Word-stream builder
// Packs the PAL frame (sync lines + active video lines) into word_buf[back].
// The invert flag is applied here: active pixel bytes are XOR'd with 0xFF.
// ─────────────────────────────────────────────────────────────────────────────

// ─── Output levels (2-bit resistor DAC) ──────────────────────────────────────
// Pulse outputs are INVERTED and idle high (~6V); driven = ~0.3V. Two pins (Pu1=GPIO8,
// Pu2=GPIO9) sum through weighted resistors into the RCA centre pin → 4 levels.
//
// level_pair[] is the ONE place polarity + resistor weighting lives. Each entry is a
// 2-bit field: bit0 → GPIO8/Pu1, bit1 → GPIO9/Pu2. PIO bit 1 → jack LOW, bit 0 → jack HIGH.
// CRITICAL: BLACK must sit ABOVE sync (0V) but below white. The TV's sync separator
// slices at a threshold BETWEEN black and sync — if BLACK == SYNC the TV loses sync
// and shows no picture. So BLACK needs a small non-zero pedestal.
//   SYNC  = 0b11 → both pins LOW  → 0V (sync tip)
//   BLACK = 0b10 → Pu1 HIGH via 1kΩ only → smallest lift above sync = black pedestal
//   WHITE = 0b00 → both pins HIGH → brightest
// (0b01 = Pu2 via 220Ω = a BRIGHTER grey — was the old too-light "black".)
// Retune the DAC by editing ONLY this table. If the scope shows GPIO8/9 swapped,
// swap the two bits within each entry — never touch the packing loop.
enum Level { SYNC = 0, BLACK = 1, WHITE = 2 };
//                                  bit1=Pu2(GPIO9), bit0=Pu1(GPIO8)
static const uint8_t level_pair[3] = {
    /*SYNC */ 0b11,   // both jack LOW  → 0V
    /*BLACK*/ 0b10,   // Pu1 HIGH via 1kΩ only → small pedestal above sync
    /*WHITE*/ 0b00,   // both jack HIGH → brightest
};

static void build_frame_words(int back, bool invert) {
    uint32_t *buf = word_buf[back];
    int word_idx = 0;
    uint32_t cur_word = 0;
    int bit_pos = 0;  // counts BITS (advances by 2 per pixel); word commits at 32

    auto commit_word = [&]() {
        buf[word_idx++] = cur_word;
        cur_word = 0;
        bit_pos = 0;
    };

    // emit_const: emit `count` PIXELS all at the given level (2 bits each).
    // 32-bit threshold / 2 = 16 pixels per word; pairs never straddle a word boundary.
    auto emit_const = [&](Level lvl, int count) {
        uint32_t pair = level_pair[lvl] & 0x3;
        // 16×-replicated full word for the fast path
        uint32_t fill = pair;
        for (int i = 1; i < 16; i++) fill = (fill << 2) | pair;
        while (bit_pos > 0 && count > 0) {
            cur_word = (cur_word << 2) | pair;
            bit_pos += 2;
            count--;
            if (bit_pos == 32) commit_word();
        }
        while (count >= 16) {
            buf[word_idx++] = fill;
            count -= 16;
        }
        while (count > 0) {
            cur_word = (cur_word << 2) | pair;
            bit_pos += 2;
            count--;
            if (bit_pos == 32) commit_word();
        }
    };

    // Blank line: fp=black, hs=sync, rest=black (blanking pedestal sits at black)
    auto emit_blank_line = [&]() {
        emit_const(BLACK, LINE_FP_PX);
        emit_const(SYNC,  LINE_HS_PX);
        emit_const(BLACK, LINE_BP_PX + LINE_AV_PX);
    };

    // V-sync line: short front porch, long sync pulse, rest black
    auto emit_vsync_line = [&]() {
        emit_const(BLACK, LINE_FP_PX);
        emit_const(SYNC,  VSYNC_LOW_PX);
        emit_const(BLACK, VSYNC_HIGH_PX - LINE_FP_PX);
    };

    // V-sync lines
    for (int l = 0; l < TV_VSYNC_LINES; l++) {
        emit_vsync_line();
    }

    // Top blank lines
    for (int l = 0; l < TV_BLANK_TOP; l++) {
        emit_blank_line();
    }

    // Active video. TV_ACTIVE_LINES rows are scanned, starting at framebuffer row
    // TV_ACTIVE_ROW0 (PAL: 0/256 = all rows; NTSC: 8/240 = a centred crop of the 256-row
    // framebuffer, so the drawing geometry is identical for both formats).
    // Framebuffer bit SET = white pixel, CLEAR = black; invert swaps WHITE/BLACK.
    for (int row = 0; row < TV_ACTIVE_LINES; row++) {
        emit_const(BLACK, LINE_FP_PX);   // front porch (black)
        emit_const(SYNC,  LINE_HS_PX);   // h-sync
        emit_const(BLACK, LINE_BP_PX);   // back porch (black)
        const uint8_t *fb_row = &frame_buffer[(TV_ACTIVE_ROW0 + row) * FB_STRIDE];
        for (int p = 0; p < FB_WIDTH; p++) {
            bool set = (fb_row[p / 8] >> (7 - (p & 7))) & 1u;
            if (invert) set = !set;
            uint32_t pair = level_pair[set ? WHITE : BLACK] & 0x3;
            for (int s = 0; s < PIXEL_STRETCH; s++) {
                cur_word = (cur_word << 2) | pair;
                bit_pos += 2;
                if (bit_pos == 32) commit_word();
            }
        }
        emit_const(BLACK, LINE_AV_PX - FB_WIDTH);  // right-pad to fill LINE_AV_PX (363-360 = 3 px)
    }

    // Bottom blank lines
    for (int l = 0; l < TV_BLANK_BOT; l++) {
        emit_blank_line();
    }

    // Flush remaining partial word, padding LSBs with BLACK pairs
    if (bit_pos > 0) {
        uint32_t blk = level_pair[BLACK] & 0x3;
        while (bit_pos < 32) {
            cur_word = (cur_word << 2) | blk;
            bit_pos += 2;
        }
        commit_word();
    }

    // Fill any remaining words with the 16×BLACK blanking-level constant
    uint32_t black_fill = level_pair[BLACK] & 0x3;
    for (int i = 1; i < 16; i++) black_fill = (black_fill << 2) | (level_pair[BLACK] & 0x3);
    while (word_idx < FRAME_WORDS) {
        buf[word_idx++] = black_fill;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Framebuffer drawing
// Called by Core 1 during vblank. Reads from shared, writes to frame_buffer[].
// ─────────────────────────────────────────────────────────────────────────────

// ─── Performance effects ─────────────────────────────────────────────────────
// Low-level effect set (the actual visual effects). run_fx() executes one of these.
enum { FX_STROBE, FX_FREEZE_BLACK, FX_FREEZE_WHITE, FX_SWAP_XY, FX_SNOW, FX_CORRUPT,
       FX_ROLL, FX_COUNT };
static bool effect_invert = false; // strobe: OR'd into build_frame_words invert

// Screen roll: vertically scroll the whole grey buffer a few rows each frame (a TV with
// broken vertical hold). Wraps around. Keeps drawing under it.
static void __not_in_flash_func(roll_step)() {
    const int SHIFT = 2;                    // rows per frame
    static uint8_t tmp[GREY_W * 2];         // hold the SHIFT rows that wrap around
    for (int r = 0; r < SHIFT; r++)
        for (int c = 0; c < GREY_W; c++) tmp[r*GREY_W + c] = grey_buffer[r*GREY_W + c];
    memmove(grey_buffer, &grey_buffer[SHIFT*GREY_W], (GREY_H - SHIFT) * GREY_W);
    for (int r = 0; r < SHIFT; r++)
        for (int c = 0; c < GREY_W; c++)
            grey_buffer[(GREY_H - SHIFT + r)*GREY_W + c] = tmp[r*GREY_W + c];
}

// ─── Behaviours (configurable per trigger source: switch-DOWN and Pu2) ────────
// A "behaviour" is what a trigger source does. Both switch-DOWN and Pulse In 2 pick
// one of these (set in the config menu). Defaults reproduce the original firmware.
enum Behaviour { BHV_INVERT, BHV_CLS, BHV_CYCLE_FX, BHV_RANDOM_FX,
                 BHV_FX1_STROBE, BHV_FX2_FADE, BHV_FX3_FADEWHITE, BHV_FX4_SNOW,
                 BHV_FX5_SWAP, BHV_FX6_CORRUPT, BHV_FX7_ROLL, BHV_COUNT };
static const char *const BHV_NAMES[BHV_COUNT] = {
    "INVERT","CLS","CYCLE FX","RANDOM FX",
    "STROBE","FADE","FADEWHITE","SNOW","SWAP","CORRUPT","ROLL" };
// Menu FX order (FX1..7) → the low-level FX enum (different order).
static const int BHV_FX_MAP[7] = {
    FX_STROBE, FX_FREEZE_BLACK, FX_FREEZE_WHITE, FX_SNOW, FX_SWAP_XY, FX_CORRUPT, FX_ROLL };

// Per-trigger-source effect state (so switch-DOWN and Pu2 don't fight each other).
struct FxState { int fx_index; uint32_t fx_phase; bool strobe_invert; };
static FxState fx_down = {0,0,false};   // switch DOWN
static FxState fx_pu1  = {0,0,false};   // Pulse In 1 (same behaviour as DOWN)
static FxState fx_pu2  = {0,0,false};   // Pulse In 2

// ─── Oscilloscope sweep (knob-controlled speed, fixed-point accumulator) ──────
// scope_x is the integer sweep column. scope_acc accumulates fractional columns each
// frame (8.8 fixed point) so slow speeds (<1 col/frame) and fast (>1) both work.
static int scope_x = 0;            // current sweep column (grey-X, Core 1 private)
static uint32_t scope_acc = 0;     // fractional column accumulator (8.8 fixed)
static uint32_t audio_read_idx = 0; // Core 1's drain position in the audio ring
// Sweep speed range (cols/frame, 8.8 fixed). 180 cols, 50 fps:
//   fast = 180/(0.1*50)=36 cols/frame; slow = 180/(3.0*50)=1.2 cols/frame.
#define SCOPE_STEP_SLOW   (307u)   //  1.2 cols/frame × 256 ≈ 307  (~3.0s/sweep)
#define SCOPE_STEP_FAST   (9216u)  // 36.0 cols/frame × 256 = 9216 (~0.1s/sweep)
// Main knob is split into THIRDS: lower = ETCH (CV vs CV), middle = SCOPE (audio wave),
// upper = SPECTRUM (audio analyser). ETCH_THRESH kept as the etch/scope-and-up boundary
// (used by Core-0 pickup routing: below it = etch, at/above = scope-style X/Y roles).
#define ETCH_THRESH       1365     // < this = etch (lower third)
#define SPECTRUM_THRESH   2730     // >= this = spectrum (upper third); between = scope
enum MainMode { MODE_ETCH = 0, MODE_SCOPE = 1, MODE_SPECTRUM = 2 };
static inline MainMode main_mode(int32_t knob) {
    return knob < ETCH_THRESH ? MODE_ETCH : knob < SPECTRUM_THRESH ? MODE_SCOPE : MODE_SPECTRUM;
}

// ─── Phosphor fade ───────────────────────────────────────────────────────────
// Scope fade is LOCKED to the sweep: one global level-decrement each time the sweep
// has advanced GREY_W/(GREY_LEVELS-1) columns. Over a full pass the sweep advances
// GREY_W cols = (GREY_LEVELS-1) decrements = level (max)→0, so a freshly-drawn column
// reaches true black just as the sweep wraps back to it, at ANY speed. (fade_cols
// accumulates columns advanced since the last decrement.)
static uint32_t fade_cols = 0;     // scope: columns since last decrement; etch: frames
// Etch fade rate is set by the knob across the etch quarter: knob 0 → fastest (0.15s
// to black), knob (ETCH_THRESH-1) → slowest (2.0s). 5 levels = 4 decrements:
//   0.15s = 7.5 frames → decrement every ~2 frames; 2.0s = 100 frames → every ~25.
#define ETCH_FADE_FAST    2        // frames/decrement at knob 0    (~0.15s to black)
#define ETCH_FADE_SLOW    25       // frames/decrement at knob max  (~2.0s to black)
static uint32_t etch_read_idx = 0; // Core 1's drain position in the etch CV ring
static int etch_prev_x = 0, etch_prev_y = 0; // last etch point (for line interpolation)
static bool etch_have_prev = false;          // false until first etch sample drawn
#define ETCH_DEADBAND 1   // grey-cell jitter band: ignore moves within ±this of anchor

// ─── 2×2 dither patterns: 5 levels × 4 orientations (GREY_SCALE=2) ────────────
// dither[level][orientation][sub_row]; each value = 2-bit field (bit1=left,bit0=right).
// level = number of lit subpixels (0..4). 4 orientations per level are cycled every 2
// frames so the dither rotates and averages out spatially over time (smoother greys,
// less fixed checkerboard). L0 and L4 are identical across orientations; L1/L2/L3 differ.
// Cell layout per entry: {top_row, bottom_row}.
static const uint8_t dither[GREY_LEVELS][4][2] = {
    /*L0 0/4*/ {{0b00,0b00},{0b00,0b00},{0b00,0b00},{0b00,0b00}},
    /*L1 1/4*/ {{0b10,0b00},{0b01,0b00},{0b00,0b01},{0b00,0b10}}, // single px in TL,TR,BR,BL
    /*L2 2/4*/ {{0b10,0b01},{0b01,0b10},{0b11,0b00},{0b00,0b11}}, // diag\, diag/, top, bottom
    /*L3 3/4*/ {{0b11,0b10},{0b11,0b01},{0b01,0b11},{0b10,0b11}}, // one corner off
    /*L4 4/4*/ {{0b11,0b11},{0b11,0b11},{0b11,0b11},{0b11,0b11}},
};

// Dither orientation index, advanced every 2 frames by Core 1 (see core1_main loop).
static int dither_orient = 0;

// When set, expand_grey_to_fb() skips white-dilation for this frame. Used for static
// TEXT screens (menu / charset / death) — dilation is a scope-trace hack that just
// fattens text and merges glyphs. Set per-frame by the screens that draw text.
static bool text_mode = false;

// Right-dilate white by WHITE_DILATE pixels: every white pixel also forces the next
// WHITE_DILATE pixels to its RIGHT white. Exploits the composite DAC's rising-edge
// slew — the node only reaches full white when several adjacent pixels are white, so a
// lone white pixel reads grey. Dilating guarantees every white feature is ≥(1+N)px in
// scan order → renders full white. MSB = leftmost pixel, so "right" = toward the LSB
// (right shift). We track a rolling history of the last 8 emitted bits so the dilate
// crosses byte boundaries for any N up to 7.
#define WHITE_DILATE 4   // measured on hardware: an isolated white px needs ~5px (1+4) to reach true white

// Per-frame cap on white-dilation (default = full WHITE_DILATE). Screens that draw solid
// filled shapes (e.g. the Boing ball) lower this so white areas don't bloom together and
// wash the whole shape white. Reset each frame at the top of update_framebuffer().
static int dilate_cap = WHITE_DILATE;

// Per-level rightward dilation in PIXELS, indexed by grey level 0..4. Higher levels
// are held on longer (→ brighter); low/fading levels get little/no dilation so dim
// pixels actually look dim. This makes dilation LEVEL-AWARE so it no longer flattens
// the greyscale (a lone L1 sub-pixel previously got the full 4px hold = looked white).
//   L0=black(0)  L1=1  L2=2  L3=3  L4=full white(4=WHITE_DILATE)
static const uint8_t dilate_amount[GREY_LEVELS] = { 0, 1, 2, 3, WHITE_DILATE };

// Level-aware right-dilation: for each grey cell, extend its lit sub-pixels rightward
// by dilate_amount[level] pixels (capped at `cap`). Seeds from a SNAPSHOT of the row's
// original lit bits (not its own writes) so chained low-level cells don't over-extend.
// `grow` = grey row. Text passes a smaller cap so glyphs stay legible but still white.
static inline void dilate_white_leveled(uint8_t *fb, const uint8_t *grow, int cap) {
    uint8_t src[FB_STRIDE];
    for (int b = 0; b < FB_STRIDE; b++) src[b] = fb[b];   // snapshot original lit bits
    for (int gx = 0; gx < GREY_W; gx++) {
        int amt = dilate_amount[grow[gx]];
        if (amt > cap) amt = cap;
        if (amt == 0) continue;
        int base_p = gx * GREY_SCALE;
        for (int sx = 0; sx < GREY_SCALE; sx++) {
            int p = base_p + sx;
            // seed only if this pixel was lit by the dither (read snapshot, not fb)
            if (src[p >> 3] & (0x80u >> (p & 7))) {
                for (int d = 1; d <= amt; d++) {
                    int pp = p + d;
                    if (pp < FB_WIDTH) fb[pp >> 3] |= (0x80u >> (pp & 7));
                }
            }
        }
    }
}

// Expand the grey buffer into the 1-bit frame_buffer using the dither. Driven by
// output byte: each frame_buffer byte = 8 horizontal pixels = (8/GREY_SCALE) grey
// cells on one grey row. Must run before build_frame_words() each frame.
static void __not_in_flash_func(expand_grey_to_fb)() {
    const int cells_per_byte = 8 / GREY_SCALE;       // 4 at scale 2
    for (int gy = 0; gy < GREY_H; gy++) {
        const uint8_t *grow = &grey_buffer[gy * GREY_W];
        for (int sub = 0; sub < GREY_SCALE; sub++) {
            int fb_row = gy * GREY_SCALE + sub;
            uint8_t *fb = &frame_buffer[fb_row * FB_STRIDE];
            int cell = 0;
            for (int b = 0; b < FB_STRIDE; b++) {
                uint8_t byte = 0;
                for (int k = 0; k < cells_per_byte; k++) {
                    byte = (uint8_t)((byte << GREY_SCALE) | dither[grow[cell++]][dither_orient][sub]);
                }
                fb[b] = byte;
            }
#if WHITE_DILATE
            dilate_white_leveled(fb, grow, dilate_cap);   // per-frame cap (text/Boing lower it)
#endif
        }
    }
}

// Plot a dot in the GREY buffer at grey-cell (gx,gy), value level, clipped.
// Each dot is DOT_W×DOT_H grey cells, anchored at (gx,gy) and extending right/down.
// DOT_W=2 makes every etch point ≥4px wide (≥6px after dilation), so single points
// render robustly full-white regardless of where they fall vs the TV's sample clock
// (a 1-cell/2px point lands on a "bad" phase at some X and reads grey).
#define DOT_W 2
#define DOT_H 1
static inline void plot_dot(int gx, int gy, uint8_t level) {
    for (int dy = 0; dy < DOT_H; dy++) {
        for (int dx = 0; dx < DOT_W; dx++) {
            int rx = gx + dx, ry = gy + dy;
            if (rx >= 0 && rx < GREY_W && ry >= 0 && ry < GREY_H) {
                GREY_SET(grey_buffer, ry, rx, level);
            }
        }
    }
}

// Draw a line from (x0,y0) to (x1,y1) in the grey buffer (Bresenham), so fast CV
// motion draws a continuous curve instead of sparse dots. Endpoints are grey cells.
static void __not_in_flash_func(draw_line)(int x0, int y0, int x1, int y1, uint8_t level) {
    int adx = x1 - x0; if (adx < 0) adx = -adx;
    int ady = y1 - y0; if (ady < 0) ady = -ady;
    int dx =  adx, sx = x0 < x1 ? 1 : -1;
    int dy = -ady, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot_dot(x0, y0, level);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// ─── Bitmap font (5×7 in a 6×8 cell) ─────────────────────────────────────────
// 7 bytes/glyph, one per row, low 5 bits = pixels (bit4 = leftmost column).
// Charset order: A-Z, 0-9, space, ':', '.', '>', '[', ']' (see font_index()).
// Used for the config menu / on-screen text (not a hot path).
static const uint8_t font5x7[][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x11,0x11,0x11,0x11}, // M (inner V near top — distinct from H)
    {0x1C,0x12,0x11,0x11,0x11,0x11,0x11}, // N (lowercase-n shape — arch at top)
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x11,0x15,0x15,0x11}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, // 2
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space
    {0x00,0x04,0x00,0x00,0x00,0x04,0x00}, // :
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C}, // .
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // >
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // [
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // ]
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, // /  (slash)
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, // !
    {0x0E,0x11,0x01,0x06,0x04,0x00,0x04}, // ?
    {0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00}, // *
    {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A}, // #
};

static int font_index(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';        // fold lowercase to uppercase
    if (c >= '0' && c <= '9') return 26 + (c - '0');
    switch (c) { case ' ': return 36; case ':': return 37; case '.': return 38;
                 case '>': return 39; case '[': return 40; case ']': return 41;
                 case '/': return 42; case '!': return 43; case '?': return 44;
                 case '*': return 45; case '#': return 46; }
    return 36;  // unknown → space
}

// Draw a text string into grey_buffer at grey-cell (gx,gy) top-left, given level.
// 9 cells per glyph advance (monospaced), 5px glyph. Text dilates by 4 (full white), so
// 5px+4 = ends col8, next glyph at col9 → a clean 1px gap (no merge). Clipped.
static void draw_text(int gx, int gy, const char *s, uint8_t level) {
    for (; *s; s++) {
        const uint8_t *g = font5x7[font_index(*s)];
        for (int row = 0; row < 7; row++) {
            int ry = gy + row;
            if (ry < 0 || ry >= GREY_H) continue;
            uint8_t bits = g[row];
            for (int col = 0; col < 5; col++) {
                if (bits & (0x10 >> col)) {
                    int rx = gx + col;
                    if (rx >= 0 && rx < GREY_W) GREY_SET(grey_buffer, ry, rx, level);
                }
            }
        }
        gx += 9;   // monospaced advance (5px glyph + 4px dilation = 1px gap)
        if (gx >= GREY_W) break;
    }
}

// Draw a non-negative integer as text at grey-cell (gx,gy).
[[maybe_unused]] static void draw_number(int gx, int gy, int n, uint8_t level) {
    char buf[12]; int i = 0;
    if (n < 0) n = 0;
    do { buf[i++] = '0' + (n % 10); n /= 10; } while (n && i < 11);
    char rev[12]; for (int j = 0; j < i; j++) rev[j] = buf[i-1-j];
    rev[i] = 0;
    draw_text(gx, gy, rev, level);
}

static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Map value from [-2048..2047] to [0..max_out]
static inline int32_t map_adc(int32_t val, int32_t max_out) {
    // val range: -2048..2047 (4096 steps)
    // output: 0..max_out
    int32_t shifted = val + 2048;          // 0..4095
    return (shifted * max_out) / 4095;
}

// Etch-a-sketch position: knob sets a base position, CV adds an offset around it.
// knob 0..4095 maps directly to 0..max_out (the resting position with no CV).
// cv -2048..2047 adds a bipolar offset (full CV swing = ±half the screen).
// So a centred knob with no patch = centre of screen, not the corner.
static inline int32_t map_knob_offset_cv(int32_t cv, int32_t knob, int32_t max_out) {
    int32_t base   = (knob * max_out) / 4095;   // 0..max_out from knob
    int32_t offset = (cv * max_out) / 4095;     // ±max_out/2 from CV (cv is ±2048)
    return base + offset;
}

// Decrement every grey cell toward black (one phosphor-fade step).
static inline void fade_step() {
    for (int i = 0; i < GREY_SIZE; i++) if (grey_buffer[i]) grey_buffer[i]--;
}
// Increment every grey cell toward white (one bloom step).
static inline void bloom_step() {
    for (int i = 0; i < GREY_SIZE; i++) if (grey_buffer[i] < GREY_LEVELS-1) grey_buffer[i]++;
}
// Corrupt: dramatic glitch — shift rows by random amounts, smear, and inject noise
// blocks. Rewrites grey_buffer in place each frame for a churning broken-signal look.
static void __not_in_flash_func(corrupt_step)() {
    for (int gy = 0; gy < GREY_H; gy++) {
        uint32_t r = lcg_rand();
        if ((r & 7) == 0) {                          // ~1/8 rows: blank or random-fill
            uint8_t v = (r >> 8) & 1 ? (GREY_LEVELS-1) : 0;
            for (int gx = 0; gx < GREY_W; gx++) grey_buffer[gy*GREY_W + gx] = v;
        } else {                                     // else: cyclically shift the row
            int sh = (int)((r >> 4) % GREY_W);
            uint8_t *row = &grey_buffer[gy*GREY_W];
            // rotate-by-sh via a one-pass temp on the stack (GREY_W=180)
            uint8_t tmp[GREY_W];
            for (int gx = 0; gx < GREY_W; gx++) tmp[gx] = row[(gx+sh) % GREY_W];
            for (int gx = 0; gx < GREY_W; gx++) row[gx] = tmp[gx];
            if ((r & 0x30) == 0) {                    // occasional noise speckle in the row
                for (int n = 0; n < 8; n++) row[(lcg_rand()) % GREY_W] = lcg_rand() % GREY_LEVELS;
            }
        }
    }
}

// Run one low-level effect (fx = FX_*). Returns true if it produced a FINISHED frame
// (caller should expand_grey_to_fb() + return). FX_SWAP_XY sets swap_xy_out and returns
// false (drawing continues, transposed). st = this trigger source's effect state.
static bool __not_in_flash_func(run_fx)(int fx, FxState &st, bool &swap_xy_out) {
    switch (fx) {
        case FX_STROBE:                 // rapid whole-screen flash; freeze drawing
            st.strobe_invert = (st.fx_phase & 2);  // toggle every 2 frames (~12Hz)
            return true;
        case FX_FREEZE_BLACK:           // freeze + fade to black ~0.3s
            if ((st.fx_phase & 3) == 0) fade_step();
            return true;
        case FX_FREEZE_WHITE:           // freeze + bloom to white ~0.3s
            if ((st.fx_phase & 3) == 0) bloom_step();
            return true;
        case FX_SNOW:                   // fill with random levels each frame
            for (int i = 0; i < GREY_SIZE; i++)
                grey_buffer[i] = (uint8_t)(lcg_rand() % GREY_LEVELS);
            return true;
        case FX_CORRUPT:                // dramatic glitch
            corrupt_step();
            return true;
        case FX_SWAP_XY:                // transpose etch / reverse scope; keep drawing
            swap_xy_out = true;
            return false;
        case FX_ROLL:                   // vertical roll; keep drawing under it
            roll_step();
            return false;
    }
    return false;
}

// Apply a configured behaviour for one trigger source (switch-DOWN or Pu2).
//   held   = trigger currently active (level)
//   rising = trigger just became active this frame (edge)
// Returns true if it produced a finished frame. st = that source's effect state.
static bool __not_in_flash_func(apply_behaviour)(int bhv, bool held, bool rising,
                                                 FxState &st, bool &swap_xy_out) {
    if (!held) return false;
    st.fx_phase++;
    switch (bhv) {
        case BHV_INVERT:
            st.strobe_invert = true;          // whole-frame invert while held
            return false;
        case BHV_CLS:
            if (rising) {                     // clear once on press
                memset(grey_buffer, 0, GREY_SIZE);
                scope_x = 0; scope_acc = 0; fade_cols = 0; etch_have_prev = false;
            }
            return false;
        case BHV_CYCLE_FX:
            if (rising) { st.fx_index = (st.fx_index + 1) % FX_COUNT; st.fx_phase = 0; }
            return run_fx(st.fx_index, st, swap_xy_out);
        case BHV_RANDOM_FX:
            if (rising) { st.fx_index = lcg_rand() % FX_COUNT; st.fx_phase = 0; }
            return run_fx(st.fx_index, st, swap_xy_out);
        default:                              // FX1..FX7 = a specific fixed effect
            return run_fx(BHV_FX_MAP[bhv - BHV_FX1_STROBE], st, swap_xy_out);
    }
}

// Config menu render (Core 1). X knob = switch-DOWN behaviour, Y = Pu2 behaviour.
static void __not_in_flash_func(draw_menu)() {
    text_mode = true;                 // crisp text — no white-dilation this frame
    memset(grey_buffer, 0, GREY_SIZE);
    draw_text(6,  14, "CONFIG", GREY_LEVELS - 1);   // whole menu nudged down one char line
    // Three independent triggers: DOWN (Main knob), PU1 (Knob X), PU2 (Knob Y).
    draw_text(6,  34, "DOWN",   GREY_LEVELS - 2);
    draw_text(6,  44, BHV_NAMES[shared.cfg_sw],  GREY_LEVELS - 1);
    draw_text(6,  68, "PU1",    GREY_LEVELS - 2);
    draw_text(6,  78, BHV_NAMES[shared.cfg_pu1], GREY_LEVELS - 1);
    draw_text(6, 102, "PU2",    GREY_LEVELS - 2);
    draw_text(6, 112, BHV_NAMES[shared.cfg_pu2], GREY_LEVELS - 1);
}

// COMET: a round comet bouncing around, leaving a phosphor tail. Main knob / CV In 1 set
// travel speed; CV In 2 sets tail length (how slowly the trail fades). Position is 8.8.
#define COMET_R 3            // comet radius in cells (round blob)
[[maybe_unused]] static void __not_in_flash_func(screensaver_bounce)() {
    static int32_t sx = (GREY_W/2)<<8, sy = (GREY_H/2)<<8;   // 8.8 position
    static int dirx = 1, diry = 1;                            // travel direction
    static int fade_ctr = 0;
    shared.alt_cv1 = 0;                          // passive: no CVOut1

    // Speed = Main knob + CV In 1 (with a small base so it always drifts). ~3x faster.
    int32_t speed = 72 + (shared.knob_main + shared.cv_x) / 8;    // 8.8 cell/frame
    if (speed < 24) speed = 24;
    // Tail length = Knob Y + CV In 2: fade every N frames (bigger N = longer tail).
    int tailc = shared.knob_y + (shared.cv_y + 2048);            // 0..~6143
    int fade_every = 1 + tailc / 700;                            // 1..~9 frames per fade step
    if (++fade_ctr >= fade_every) { fade_ctr = 0; fade_step(); }  // trail decay

    // Move (8.8) and bounce off the edges, keeping inside by the radius.
    sx += dirx * speed; sy += diry * speed;
    int32_t lo = COMET_R << 8, hix = (GREY_W-1-COMET_R)<<8, hiy = (GREY_H-1-COMET_R)<<8;
    if (sx <= lo)  { sx = lo;  dirx = 1; }
    if (sx >= hix) { sx = hix; dirx = -1; }
    if (sy <= lo)  { sy = lo;  diry = 1; }
    if (sy >= hiy) { sy = hiy; diry = -1; }

    // Draw a round filled blob at full white.
    int cx = sx>>8, cy = sy>>8;
    for (int dy = -COMET_R; dy <= COMET_R; dy++)
        for (int dx = -COMET_R; dx <= COMET_R; dx++) {
            if (dx*dx + dy*dy > COMET_R*COMET_R) continue;
            int gx = cx+dx, gy = cy+dy;
            if (gx>=0&&gx<GREY_W&&gy>=0&&gy<GREY_H) GREY_SET(grey_buffer, gy, gx, GREY_LEVELS-1);
        }
}

// ─── Asteroids screensaver ────────────────────────────────────────────────────
// Wrap-around play area. A small triangle ship moves forward constantly; the Main knob
// sets turn rate (centre = straight). Switch DOWN fires a bullet from the nose (no wrap,
// dies at edge). Parent comets drift and wrap; shooting a parent splits it into two
// children, shooting a child destroys it. Max parents grows +1 every 3 hits. A ship-
// comet collision shows the HITS score then resets. Switch UP shows the font charset.
// Positions are 16.8 fixed point (x256). Heading is 0..255 (256 = full turn).
#define AST_MAXCOMET 16      // pool size (parents + children)
#define AST_NBULLET  5
#define SCR_W256     (GREY_W << 8)
#define SCR_H256     (GREY_H << 8)
// sin table, 256 steps, amplitude ±256 (8.8). cos = sin(a+64).
static const int16_t sin256[256] = {
   0,6,13,19,25,31,38,44,50,56,62,68,74,80,86,92,98,104,109,115,121,126,132,137,142,
  147,152,157,162,167,172,177,181,185,190,194,198,202,206,209,213,216,220,223,226,229,
  231,234,237,239,241,243,245,247,248,250,251,252,253,254,255,255,255,256,255,255,255,
  254,253,252,251,250,248,247,245,243,241,239,237,234,231,229,226,223,220,216,213,209,
  206,202,198,194,190,185,181,177,172,167,162,157,152,147,142,137,132,126,121,115,109,
  104,98,92,86,80,74,68,62,56,50,44,38,31,25,19,13,6,0,-6,-13,-19,-25,-31,-38,-44,-50,
  -56,-62,-68,-74,-80,-86,-92,-98,-104,-109,-115,-121,-126,-132,-137,-142,-147,-152,
  -157,-162,-167,-172,-177,-181,-185,-190,-194,-198,-202,-206,-209,-213,-216,-220,-223,
  -226,-229,-231,-234,-237,-239,-241,-243,-245,-247,-248,-250,-251,-252,-253,-254,-255,
  -255,-255,-256,-255,-255,-255,-254,-253,-252,-251,-250,-248,-247,-245,-243,-241,-239,
  -237,-234,-231,-229,-226,-223,-220,-216,-213,-209,-206,-202,-198,-194,-190,-185,-181,
  -177,-172,-167,-162,-157,-152,-147,-142,-137,-132,-126,-121,-115,-109,-104,-98,-92,
  -86,-80,-74,-68,-62,-56,-50,-44,-38,-31,-25,-19,-13,-6 };
static inline int sin_a(uint8_t a){ return sin256[a]; }
static inline int cos_a(uint8_t a){ return sin256[(uint8_t)(a + 64)]; }

// ── FOURTRIG: four trigger inputs stamp a "thing" into their screen quadrant ─────────
// AudioIn1, AudioIn2, PulseIn1, PulseIn2 are all trigger inputs. Each fires a stamp into
// its own quadrant (pulled in toward screen centre). Stamps decay through the 5 greys to
// black over a few frames. Knob X = bank (5), Knob Y (+CV2) = set within the bank
// (5 sets of 4), Main (+CV1) = CHAOS.
//   Banks are ordered icons/pictures FIRST, words LAST:
//     Bank 0 = SHAPES, Bank 1 = MUSIC/HIT glyphs, Bank 2 = SYMBOLS (more icons),
//     Bank 3 = WORDS, Bank 4 = EMPHASIS.
// CHAOS 0 → tidy centres, uniform size, no glitch. Rising chaos → position jitter +
// occasional quadrant swap, GROWING + randomised size, and (past ~50%) a growing chance
// that a stamp ALSO fires a screen glitch (corrupt / snow-fleck / roll kick).
#define FT_BANKS 5
#define FT_SETS  5
enum { FT_BANK_SHAPES = 0, FT_BANK_MUSIC = 1, FT_BANK_SYMBOLS = 2,
       FT_BANK_WORDS = 3, FT_BANK_EMPH = 4 };

// Bank 3 (WORDS) — drum/shout style hits, one per trigger quadrant.
static const char *const FT_WORDS[FT_SETS][4] = {
    { "HAT",  "CLAP", "KICK", "SNARE"    },
    { "TSH",  "CHCK", "BOOOM","CRACK"    },
    { "WOW",  "YES",  "BOOM", "NO"       },
    { "LOVE", "SEX",  "HATE", "RELIGION" },
    { "EVERY","BODY", "SAY",  "YEAH"     },
};

// Bank 4 (EMPHASIS) — short punchy text stabs.
static const char *const FT_EMPH[FT_SETS][4] = {
    { "YEAH!", "NOPE", "OW",   "HUH?"   },
    { "!!!",   "???",  "***",  "###"    },
    { "STOP",  "GO",   "WAIT", "NOW"    },
    { "UH",    "OH",   "AH",   "EH"     },
    { "BANG",  "POW",  "ZAP",  "WHAM"   },
};

// ── Thick (2×2) drawing primitives for the shape stamps ──────────────────────────────
// plot_dot is 2×1 grey cells (DOT_W×DOT_H); the shapes want a heftier 2×2 look, so ftD
// stamps a 2×2 cell block and ftL draws a thick line out of it. Used by all ft_* shapes.
static inline void ftD(int gx, int gy, uint8_t level) {
    plot_dot(gx, gy,     level);       // 2×1
    plot_dot(gx, gy + 1, level);       // → 2×2
}

static inline void ft_cell(int gx, int gy, uint8_t level) {   // set one grey cell, clipped
    if (gx >= 0 && gx < GREY_W && gy >= 0 && gy < GREY_H) GREY_SET(grey_buffer, gy, gx, level);
}

// Draw a word centred on grey (cx,cy) at `level`, DOUBLED: each 5×7 glyph pixel becomes a
// 2×2 grey block, so strokes are solid (reach full white) and the word reads larger.
// Vertical strokes get one extra grey column so uprights read a touch bolder (height
// unchanged). Cell = 10 wide (5px×2) + gap; ADV per glyph = 13 (extra px for the fatter
// uprights so glyphs don't touch); 14 tall (7×2).
static void __not_in_flash_func(ft_draw_word)(int cx, int cy, const char *s, uint8_t level) {
    int n = 0; for (const char *p = s; *p; p++) n++;
    const int ADV = 13;                        // per-glyph advance (10px glyph + fat gap)
    int w = n * ADV - 3;                        // last glyph has no trailing gap
    int gx0 = cx - w / 2, gy0 = cy - 7;        // 14 tall → top = cy-7
    int gx = gx0;
    for (; *s; s++) {
        const uint8_t *g = font5x7[font_index(*s)];
        for (int row = 0; row < 7; row++) {
            uint8_t bits = g[row];
            uint8_t above = (row > 0) ? g[row-1] : 0;
            uint8_t below = (row < 6) ? g[row+1] : 0;
            for (int col = 0; col < 5; col++) {
                uint8_t m = 0x10 >> col;
                if (bits & m) {
                    int px = gx + col * 2, py = gy0 + row * 2;
                    ftD(px, py, level);        // ftD is 2×2 → doubled pixel
                    // Vertical-stroke member (lit above or below) → +1 grey column right.
                    if ((above & m) || (below & m)) {
                        ft_cell(px + 2, py,     level);
                        ft_cell(px + 2, py + 1, level);
                    }
                }
            }
        }
        gx += ADV;
    }
}
static void __not_in_flash_func(ftL)(int x0, int y0, int x1, int y1, uint8_t level) {
    int adx = x1 - x0; if (adx < 0) adx = -adx;
    int ady = y1 - y0; if (ady < 0) ady = -ady;
    int dx =  adx, sx = x0 < x1 ? 1 : -1;
    int dy = -ady, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        ftD(x0, y0, level);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// ── Vector shape stamps (SHAPES / MUSIC / SYMBOLS banks) ──────────────────────────────
// All draw centred on grey (cx,cy) with radius r, into grey_buffer at `level`. Use the
// thick ftL/ftD primitives so lines read as 2×2.
static void __not_in_flash_func(ft_circle)(int cx, int cy, int r, uint8_t level) {
    for (int a = 0; a < 256; a += 8) {             // 32 segments over the full 256-step circle
        int x0 = cx + (r * cos_a((uint8_t)a)      >> 8), y0 = cy + (r * sin_a((uint8_t)a)      >> 8);
        int x1 = cx + (r * cos_a((uint8_t)(a + 8)) >> 8), y1 = cy + (r * sin_a((uint8_t)(a + 8)) >> 8);
        ftL(x0, y0, x1, y1, level);
    }
}
static void __not_in_flash_func(ft_disc)(int cx, int cy, int r, uint8_t level) {
    for (int rr = r; rr > 0; rr -= 2) ft_circle(cx, cy, rr, level);  // filled circle
    ftD(cx, cy, level);
}
static void __not_in_flash_func(ft_square)(int cx, int cy, int r, uint8_t level) {
    ftL(cx-r, cy-r, cx+r, cy-r, level); ftL(cx+r, cy-r, cx+r, cy+r, level);
    ftL(cx+r, cy+r, cx-r, cy+r, level); ftL(cx-r, cy+r, cx-r, cy-r, level);
}
static void __not_in_flash_func(ft_triangle)(int cx, int cy, int r, uint8_t level) {
    ftL(cx, cy-r, cx+r, cy+r, level); ftL(cx+r, cy+r, cx-r, cy+r, level);
    ftL(cx-r, cy+r, cx, cy-r, level);
}
static void __not_in_flash_func(ft_star)(int cx, int cy, int r, uint8_t level) {
    // 5-point star via the classic {0,2,4,1,3} vertex skip.
    int px[5], py[5];
    for (int i = 0; i < 5; i++) {
        uint8_t a = (uint8_t)(192 + i * 256 / 5);          // start pointing up
        px[i] = cx + (r * cos_a(a) >> 8); py[i] = cy + (r * sin_a(a) >> 8);
    }
    int order[6] = {0,2,4,1,3,0};
    for (int i = 0; i < 5; i++)
        ftL(px[order[i]], py[order[i]], px[order[i+1]], py[order[i+1]], level);
}
static void __not_in_flash_func(ft_wavy)(int cx, int cy, int r, uint8_t level) {
    for (int row = -1; row <= 1; row++) {                   // three wavy lines
        int y = cy + row * (r/2);
        for (int x = -r; x < r; x++) {
            int yy = y + (sin_a((uint8_t)((x + r) * 8)) >> 6);
            ftD(cx + x, yy, level);
        }
    }
}
static void __not_in_flash_func(ft_cross)(int cx, int cy, int r, uint8_t level) {
    ftL(cx-r, cy, cx+r, cy, level); ftL(cx, cy-r, cx, cy+r, level);
}
static void __not_in_flash_func(ft_ninedots)(int cx, int cy, int r, uint8_t level) {
    for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++)
            ftD(cx + i*r/2, cy + j*r/2, level);
}
static void __not_in_flash_func(ft_hash)(int cx, int cy, int r, uint8_t level) {
    ftL(cx-r, cy-r/2, cx+r, cy-r/2, level); ftL(cx-r, cy+r/2, cx+r, cy+r/2, level);
    ftL(cx-r/2, cy-r, cx-r/2, cy+r, level); ftL(cx+r/2, cy-r, cx+r/2, cy+r, level);
}
// Arrow pointing in direction dir (0=left,1=right,2=up,3=down).
static void __not_in_flash_func(ft_arrow)(int cx, int cy, int r, int dir, uint8_t level) {
    int dx = (dir==0)?-1:(dir==1)?1:0, dy = (dir==2)?-1:(dir==3)?1:0;
    int tx = cx + dx*r, ty = cy + dy*r, bx = cx - dx*r, by = cy - dy*r;
    ftL(bx, by, tx, ty, level);
    int px = -dy, py = dx;                                  // perpendicular for the head barbs
    ftL(tx, ty, tx - dx*r/2 + px*r/2, ty - dy*r/2 + py*r/2, level);
    ftL(tx, ty, tx - dx*r/2 - px*r/2, ty - dy*r/2 - py*r/2, level);
}
static void __not_in_flash_func(ft_sun)(int cx, int cy, int r, uint8_t level) {
    ft_circle(cx, cy, r/2, level);
    for (int a = 0; a < 256; a += 32) {                     // 8 rays around the full circle
        uint8_t aa = (uint8_t)a;
        int x0 = cx + (r*3/4 * cos_a(aa) >> 8), y0 = cy + (r*3/4 * sin_a(aa) >> 8);
        int x1 = cx + (r     * cos_a(aa) >> 8), y1 = cy + (r     * sin_a(aa) >> 8);
        ftL(x0, y0, x1, y1, level);
    }
}
static void __not_in_flash_func(ft_cloud)(int cx, int cy, int r, uint8_t level) {
    ft_circle(cx - r/2, cy, r/2, level); ft_circle(cx + r/2, cy, r/2, level);
    ft_circle(cx, cy - r/3, r/2, level);
    ftL(cx-r, cy+r/3, cx+r, cy+r/3, level);
}
static void __not_in_flash_func(ft_rain)(int cx, int cy, int r, uint8_t level) {
    ft_cloud(cx, cy - r/2, r, level);
    for (int i = -1; i <= 1; i++) ftL(cx+i*r/2, cy+r/3, cx+i*r/2-2, cy+r, level);
}
static void __not_in_flash_func(ft_snow)(int cx, int cy, int r, uint8_t level) {
    for (int a = 0; a < 256; a += 42) {                     // 6-arm flake (256/6 ≈ 42)
        uint8_t aa = (uint8_t)a;
        int x1 = cx + (r * cos_a(aa) >> 8), y1 = cy + (r * sin_a(aa) >> 8);
        ftL(cx, cy, x1, y1, level);
    }
}
static void __not_in_flash_func(ft_heart)(int cx, int cy, int r, uint8_t level) {
    ft_circle(cx - r/2, cy - r/3, r/2, level); ft_circle(cx + r/2, cy - r/3, r/2, level);
    ftL(cx-r, cy, cx, cy+r, level); ftL(cx+r, cy, cx, cy+r, level);
}
static void __not_in_flash_func(ft_spade)(int cx, int cy, int r, uint8_t level) {
    ft_triangle(cx, cy - r/3, r, level);                   // body
    ftL(cx, cy+r/3, cx, cy+r, level);                      // stem
    ft_circle(cx - r/3, cy + r/4, r/3, level); ft_circle(cx + r/3, cy + r/4, r/3, level);
}
static void __not_in_flash_func(ft_club)(int cx, int cy, int r, uint8_t level) {
    ft_circle(cx, cy - r/2, r/2, level);
    ft_circle(cx - r/2, cy + r/6, r/2, level); ft_circle(cx + r/2, cy + r/6, r/2, level);
    ftL(cx, cy, cx, cy+r, level);
}
static void __not_in_flash_func(ft_diamond)(int cx, int cy, int r, uint8_t level) {
    ftL(cx, cy-r, cx+r, cy, level); ftL(cx+r, cy, cx, cy+r, level);
    ftL(cx, cy+r, cx-r, cy, level); ftL(cx-r, cy, cx, cy-r, level);
}
static void __not_in_flash_func(ft_note)(int cx, int cy, int r, uint8_t level) {
    ft_circle(cx - r/2, cy + r/2, r/3, level);             // note head
    ftL(cx - r/6, cy + r/2, cx - r/6, cy - r, level);      // stem
    ftL(cx - r/6, cy - r, cx + r/2, cy - r + r/3, level);  // flag
}
static void __not_in_flash_func(ft_bolt)(int cx, int cy, int r, uint8_t level) {
    ftL(cx+r/2, cy-r, cx-r/3, cy, level); ftL(cx-r/3, cy, cx+r/4, cy, level);
    ftL(cx+r/4, cy, cx-r/2, cy+r, level);
}
static void __not_in_flash_func(ft_burst)(int cx, int cy, int r, uint8_t level) {
    // 16 spikes around the full circle: radius alternates long/short each segment.
    int prevx = 0, prevy = 0;
    for (int i = 0; i <= 16; i++) {
        uint8_t a = (uint8_t)(i * 16);
        int rr = (i & 1) ? r : r/2;
        int x = cx + (rr * cos_a(a) >> 8), y = cy + (rr * sin_a(a) >> 8);
        if (i) ftL(prevx, prevy, x, y, level);
        prevx = x; prevy = y;
    }
}
static void __not_in_flash_func(ft_rings)(int cx, int cy, int r, uint8_t level) {
    ft_circle(cx, cy, r, level); ft_circle(cx, cy, r*2/3, level); ft_circle(cx, cy, r/3, level);
}
// ── SYMBOLS bank (more icons) ──
static void __not_in_flash_func(ft_face)(int cx, int cy, int r, int mood, uint8_t level) {
    ft_circle(cx, cy, r, level);                           // head
    ftD(cx - r/2, cy - r/4, level); ftD(cx + r/2, cy - r/4, level);  // eyes
    if (mood == 0)      { for (int x=-r/2;x<=r/2;x++) ftD(cx+x, cy+r/2 - (sin_a((uint8_t)((x+r)*4))>>6), level); } // smile
    else if (mood == 1) ftL(cx - r/2, cy + r/3, cx + r/2, cy + r/3, level);   // flat
    else if (mood == 2) { for (int x=-r/2;x<=r/2;x++) ftD(cx+x, cy+r/4 + (sin_a((uint8_t)((x+r)*4))>>6), level); } // frown (raised so it clears the chin)
    else                { ft_circle(cx, cy + r/4, r/4, level); }              // shock (O mouth)
}
static void __not_in_flash_func(ft_plus)(int cx, int cy, int r, uint8_t level) {
    ftL(cx-r, cy, cx+r, cy, level); ftL(cx, cy-r, cx, cy+r, level);
    ft_circle(cx, cy, r, level);                           // plus in a ring
}
static void __not_in_flash_func(ft_x)(int cx, int cy, int r, uint8_t level) {
    ftL(cx-r, cy-r, cx+r, cy+r, level); ftL(cx-r, cy+r, cx+r, cy-r, level);
}
static void __not_in_flash_func(ft_check)(int cx, int cy, int r, uint8_t level) {
    ftL(cx-r, cy, cx-r/4, cy+r, level); ftL(cx-r/4, cy+r, cx+r, cy-r, level);
}
static void __not_in_flash_func(ft_target)(int cx, int cy, int r, uint8_t level) {
    ft_circle(cx, cy, r, level); ft_circle(cx, cy, r/2, level); ftD(cx, cy, level);
}
static void __not_in_flash_func(ft_bolt2)(int cx, int cy, int r, uint8_t level) { ft_bolt(cx,cy,r,level); }
static void __not_in_flash_func(ft_house)(int cx, int cy, int r, uint8_t level) {
    ft_square(cx, cy + r/3, r*2/3, level);                 // body
    ftL(cx-r, cy - r/3, cx, cy-r, level); ftL(cx, cy-r, cx+r, cy-r/3, level);  // roof
}
static void __not_in_flash_func(ft_eye)(int cx, int cy, int r, uint8_t level) {
    // Almond outline: two arcs meeting at the corners (±r, 0), bulging ±r/2 at the centre.
    for (int x = -r; x <= r; x++) {
        int lid = (r/2) * (r*r - x*x) / (r*r + 1);         // 0 at corners, ~r/2 at centre
        ftD(cx + x, cy - lid, level);
        ftD(cx + x, cy + lid, level);
    }
    ft_circle(cx, cy, r/3, level);                         // iris
    ftD(cx, cy, level);                                    // pupil
}
static void __not_in_flash_func(ft_moon)(int cx, int cy, int r, uint8_t level) {
    // Crescent moon: the outer circle's left-facing arc, plus an inner arc (a circle offset
    // to the right) that carves the concave edge. Both arcs run from the top tip to bottom
    // tip, so they meet and read as a crescent opening to the right.
    int ir  = r*3/4;               // inner circle radius
    int iox = r/2;                 // inner circle x-offset (to the right)
    // Outer arc: left half of the circle (angles 64..192 = bottom→left→top in this table).
    for (int a = 64; a <= 192; a += 6) {
        uint8_t a0 = (uint8_t)a, a1 = (uint8_t)(a + 6);
        int x0 = cx + (r*cos_a(a0)>>8), y0 = cy + (r*sin_a(a0)>>8);
        int x1 = cx + (r*cos_a(a1)>>8), y1 = cy + (r*sin_a(a1)>>8);
        ftL(x0, y0, x1, y1, level);
    }
    // Inner arc: left half of the offset circle (the concave bite).
    for (int a = 64; a <= 192; a += 6) {
        uint8_t a0 = (uint8_t)a, a1 = (uint8_t)(a + 6);
        int x0 = cx+iox + (ir*cos_a(a0)>>8), y0 = cy + (ir*sin_a(a0)>>8);
        int x1 = cx+iox + (ir*cos_a(a1)>>8), y1 = cy + (ir*sin_a(a1)>>8);
        ftL(x0, y0, x1, y1, level);
    }
    // Close the two tips so the crescent is a continuous outline.
    ftL(cx, cy+r, cx+iox, cy+ir, level);   // bottom tip
    ftL(cx, cy-r, cx+iox, cy-ir, level);   // top tip
}

// Draw one shape/glyph for (bank,set,slot) centred at (cx,cy), radius r.
static void __not_in_flash_func(ft_draw_shape)(int bank, int set, int slot,
                                               int cx, int cy, int r, uint8_t level) {
    if (bank == FT_BANK_SHAPES) {                           // geometric / cards / arrows / …
        switch (set) {
            case 0: (slot==0?ft_circle:slot==1?ft_square:slot==2?ft_triangle:ft_star)(cx,cy,r,level); break;
            case 1: (slot==0?ft_wavy:slot==1?ft_cross:slot==2?ft_ninedots:ft_hash)(cx,cy,r,level); break;
            case 2: ft_arrow(cx,cy,r, slot, level); break;   // slot 0=L 1=R 2=U 3=D
            case 3: (slot==0?ft_sun:slot==1?ft_rain:slot==2?ft_cloud:ft_snow)(cx,cy,r,level); break;
            default:(slot==0?ft_heart:slot==1?ft_spade:slot==2?ft_club:ft_diamond)(cx,cy,r,level); break;
        }
        return;
    }
    if (bank == FT_BANK_MUSIC) {                            // abstract sound glyphs
        switch (set) {
            case 0: (slot==0?ft_note:slot==1?ft_bolt:slot==2?ft_burst:ft_rings)(cx,cy,r,level); break;
            case 1: (slot==0?ft_burst:slot==1?ft_rings:slot==2?ft_note:ft_bolt)(cx,cy,r,level); break;
            case 2: (slot==0?ft_disc:slot==1?ft_circle:slot==2?ft_target:ft_rings)(cx,cy,r,level); break;
            case 3: (slot==0?ft_bolt:slot==1?ft_burst:slot==2?ft_star:ft_note)(cx,cy,r,level); break;
            default:(slot==0?ft_rings:slot==1?ft_disc:slot==2?ft_burst:ft_target)(cx,cy,r,level); break;
        }
        return;
    }
    // FT_BANK_SYMBOLS — more figurative icons.
    switch (set) {
        case 0: ft_face(cx,cy,r, slot, level); break;        // 4 moods
        case 1: (slot==0?ft_check:slot==1?ft_x:slot==2?ft_plus:ft_target)(cx,cy,r,level); break;
        case 2: (slot==0?ft_house:slot==1?ft_eye:slot==2?ft_moon:ft_star)(cx,cy,r,level); break;
        case 3: ft_arrow(cx,cy,r, slot, level); break;       // reuse arrows (diagonal feel via jitter)
        default:(slot==0?ft_bolt2:slot==1?ft_sun:slot==2?ft_snow:ft_diamond)(cx,cy,r,level); break;
    }
}

// Flip the whole grey buffer 180° (upside-down + mirrored) in place — used as FourTrig's
// SWAP effect. In-place reversal fits the buffer exactly (unlike a 90° turn, which would
// have to squash the non-square framebuffer). One frame's flip is enough; repeated calls
// just toggle it back and forth (a churn while held).
static void __not_in_flash_func(ft_flip180)() {
    for (int i = 0, j = GREY_SIZE - 1; i < j; i++, j--) {
        uint8_t t = grey_buffer[i]; grey_buffer[i] = grey_buffer[j]; grey_buffer[j] = t;
    }
}

// FourTrig "glitch" = one of the full VFX set (like normal-boot RANDOM FX), NOT just the
// corrupt/roll/snow subset. Runs effect `fx` for the current frame. Sets *inv for a
// whole-frame invert (STROBE / INVERT). SWAP here means flip 180° (see ft_flip180).
enum { FTFX_STROBE, FTFX_INVERT, FTFX_FADE_BLACK, FTFX_FADE_WHITE, FTFX_SNOW,
       FTFX_CORRUPT, FTFX_ROLL, FTFX_SWAP, FTFX_COUNT };
static void __not_in_flash_func(ft_run_vfx)(int fx, uint32_t ph, bool *inv) {
    switch (fx) {
        case FTFX_STROBE:     *inv = (ph & 2); break;                 // rapid flash
        case FTFX_INVERT:     *inv = true; break;                     // held invert
        case FTFX_FADE_BLACK: if ((ph & 3) == 0) fade_step();  break; // fade to black
        case FTFX_FADE_WHITE: if ((ph & 3) == 0) bloom_step(); break; // bloom to white
        case FTFX_SNOW: {
            for (int i = 0; i < GREY_SIZE; i++)
                grey_buffer[i] = (uint8_t)(lcg_rand() % GREY_LEVELS);
            break;
        }
        case FTFX_CORRUPT:    corrupt_step(); break;
        case FTFX_ROLL:       roll_step();    break;
        case FTFX_SWAP:       if ((ph & 7) == 0) ft_flip180(); break; // flip 180° periodically
    }
}

static void __not_in_flash_func(screensaver_fourtrig)() {
    auto rnd = [](){ return (int)(lcg_rand() & 0x7fffffff); };
    dilate_cap = 3;                         // heavier strokes (shapes are thick 2×2 too)

    // Fade the whole buffer toward black one level every other frame (~10-frame trail).
    static uint8_t phase = 0;
    phase++;
    if (phase & 1) {
        for (int i = 0; i < GREY_SIZE; i++) if (grey_buffer[i]) grey_buffer[i]--;
    }

    // Controls: raw alt-mode knobs read here, with CV folded in (Core 0 leaves them raw for
    // this hybrid). knob_x/y are 0..4095; cv_x/cv_y are ±2048 — clamp after summing.
    int bank = clamp(shared.knob_x * FT_BANKS / 4096, 0, FT_BANKS - 1);      // X → bank
    int ysel = clamp((shared.knob_y + shared.cv_y) * FT_SETS / 4096, 0, FT_SETS - 1); // Y+CV2 → set
    int chaos = clamp((shared.knob_main + shared.cv_x) * 100 / 4096, 0, 100);// Main+CV1 → chaos %

    // Read-and-clear the four trigger latches (Core 0 sets the rising edges).
    bool trig[4];
    trig[0] = shared.ain1_rising; shared.ain1_rising = false;   // Audio In 1 → quadrant 0 (TL)
    trig[1] = shared.ain2_rising; shared.ain2_rising = false;   // Audio In 2 → quadrant 1 (TR)
    trig[2] = shared.pu1_rising;  shared.pu1_rising  = false;   // Pulse In 1 → quadrant 2 (BL)
    trig[3] = shared.pu2_rising;  shared.pu2_rising  = false;   // Pulse In 2 → quadrant 3 (BR)

    // Quadrant centres, pulled ~40% of the way in toward screen centre so the default
    // placement sits closer to the middle of the screen (not out in the corners).
    const int SCX = GREY_W/2, SCY = GREY_H/2;
    auto pull = [](int q, int c){ return c + (q - c) * 6 / 10; };   // 0.6 toward centre
    const int qx[4] = { pull(GREY_W/4,SCX), pull(GREY_W*3/4,SCX), pull(GREY_W/4,SCX), pull(GREY_W*3/4,SCX) };
    const int qy[4] = { pull(GREY_H/4,SCY), pull(GREY_H/4,SCY),   pull(GREY_H*3/4,SCY), pull(GREY_H*3/4,SCY) };

    for (int i = 0; i < 4; i++) {
        if (!trig[i]) continue;

        // CHAOS: which quadrant actually gets drawn (swap chance rises with chaos).
        int q = i;
        if (chaos > 0 && rnd() % 100 < chaos / 2) q = rnd() & 3;   // up to 50% swap at max

        int cx = qx[q], cy = qy[q];
        // Position jitter grows with chaos (±up to ~a quarter-quadrant at max).
        if (chaos > 0) {
            int j = chaos * GREY_W / 400;                          // 0..~45 px at chaos 100
            cx += (rnd() % (2*j + 1)) - j;
            cy += (rnd() % (2*j + 1)) - j;
        }
        cx = clamp(cx, 14, GREY_W - 14);
        cy = clamp(cy, 10, GREY_H - 10);

        // Size GROWS with chaos AND is randomised: base rises from ~10 (chaos 0) toward
        // ~20 (chaos 100), plus a ± wobble that also widens with chaos.
        int base = 10 + chaos * 10 / 100;                         // 10..20
        int wob  = chaos / 8;                                     // ±0..12
        int r = base + (wob ? (rnd() % (2*wob + 1)) - wob : 0);
        if (r < 6)  r = 6;
        if (r > 24) r = 24;

        const uint8_t white = GREY_LEVELS - 1;
        if      (bank == FT_BANK_WORDS) ft_draw_word(cx, cy, FT_WORDS[ysel][i], white);
        else if (bank == FT_BANK_EMPH)  ft_draw_word(cx, cy, FT_EMPH[ysel][i],  white);
        else                            ft_draw_shape(bank, ysel, i, cx, cy, r, white);

        // Past ~50% chaos, a stamp can ALSO fire a random VFX (one of the full set, like
        // normal-boot RANDOM FX). Chance rises to ~50% at max chaos.
        if (chaos > 50) {
            int gp = (chaos - 50);                                 // 0..50
            if (rnd() % 100 < gp) {
                bool inv = false;
                // phase 0 → the phase-gated effects (fade/flip) fire on this one-shot hit.
                ft_run_vfx(rnd() % FTFX_COUNT, 0, &inv);
                if (inv) effect_invert = true;
            }
        }
        // OUT2 pulse on any trigger (Core 0 gates on ast_gate_seq change).
        shared.ast_gate_seq = shared.ast_gate_seq + 1;
    }

    // Momentary Switch DOWN = held VFX (as in normal boot, where DOWN runs an effect).
    // Picks one random effect from the full set on press and holds/churns it while down.
    static bool ft_down_prev = false;
    static int  ft_down_fx   = 0;
    bool ft_down = (shared.sw_position == 2);
    if (ft_down && !ft_down_prev) ft_down_fx = rnd() % FTFX_COUNT;   // re-roll on each press
    if (ft_down) {
        bool inv = false;
        ft_run_vfx(ft_down_fx, phase, &inv);
        if (inv) effect_invert = true;
    }
    ft_down_prev = ft_down;
}

static void __not_in_flash_func(screensaver_asteroids)() {
    struct Comet  { int32_t x, y, vx, vy; uint8_t active; uint8_t gen; }; // gen 0=parent,1=child
    struct Bullet { int32_t x, y, vx, vy; int life; };  // life 0 = inactive
    static bool init = false;
    static int32_t  shipx, shipy; static uint8_t heading;
    static Comet  comets[AST_MAXCOMET];
    static Bullet bullets[AST_NBULLET];
    static bool   prev_down = false;
    static int    hits = 0;        // comets shot this life
    static int    dead = 0;        // >0: showing the death "HITS n" screen (frames)
    static int    parent_max = 3;  // max parent (gen-0) comets; +1 every 3 hits
    static int    note = 0;        // current pitch (semitones above base); climbs per hit
    static int    arp = 0;         // >0: crash arpeggio running (descends note → 0)

    // Bump the CV gate sequence so Core 0 emits a gate pulse (a hit or an arp step).
    auto gate_pulse = [](){ shared.ast_gate_seq = shared.ast_gate_seq + 1; };

    auto rnd = [](){ return (int)(lcg_rand() & 0x7fffffff); };
    // initialise a parent comet at a random edge with a random drift
    auto make_parent = [&](Comet &c){
        if (rnd() & 1) { c.x = (rnd()%2)?0:SCR_W256-256; c.y = (rnd()%GREY_H)<<8; }
        else           { c.y = (rnd()%2)?0:SCR_H256-256; c.x = (rnd()%GREY_W)<<8; }
        c.vx = ((rnd()%128) - 64) * 4;
        c.vy = ((rnd()%128) - 64) * 4;
        if (c.vx==0 && c.vy==0) c.vx = 128;
        c.active = 1; c.gen = 0;
    };
    auto count_parents = [&](){ int n=0; for (auto &c:comets) if (c.active && c.gen==0) n++; return n; };
    auto spawn_slot = [&]()->Comet*{ for (auto &c:comets) if (!c.active) return &c; return nullptr; };

    if (!init) {
        init = true;
        note = 0; shared.ast_note = 0;
        shipx = SCR_W256/2; shipy = SCR_H256/2; heading = 192;  // pointing up
        for (auto &c : comets) c.active = 0;
        for (int i = 0; i < parent_max; i++) { Comet *c = spawn_slot(); if (c) make_parent(*c); }
        for (auto &b : bullets) b.life = 0;
    }

    // (Switch UP is handled by the alt-boot selector before we get here, so the game
    //  only runs when the switch is MID or DOWN.)

    // ── Death screen: show HITS while the pitch arpeggiates DOWN to 0 (gate per step),
    //    then hold briefly and resume. ──
    if (arp > 0) {
        text_mode = true;
        arp++;
        if (note <= 0) { arp = 0; dead = 60; }        // nothing to descend → hold HITS
        else if ((arp % 4) == 0) {                    // one step every 4 frames (~80ms)
            note--; shared.ast_note = note; gate_pulse();
            if (note == 0) { arp = 0; dead = 60; }    // arp done → hold HITS ~1.2s
        }
        memset(grey_buffer, 0, GREY_SIZE);
        draw_text(40, 40, "HITS", GREY_LEVELS-1);
        draw_number(60, 56, hits, GREY_LEVELS-1);
        return;
    }

    // ── Post-arpeggio hold: keep HITS up briefly, then reset the life. ──
    if (dead > 0) {
        text_mode = true;
        dead--;
        memset(grey_buffer, 0, GREY_SIZE);
        draw_text(40, 40, "HITS", GREY_LEVELS-1);
        draw_number(60, 56, hits, GREY_LEVELS-1);
        if (dead == 0) {                 // resume: reset life (parent_max back to 3)
            hits = 0; parent_max = 3;
            note = 0; shared.ast_note = 0;
            shipx = SCR_W256/2; shipy = SCR_H256/2; heading = 192;
            for (auto &c : comets) c.active = 0;
            for (int i = 0; i < parent_max; i++) { Comet *c = spawn_slot(); if (c) make_parent(*c); }
            for (auto &b : bullets) b.life = 0;
        }
        return;
    }

    // ── Input: Main knob → turn rate (centre = straight); DOWN edge → fire ──
    int32_t turn = shared.knob_main - 2048;        // -2048..+2047
    heading = (uint8_t)(heading + turn / 512);     // hard over ≈ ±4 steps/frame

    bool down = (shared.sw_position == 2);
    bool fire = (down && !prev_down);              // switch DOWN edge
    if (shared.ast_fire) { fire = true; shared.ast_fire = false; }  // PU1 fire (read-clear)
    if (fire) {
        for (auto &b : bullets) if (!b.life) {
            b.x = shipx; b.y = shipy;
            b.vx = cos_a(heading) * 6; b.vy = sin_a(heading) * 6;  // fast, from nose
            b.life = 1; break;
        }
    }
    prev_down = down;

    // ── Advance ship (constant forward), wrap ──
    shipx += cos_a(heading) * 2; shipy += sin_a(heading) * 2;      // slow cruise
    shipx = (shipx + SCR_W256) % SCR_W256;
    shipy = (shipy + SCR_H256) % SCR_H256;

    // ── Advance comets, wrap (active only) ──
    for (auto &c : comets) {
        if (!c.active) continue;
        c.x = (c.x + c.vx + SCR_W256) % SCR_W256;
        c.y = (c.y + c.vy + SCR_H256) % SCR_H256;
    }

    // ── Advance bullets (no wrap; die at edge); check comet hits ──
    // Parent (gen 0) hit → splits into 2 children (gen 1). Child hit → destroyed.
    // Hit radius scales with size: parent r≈7 (r²≤49), child r≈4 (r²≤16).
    for (auto &b : bullets) {
        if (!b.life) continue;
        b.x += b.vx; b.y += b.vy;
        if (b.x < 0 || b.x >= SCR_W256 || b.y < 0 || b.y >= SCR_H256) { b.life = 0; continue; }
        for (auto &c : comets) {
            if (!c.active) continue;
            int dx = (b.x - c.x) >> 8, dy = (b.y - c.y) >> 8;
            int r2 = (c.gen == 0) ? 49 : 16;
            if (dx*dx + dy*dy <= r2) {
                b.life = 0; hits++;
                note++; shared.ast_note = note; gate_pulse();  // pitch climbs a semitone
                if (c.gen == 0) {                 // split into two children
                    int32_t ovx = c.vx, ovy = c.vy;
                    c.gen = 1;                    // reuse this slot as child 1
                    c.vx = ovx - ovy/2; c.vy = ovy + ovx/2;   // veer one way
                    Comet *c2 = spawn_slot();     // child 2 veers the other
                    if (c2) { *c2 = c; c2->vx = ovx + ovy/2; c2->vy = ovy - ovx/2; }
                } else {
                    c.active = 0;                 // child destroyed
                }
                break;
            }
        }
    }

    // ── Escalation: max parents = 3 + hits/3 (capped). Top up parents to the max. ──
    parent_max = 3 + hits/3;
    if (parent_max > AST_MAXCOMET - 4) parent_max = AST_MAXCOMET - 4;  // leave room for children
    while (count_parents() < parent_max) { Comet *c = spawn_slot(); if (!c) break; make_parent(*c); }

    // ── Ship vs comet collision → start the crash arpeggio (then HITS screen) ──
    for (auto &c : comets) {
        if (!c.active) continue;
        int dx = (shipx - c.x) >> 8, dy = (shipy - c.y) >> 8;
        int r2 = (c.gen == 0) ? 49 : 16;
        if (dx*dx + dy*dy <= r2) { arp = 1; return; }   // arp runs at top of next frame
    }

    // ── Render ──
    fade_step();   // light phosphor trails
    auto plot = [&](int px, int py, uint8_t lvl){
        px = ((px % GREY_W) + GREY_W) % GREY_W; py = ((py % GREY_H) + GREY_H) % GREY_H;
        GREY_SET(grey_buffer, py, px, lvl);
    };
    // comets: parents ~8×8, children ~4×4
    for (auto &c : comets) {
        if (!c.active) continue;
        int cx = c.x >> 8, cy = c.y >> 8;
        int lo = (c.gen == 0) ? -3 : -1, hi = (c.gen == 0) ? 4 : 2;
        for (int dy = lo; dy <= hi; dy++)
            for (int dx = lo; dx <= hi; dx++)
                plot(cx+dx, cy+dy, GREY_LEVELS-1);
    }
    // bullets
    for (auto &b : bullets) if (b.life) plot(b.x>>8, b.y>>8, GREY_LEVELS-1);
    // ship triangle: nose + two back corners (heading-relative)
    int sx = shipx >> 8, sy = shipy >> 8;
    int nx = sx + (cos_a(heading)*4>>8),        ny = sy + (sin_a(heading)*4>>8);
    uint8_t bl = heading + 96, br = heading + 160;   // back corners ±135°-ish
    int lx = sx + (cos_a(bl)*4>>8), ly = sy + (sin_a(bl)*4>>8);
    int rx = sx + (cos_a(br)*4>>8), ry = sy + (sin_a(br)*4>>8);
    // draw the three edges in the grey buffer
    auto line = [&](int x0,int y0,int x1,int y1){
        int adx=x1-x0; if(adx<0)adx=-adx; int ady=y1-y0; if(ady<0)ady=-ady;
        int sgx=x0<x1?1:-1, sgy=y0<y1?1:-1, err=adx-ady;
        for(;;){ plot(x0,y0,GREY_LEVELS-1); if(x0==x1&&y0==y1)break;
            int e2=2*err; if(e2>-ady){err-=ady;x0+=sgx;} if(e2<adx){err+=adx;y0+=sgy;} }
    };
    line(nx,ny,lx,ly); line(nx,ny,rx,ry); line(lx,ly,rx,ry);
}

// ── BOING: Amiga-style rotating checkered ball ──────────────────────────────────────
// A large red/white checkered sphere spins and bounces around under gravity. The ball is
// generated PROCEDURALLY: at init we build a per-cell lookup for the fixed radius mapping
// each disc cell → (latitude band, longitude angle, shade). Per frame the hot loop is
// just a table read + add spin_phase + parity → GREY_SET, so it's cheap enough for 50fps
// alongside the video loop. Controls (read raw on Core 1): Knob X / CV In 1 = spin speed,
// Knob Y / CV In 2 = horizontal speed, Main = bounce efficiency (centre = no height lost,
// CW = lose, CCW = gain). A floor bounce bumps ast_gate_seq so Core 0 emits a trigger on
// CV Out 2. Positions are 8.8 fixed point.
#define BOING_R      44      // ball radius in grey cells (~88 across; fills the 128 height)
#define BOING_D      (2*BOING_R + 1)
#define BOING_NLON   10      // longitude checker bands (around the vertical axis)
#define BOING_NLAT   7       // latitude checker bands (pole to pole)
#define BOING_TILT   240     // pole-axis tilt (0..255 = full turn; 240 = -16 ≈ -22.5° lean)
#define BOING_WHITE  (GREY_LEVELS - 1)   // 4 (dilation off for the ball, so no washout)
#define BOING_RED    0       // "red" square → fully black (no dither = no strobing)
// Scene: a checkerboard wall (light squares = white/black stipple + black squares) with a
// BLACK drop shadow. The stipple uses only levels 0 and 4 (never dither) so it can't flash.
#define BOING_GRIDSP 18      // wall checker square size in cells
#define BOING_SHOFF  10      // shadow offset (cells) down + right from the ball centre

// Integer sqrt (floor). Small values only (≤ R²), so a simple loop is fine at init.
static uint32_t isqrt_u(uint32_t v) {
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}
// Integer atan2 → 0..255 (256 = full turn), no libm. Uses the octant + a rational approx.
static uint8_t atan2_u8(int y, int x) {
    if (x == 0 && y == 0) return 0;
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int a;   // angle within [0,64] scaled to the first octant-pair
    if (ax >= ay) {
        int r = ay * 64 / (ax ? ax : 1);       // 0..64 (approx atan(ay/ax)*128/pi)
        a = (r * 32) / 64;                      // ~0..32 (0..45°)
    } else {
        int r = ax * 64 / (ay ? ay : 1);
        a = 64 - (r * 32) / 64;                 // 32..64 (45..90°)
    }
    int ang;                                    // map by quadrant into 0..255
    if      (x >= 0 && y >= 0) ang = a;             // Q1  0..64
    else if (x <  0 && y >= 0) ang = 128 - a;       // Q2  64..128
    else if (x <  0 && y <  0) ang = 128 + a;       // Q3  128..192
    else                       ang = 256 - a;       // Q4  192..256
    return (uint8_t)ang;
}

static void __not_in_flash_func(screensaver_boing)() {
    struct Cell { uint8_t on; uint8_t lat; uint8_t lon; uint8_t shade; };
    static Cell     tab[BOING_D * BOING_D];   // ~8KB: per-cell sphere lookup (built once)
    static bool     init = false;
    static int32_t  x, y, vx, vy;             // ball centre, 8.8 fixed
    static uint8_t  spin_phase = 0;           // longitude rotation offset
    static int      spin_dir = 1;             // flips on wall bounce (classic Boing)
    static bool     prev_down = false;        // switch-DOWN edge detect (DOWN = kick too)

    if (!init) {
        init = true;
        // Build the sphere lookup for the fixed radius. The pole axis is TILTED (classic
        // Boing look) by rotating the in-screen (dx,dy) by BOING_TILT before deriving
        // latitude (from the rotated dy') and longitude (from rotated dx' and depth nz).
        const int ct = cos_a(BOING_TILT), st = sin_a(BOING_TILT);   // 8.8 (±256)
        for (int dy = -BOING_R; dy <= BOING_R; dy++) {
            for (int dx = -BOING_R; dx <= BOING_R; dx++) {
                Cell &c = tab[(dy + BOING_R) * BOING_D + (dx + BOING_R)];
                int r2 = dx*dx + dy*dy;
                if (r2 > BOING_R*BOING_R) { c.on = 0; continue; }
                c.on = 1;
                int nz = (int)isqrt_u((uint32_t)(BOING_R*BOING_R - r2));  // 0..R (depth)
                // Tilt: rotate (dx,dy) in the screen plane by BOING_TILT.
                int rx = (dx * ct - dy * st) >> 8;
                int ry = (dx * st + dy * ct) >> 8;
                // Latitude: rotated ry over R → band 0..NLAT-1 (pole to pole).
                int lat = ((ry + BOING_R) * BOING_NLAT) / (BOING_D);
                if (lat < 0) lat = 0;
                if (lat >= BOING_NLAT) lat = BOING_NLAT - 1;
                c.lat = (uint8_t)lat;
                // Longitude: angle around the (tilted) vertical axis from (rx, nz) → 0..255.
                c.lon = atan2_u8(rx, nz);
                // Shade: front (large nz) brighter. 0..3, subtle so checkers stay legible.
                c.shade = (uint8_t)((nz * 4) / (BOING_R + 1));   // 0..3
            }
        }
        x = (GREY_W / 2) << 8;
        y = (BOING_R + 4) << 8;
        vx = 130;                              // gentle base drift
        vy = 0;
    }

    // ── Controls ──────────────────────────────────────────────────────────────
    // Spin momentum = Knob X + CV In 1: sets spin rate AND the kick launch strength, so it
    //   doubles as the "energy" control for a tunable bouncing-ball delay.
    // Bounce efficiency = Main knob + bipolar CV In 2: centre = perfect (bounces forever),
    //   CW = GAIN height, CCW = decays to rest. Horizontal travel = Knob Y.
    int32_t spinc = (shared.knob_x - 2048) + shared.cv_x;   // ±~4095 (0 = centre)
    int32_t spin  = spinc / 192;                            // signed spin rate, ±~21
    int32_t hspd  = (shared.knob_y * 3) / 8;                // horizontal: 0 (stopped) → fast (~3x)
    int32_t effc  = (shared.knob_main - 2048) + shared.cv_y; // Main + bipolar CV2, ±~4095
    int32_t eff   = 256 + effc / 16;                         // ~128..384 (256 = perfect)
    if (eff < 32)  eff = 32;                                 // guard extremes
    if (eff > 480) eff = 480;
    // Kick launch velocity centred on the spin control: CENTRE = 1100 (≈2/3 screen height);
    //   spinning one way gives progressively less, the other way progressively more.
    int32_t kickv = 1100 + spinc / 6;                       // ~420..1780 across the range
    if (kickv < 200)  kickv = 200;
    if (kickv > 2000) kickv = 2000;

    // ── Physics (constant gravity + floor/wall bounce), 8.8 fixed ──
    const int32_t GRAV = 26;                   // fixed gravity
    // Kick UPWARD from PU1 (ast_fire, set by Core 0) OR a momentary switch-DOWN edge.
    //   Applied BEFORE integrating so it lifts off the floor this frame. Launch strength =
    //   kickv (from the spin-momentum control) → tunes how high / how many bounces follow.
    bool down = (shared.sw_position == 2);
    bool kick = false;
    if (shared.ast_fire) { shared.ast_fire = false; kick = true; }
    if (down && !prev_down) kick = true;
    prev_down = down;
    if (kick) vy = -kickv;

    vy += GRAV;
    y  += vy;
    // horizontal: keep current direction, set magnitude from the control speed
    vx = (vx >= 0 ? hspd : -hspd);
    x += vx;

    // Floor / ceiling
    int32_t floor_y = (GREY_H - 1 - BOING_R) << 8;
    int32_t ceil_y  = (BOING_R) << 8;
    if (y >= floor_y && vy >= 0) {             // at/below floor AND not moving up
        y = floor_y;
        if (vy > 0) {                          // a real downward impact → bounce
            vy = -((vy * eff) >> 8);           // reflect, scaled by bounce efficiency
            if (vy < -6000) vy = -6000;        // clamp gained energy (CW) — no runaway
            // Low rest cutoff → the ball keeps making smaller, faster bounces (an
            // accelerating "shortening" ratchet, à la Peaks) before finally settling.
            if (vy > -40) vy = 0;              // below ~0.16 cell/frame → rest on the floor
            else shared.ast_gate_seq = shared.ast_gate_seq + 1;  // bounce → CV Out 2 trigger
        } else {
            vy = 0;                            // resting on the floor
        }
    }
    if (y <= ceil_y) { y = ceil_y; if (vy < 0) vy = -vy; }

    // Walls
    int32_t left_x  = (BOING_R) << 8;
    int32_t right_x = (GREY_W - 1 - BOING_R) << 8;
    if (x <= left_x)  { x = left_x;  vx =  (vx < 0 ? -vx : vx); spin_dir = -spin_dir; }
    if (x >= right_x) { x = right_x; vx = -(vx < 0 ? -vx : vx); spin_dir = -spin_dir; }

    // ── Spin: advance the longitude offset ──
    spin_phase = (uint8_t)(spin_phase + spin * spin_dir);

    // ── CV1 out = ball height: map y (ceil..floor) → +2047 (top) .. -2048 (floor) ──
    {
        int32_t span = floor_y - ceil_y;                       // total vertical travel
        int32_t up   = floor_y - y;                            // 0 at floor .. span at top
        shared.alt_cv1 = (span > 0) ? ((up * 4095) / span - 2048) : 0;
    }

    // ── Render: checkerboard wall, black drop shadow, then the ball. No white-dilation
    //    this screen (ball is solid; keeps the wall stipple crisp). ──
    dilate_cap = 0;
    int cx = x >> 8, cy = y >> 8;

    // 1) Background wall: a checkerboard of "light" and black squares, BOING_GRIDSP cells
    //    per square. The light squares are a HAND STIPPLE — a per-cell checker of white(4)
    //    and black(0) cells. Levels 0 and 4 never dither, so it's rock-steady (a level-2
    //    fill would flash: its dither orientation rotates every 2 frames). Reads as grey.
    for (int gy = 0; gy < GREY_H; gy++) {
        int wy = gy / BOING_GRIDSP;
        for (int gx = 0; gx < GREY_W; gx++) {
            int wx = gx / BOING_GRIDSP;
            int lvl = 0;
            // Light squares: 2-on/1-off diagonal stipple (2 white : 1 black) → brighter
            // grey, still only levels 0/4 so it never dithers/flashes.
            if ((wx + wy) & 1) lvl = (((gx + gy) % 3) == 0) ? 0 : BOING_WHITE;
            GREY_SET(grey_buffer, gy, gx, lvl);
        }
    }

    // 2) Drop shadow: a BLACK ellipse offset down+right — reads as the ball's shadow on the
    //    wall (blacks out the grey squares it covers). Squashed ~0.8× vertically.
    int shx = cx + BOING_SHOFF, shy = cy + BOING_SHOFF;
    for (int dy = -BOING_R; dy <= BOING_R; dy++) {
        int gy = shy + dy; if (gy < 0 || gy >= GREY_H) continue;
        for (int dx = -BOING_R; dx <= BOING_R; dx++) {
            if (dx*dx*64 + dy*dy*100 > BOING_R*BOING_R*64) continue;   // ellipse
            int gx = shx + dx; if (gx < 0 || gx >= GREY_W) continue;
            GREY_SET(grey_buffer, gy, gx, 0);
        }
    }

    // 3) The ball on top (fully opaque: white checkers = white, black checkers = black).
    for (int dy = 0; dy < BOING_D; dy++) {
        int gy = cy - BOING_R + dy;
        if (gy < 0 || gy >= GREY_H) continue;
        const Cell *row = &tab[dy * BOING_D];
        for (int dx = 0; dx < BOING_D; dx++) {
            const Cell &c = row[dx];
            if (!c.on) continue;
            int gx = cx - BOING_R + dx;
            if (gx < 0 || gx >= GREY_W) continue;
            uint8_t ang = (uint8_t)(c.lon + spin_phase);          // wrap to 0..255
            int lon_band = (ang * BOING_NLON) / 256;              // 0..NLON-1
            int checker = (c.lat + lon_band) & 1;
            int level = checker ? BOING_WHITE : BOING_RED;
            // subtle spherical shading: darken the rim a touch (shade 0..3)
            if (c.shade == 0 && level > 0) level--;
            GREY_SET(grey_buffer, gy, gx, level);
        }
    }
}

// ── STARFIELD: classic fly-through-space screensaver ────────────────────────────────
// Stars stream outward from screen centre toward the viewer. Each star has a fixed (sx,sy)
// direction and a depth z; every frame z decreases (star approaches) so its screen radius
// grows and it brightens; when it passes the viewer or leaves the screen it respawns far
// away with a new random direction. Speed (how fast z decreases) = Main knob + CV In 1.
#define STAR_N   96          // number of stars
#define STAR_ZMAX 4096       // far plane (z at spawn)
static void __not_in_flash_func(screensaver_starfield)() {
    struct Star { int32_t sx, sy, z; };   // sx,sy = direction (±), z = depth (1..ZMAX)
    static Star stars[STAR_N];
    static bool init = false;
    auto rnd = [](){ return (int)(lcg_rand() & 0x7fffffff); };
    auto respawn = [&](Star &s){
        s.sx = (rnd() % (GREY_W)) - GREY_W/2;   // direction spread
        s.sy = (rnd() % (GREY_H)) - GREY_H/2;
        if (s.sx == 0 && s.sy == 0) s.sx = 1;
        s.z  = STAR_ZMAX - (rnd() % 256);       // spawn near the far plane
    };
    if (!init) { init = true; for (auto &s : stars) { respawn(s); s.z = 1 + rnd() % STAR_ZMAX; } }

    // Speed = Main knob (0..4095), clamped to a lively range.
    int32_t speed = shared.knob_main / 20;   // ~0..204
    if (speed < 4)   speed = 4;                               // always drifting a little
    if (speed > 300) speed = 300;
    // Turn: Knob X + CV In 1 = horizontal, Knob Y + CV In 2 = vertical. Bipolar. Nearer
    // stars are pushed further opposite the turn → parallax = flying/banking through space.
    int32_t turnx = (shared.knob_x - 2048) + shared.cv_x;    // ±~4095
    int32_t turny = (shared.knob_y - 2048) + shared.cv_y;    // ±~4095

    dilate_cap = 2;                        // let near stars bloom to solid white
    shared.alt_cv1 = 0;                    // starfield doesn't drive CVOut1
    memset(grey_buffer, 0, GREY_SIZE);
    const int ccx = GREY_W / 2, ccy = GREY_H / 2;
    for (auto &s : stars) {
        s.z -= speed;
        if (s.z <= 1) { respawn(s); continue; }
        // Nearness 0..255 (0 = far plane, 255 = right at the viewer).
        int near = ((STAR_ZMAX - s.z) * 255) / STAR_ZMAX;
        if (near < 0) near = 0;
        if (near > 255) near = 255;
        // Project: screen = centre + dir·(scale/z) − turn·nearness (parallax turn).
        int px = ccx + (s.sx * 256) / s.z - (turnx * near) / 8192;
        int py = ccy + (s.sy * 256) / s.z - (turny * near) / 8192;
        if (px < 0 || px >= GREY_W || py < 0 || py >= GREY_H) { respawn(s); continue; }

        if (near < 96) {
            // FAR: single dim/mid pixel (level 1..3 with nearness).
            GREY_SET(grey_buffer, py, px, 1 + near / 40);
        } else if (near < 176) {
            // MID: a small ~2-line blob, 3/1 grey stipple (levels 3 & 1) → soft grey clump.
            for (int dy = 0; dy <= 1; dy++)
                for (int dx = 0; dx <= 1; dx++) {
                    int gx = px + dx, gy = py + dy;
                    if (gx < 0 || gx >= GREY_W || gy < 0 || gy >= GREY_H) continue;
                    GREY_SET(grey_buffer, gy, gx, (((gx + gy) & 1) ? 3 : 1));  // 3/1 stipple
                }
        } else {
            // NEAR: a ~5px round-ish SOLID WHITE blob (dilation pushes it past full white).
            int rad = 1 + (near - 176) / 32;             // radius 1..3 cells → ~3..7px wide
            for (int dy = -rad; dy <= rad; dy++)
                for (int dx = -rad; dx <= rad; dx++) {
                    if (dx*dx + dy*dy > rad*rad) continue;   // round
                    int gx = px + dx, gy = py + dy;
                    if (gx < 0 || gx >= GREY_W || gy < 0 || gy >= GREY_H) continue;
                    GREY_SET(grey_buffer, gy, gx, GREY_LEVELS - 1);
                }
        }
    }
}

// ── RADAR: radar-scope shooter ──────────────────────────────────────────────────────
// A sweeping hand rotates leaving a decaying phosphor wedge over a dim ring+spoke scope.
// Up to 4 slow blips drift. A turret at centre aims a bearing (Main knob, 7→5 o'clock).
// Hold PU1/DOWN to charge launch power; release to lob a missile out along the bearing —
// it travels ballistically in RANGE (longer hold = detonates further out, capped at the
// rim), path traced, then explodes; any target within the blast radius is destroyed and
// respawns later. CV Out 2 triggers on each detonation; CV Out 1 ramps with the sweep.
#define RAD_RMAX   60        // scope radius in cells
#define RAD_NTGT   4         // max targets
#define RAD_BLAST  8         // detonation kill radius (cells)
static void __not_in_flash_func(screensaver_radar)() {
    struct Tgt { uint8_t active; int32_t x, y, vx, vy; int respawn; int ping; }; // ping = brightness timer
    static bool init = false;
    static uint8_t sweep = 0;
    static Tgt tgt[RAD_NTGT];
    static bool prev_hold = false;
    static int  power = 0;                 // charge accumulator while held
    static struct { uint8_t active; uint8_t ang; int32_t r, vr; int expl; int hit; } msl = {0,0,0,0,0,0};
    // Player-placed static "enemies" (PU2 IN): sit on the sweep line where placed, don't
    // move, fade over time. Distinct diamond shape vs the round drifting blips.
    #define RAD_NENEMY 12
    static struct { uint8_t active; int ex, ey; int life; } enemy[RAD_NENEMY] = {};

    const int cx = GREY_W/2, cy = GREY_H/2;
    auto rnd = [](){ return (int)(lcg_rand() & 0x7fffffff); };
    auto spawn_tgt = [&](Tgt &t){
        int a = rnd() & 255, rr = (RAD_RMAX/3) + rnd() % (RAD_RMAX/2);
        t.x = (cx + (cos_a(a)*rr>>8)) << 8;
        t.y = (cy + (sin_a(a)*rr>>8)) << 8;
        t.vx = ((rnd()%64) - 32) * 2;      // slow drift
        t.vy = ((rnd()%64) - 32) * 2;
        if (t.vx==0 && t.vy==0) t.vx = 32;
        t.active = 1; t.respawn = 0; t.ping = 0;
    };
    if (!init) { init = true; for (auto &t : tgt) spawn_tgt(t); }

    // Fade the whole scope each frame → phosphor decay of wedge/blips/trace.
    fade_step();
    dilate_cap = 1;

    // Dim static scope: concentric rings + spokes (level 1). Midpoint circle per ring.
    for (int ring = 1; ring <= 3; ring++) {
        int R = RAD_RMAX * ring / 3;
        int px = R, py = 0, err = 1 - R;
        while (px >= py) {
            const int o[8][2] = {{px,py},{py,px},{-py,px},{-px,py},{-px,-py},{-py,-px},{py,-px},{px,-py}};
            for (auto &d : o) { int gx=cx+d[0], gy=cy+d[1];
                if (gx>=0&&gx<GREY_W&&gy>=0&&gy<GREY_H) GREY_SET(grey_buffer,gy,gx,1); }
            py++; if (err < 0) err += 2*py + 1; else { px--; err += 2*(py-px) + 1; }
        }
    }
    for (int s = 0; s < 8; s++) {              // 8 spokes
        uint8_t a = (uint8_t)(s * 32);
        draw_line(cx, cy, cx + (cos_a(a)*RAD_RMAX>>8), cy + (sin_a(a)*RAD_RMAX>>8), 1);
    }

    // Sweep hand (bright), leaves the decaying wedge behind it via fade_step.
    uint8_t prev_sweep = sweep;
    sweep = (uint8_t)(sweep + 2);
    draw_line(cx, cy, cx + (cos_a(sweep)*RAD_RMAX>>8), cy + (sin_a(sweep)*RAD_RMAX>>8), GREY_LEVELS-1);
    shared.alt_cv1 = (sweep * 16) - 2048;     // CV Out 1 = sweep ramp

    // PU2 IN → place a static enemy on the current sweep line. CV In 1 sets WHERE along the
    // sweep it lands: bipolar from centre (−2048 = at centre, 0V = mid, +2047 = at the rim).
    if (shared.pu2_rising) {
        shared.pu2_rising = false;
        for (int i = 0; i < RAD_NENEMY; i++) if (!enemy[i].active) {
            int rr = ((shared.cv_x + 2048) * RAD_RMAX) / 4096;   // 0..RMAX from CV In 1
            if (rr < 0) rr = 0;
            if (rr > RAD_RMAX) rr = RAD_RMAX;
            enemy[i].ex = cx + (cos_a(sweep)*rr>>8);
            enemy[i].ey = cy + (sin_a(sweep)*rr>>8);
            enemy[i].life = 255; enemy[i].active = 1;
            break;
        }
    }
    // Draw + fade the placed enemies: a solid SQUARE block — clearly distinct from the round
    // blips. Brightness by remaining life.
    for (int i = 0; i < RAD_NENEMY; i++) {
        if (!enemy[i].active) continue;
        enemy[i].life -= 3;                                       // fade out over ~85 frames
        if (enemy[i].life <= 0) { enemy[i].active = 0; continue; }
        int lvl = 1 + enemy[i].life / 64;                        // 1..4
        int ex = enemy[i].ex, ey = enemy[i].ey;
        for (int dy = -2; dy <= 1; dy++)                          // 4 tall
            for (int dx = -2; dx <= 2; dx++) {                    // 5 wide
                int gx = ex+dx, gy = ey+dy;
                if (gx>=0&&gx<GREY_W && gy>=0&&gy<GREY_H) GREY_SET(grey_buffer,gy,gx,lvl);
            }
    }

    // Targets: drift, reflect at the rim. Each is INVISIBLE until the sweep passes its
    // bearing — then it "pings" bright+large and fades over the 5 levels, gone before the
    // sweep comes round again (ping 255→0 at −2/frame ≈ 128 frames = one revolution).
    for (auto &t : tgt) {
        if (!t.active) { if (--t.respawn <= 0) spawn_tgt(t); continue; }
        t.x += t.vx; t.y += t.vy;
        int tx = t.x>>8, ty = t.y>>8, dx = tx-cx, dy = ty-cy;
        if (dx*dx + dy*dy > RAD_RMAX*RAD_RMAX) { t.vx = -t.vx; t.vy = -t.vy;   // bounce in
            t.x += 2*t.vx; t.y += 2*t.vy; tx = t.x>>8; ty = t.y>>8; }
        // Did the sweep cross this target's bearing this frame? → ping.
        uint8_t tb = atan2_u8(ty - cy, tx - cx);   // (y, x) order — matches cos/sin sweep
        if ((uint8_t)(tb - prev_sweep) < (uint8_t)(sweep - prev_sweep) || tb == sweep) t.ping = 255;
        if (t.ping > 0) {
            t.ping -= 2; if (t.ping < 0) t.ping = 0;
            int lvl = 1 + t.ping / 64;                 // 1..4 by freshness
            int rad = t.ping / 90;                     // 0..2 cells: bigger when fresh
            for (int ddy = -rad; ddy <= rad; ddy++)
                for (int ddx = -rad; ddx <= rad; ddx++) {
                    if (ddx*ddx + ddy*ddy > rad*rad) continue;
                    int gx = tx+ddx, gy = ty+ddy;
                    if (gx>=0&&gx<GREY_W&&gy>=0&&gy<GREY_H) GREY_SET(grey_buffer,gy,gx,lvl);
                }
        }
    }

    // Turret aim: Main knob → bearing over the 7→5 o'clock arc (skip the bottom 60°).
    // Base rotated -64 (90° the correct way) so the knob's 6 o'clock aligns with 6 o'clock.
    uint8_t aim = (uint8_t)(149 - 64 + (shared.knob_main * 213) / 4095);
    { int tx = cx + (cos_a(aim)*10>>8), ty = cy + (sin_a(aim)*10>>8);
      draw_line(cx, cy, tx, ty, GREY_LEVELS-1); }   // short turret stub

    // Fire: hold PU1 or switch-DOWN to charge power; release to launch.
    bool hold = shared.pu1_held || (shared.sw_position == 2);
    if (hold) { power += 12; if (power > 900) power = 900; }
    if (!hold && prev_hold && !msl.active) {         // release → launch
        msl.active = 1; msl.ang = aim; msl.r = 0;
        msl.vr = 200 + power;                         // launch velocity (range/frame, 8.8)
        msl.expl = 0;
    }
    prev_hold = hold;
    if (hold) {                                       // charge gauge: a growing radial tick
        int g = (power * RAD_RMAX) / 900;
        draw_line(cx, cy, cx + (cos_a(aim)*g>>8), cy + (sin_a(aim)*g>>8), GREY_LEVELS-2);
    } else if (!msl.active) power = 0;

    // Missile: ballistic in RANGE — decelerates ("range gravity"), detonates at apex or rim.
    if (msl.active) {
        if (msl.expl == 0) {
            msl.vr -= 26;                             // range "gravity"
            msl.r  += msl.vr;
            int rr = msl.r >> 8;
            int mx = cx + (cos_a(msl.ang)*rr>>8), my = cy + (sin_a(msl.ang)*rr>>8);
            if (msl.vr <= 0 || rr >= RAD_RMAX) {      // apex or edge → detonate
                msl.expl = 1; msl.hit = 0;
                shared.ast_gate_seq = shared.ast_gate_seq + 1;   // CV Out 2 = shot trigger
                for (auto &t : tgt) if (t.active) {   // blast check (targets always solid,
                    int ddx = (t.x>>8)-mx, ddy = (t.y>>8)-my;    //   even if faded to zero)
                    if (ddx*ddx + ddy*ddy <= RAD_BLAST*RAD_BLAST) {
                        t.active = 0; t.respawn = 90 + (rnd()%120); msl.hit = 1;
                    }
                }
            } else if (mx>=0&&mx<GREY_W&&my>=0&&my<GREY_H) {
                GREY_SET(grey_buffer,my,mx,GREY_LEVELS-1);   // trace
            }
        } else {
            int rr = msl.r >> 8;
            int mx = cx + (cos_a(msl.ang)*rr>>8), my = cy + (sin_a(msl.ang)*rr>>8);
            if (msl.hit) {
                // HIT: a big solid bright blast + "HIT" label — unmistakable.
                int er = msl.expl; if (er > 10) er = 10;
                for (int dy = -er; dy <= er; dy++)
                    for (int dx = -er; dx <= er; dx++) {
                        if (dx*dx + dy*dy > er*er) continue;
                        int ex = mx+dx, ey = my+dy;
                        if (ex>=0&&ex<GREY_W&&ey>=0&&ey<GREY_H) GREY_SET(grey_buffer,ey,ex,GREY_LEVELS-1);
                    }
                text_mode = true;
                draw_text(cx - 13, 4, "HIT", GREY_LEVELS-1);
            } else {
                // MISS: subtle small expanding ring (the original quiet detonation).
                int er = msl.expl;
                for (int a = 0; a < 256; a += 24) {
                    int ex = mx + (cos_a((uint8_t)a)*er>>8), ey = my + (sin_a((uint8_t)a)*er>>8);
                    if (ex>=0&&ex<GREY_W&&ey>=0&&ey<GREY_H) GREY_SET(grey_buffer,ey,ex,GREY_LEVELS-1);
                }
            }
            if (++msl.expl > (msl.hit ? 16 : RAD_BLAST)) { msl.active = 0; power = 0; }
        }
    }
}

// ── LUNAR: side-view Lunar Lander ───────────────────────────────────────────────────
// Ship rotates (Main knob + CV In 1), thruster (PU1 or held switch-DOWN) pushes along the
// nose against constant gravity. Random jagged terrain with one flat landing pad. Land on
// the pad slowly & upright → next stage (+1 drifting UFO, new terrain, refuel). Crash on
// terrain / a UFO / too fast / too tilted → explosion, restart at stage 1. Fuel is limited;
// empty = no thrust. CV Out 1 = altitude; CV Out 2 = pulse on each land/crash event.
#define LUN_MAXUFO 6
#define LUN_GRAV   6         // downward accel per frame (8.8)
#define LUN_THR    14        // thruster accel per frame (8.8)
#define LUN_VLAND  220       // max landing speed (8.8) for a safe touchdown
static void __not_in_flash_func(screensaver_lunar)() {
    static bool init = false;
    static int  stage = 1, fuel = 0, phase = 0, timer = 0;   // phase 0=FLY 1=LANDED 2=CRASH
    static int32_t sx, sy, svx, svy;                          // ship pos/vel 8.8
    static uint8_t sang;                                      // heading (0 = upright/up)
    static uint8_t ground[GREY_W];                            // terrain height (row) per col
    static int  padx0, padx1;                                 // landing pad span (cells)
    static struct { uint8_t active; int32_t x, y, vx; } ufo[LUN_MAXUFO];
    static int  expl = 0;

    auto rnd = [](){ return (int)(lcg_rand() & 0x7fffffff); };
    auto regen = [&](){
        // Gently rolling terrain: a random walk with SLOPE momentum (real hills, not noise),
        // over a modest vertical band. Slope changes slowly and is capped shallow so it's
        // not too spiky. Also only nudge the slope every few columns → longer, smoother hills.
        const int HI = GREY_H - 40, LO = GREY_H - 10;  // highest peak .. lowest valley (raised)
        int h = (GREY_H - 24) - (rnd() % 12);
        int slope = 0;
        for (int i = 0; i < GREY_W; i++) {
            if ((i & 3) == 0) slope += (rnd() % 3) - 1; // curve slope every 4 cols (smoother)
            if (slope > 2)  slope = 2;                  // shallow max steepness
            if (slope < -2) slope = -2;
            h += slope;
            if (h < HI) { h = HI; slope = 1; }          // bounce off the bands
            if (h > LO) { h = LO; slope = -1; }
            ground[i] = (uint8_t)h;
        }
        // Carve a flat pad. Width NARROWS with the stage (harder each time), down to a
        // tight minimum. Sit it low-ish in a clear notch so it reads as a platform.
        int pw = 24 - (stage - 1) * 3;                  // stage 1 = 24 wide, shrinking
        if (pw < 8) pw = 8;                             // minimum landable width
        padx0 = 16 + rnd() % (GREY_W - 32 - pw);
        padx1 = padx0 + pw;
        int py = ground[(padx0 + padx1) / 2];
        if (py < GREY_H - 14) py = GREY_H - 14;         // keep the pad low-ish (reachable)
        for (int i = padx0; i <= padx1 && i < GREY_W; i++) ground[i] = (uint8_t)py;
        // UFOs = stage count (capped).
        int n = stage; if (n > LUN_MAXUFO) n = LUN_MAXUFO;
        for (int i = 0; i < LUN_MAXUFO; i++) {
            if (i < n) { ufo[i].active = 1; ufo[i].x = (rnd()%GREY_W)<<8;
                         // spawn across the WHOLE playfield height (they pass through hills)
                         ufo[i].y = (32 + rnd()%(GREY_H-42))<<8;  // top raised down 12px
                         // direction from a HIGH bit (LCG low bits are correlated → all same
                         // way); speed independent → some go L→R, others R→L, varied speeds.
                         int spd = 48 + rnd()%208;
                         ufo[i].vx = ((rnd() & 0x400) ? spd : -spd); }
            else ufo[i].active = 0;
        }
    };
    auto reset_ship = [&](){ sx = (GREY_W/2)<<8; sy = 8<<8; svx = 0; svy = 0; sang = 0; };

    if (!init) { init = true; stage = 1; fuel = 600; regen(); reset_ship(); phase = 0; }

    dilate_cap = 2;                              // thicker/brighter lines than the default
    memset(grey_buffer, 0, GREY_SIZE);

    // Draw terrain as a SOLID filled ground (from the surface down a few rows) so it reads
    // boldly; the surface row is brightest. Pad marked full-white with little end posts.
    // Body fill (solid ground below the surface) — thin (~1/3 of the old depth).
    for (int i = 0; i < GREY_W; i++) {
        int g = ground[i];
        for (int d = 0; d <= 2 && g+d < GREY_H; d++) GREY_SET(grey_buffer, g+d, i, 3);
    }
    // Bright surface as a CONNECTED polyline between adjacent tops (closes diagonal gaps on
    // slopes → the crest reads as a continuous thick line left-to-right), drawn 2 rows deep.
    for (int i = 0; i < GREY_W - 1; i++) {
        draw_line(i, ground[i],   i+1, ground[i+1],   GREY_LEVELS-1);
        draw_line(i, ground[i]-1, i+1, ground[i+1]-1, GREY_LEVELS-1);
    }
    // Landing pad: an unmistakable bright platform. Solid deck (2 rows), tall end posts
    // with little flag tops, so it clearly stands out from the surrounding hills.
    int pady = ground[padx0];
    for (int i = padx0; i <= padx1 && i < GREY_W; i++) {
        GREY_SET(grey_buffer, pady, i, GREY_LEVELS-1);                 // deck top
        if (pady+1 < GREY_H) GREY_SET(grey_buffer, pady+1, i, GREY_LEVELS-1);  // deck underside
    }
    for (int e = 0; e < 2; e++) {
        int px = e ? padx1 : padx0;
        if (px < 0 || px >= GREY_W) continue;
        for (int d = 1; d <= 8; d++)                                   // tall post
            if (pady-d >= 0) GREY_SET(grey_buffer, pady-d, px, GREY_LEVELS-1);
        // little flag arm at the top of each post, pointing inward
        int fdir = e ? -1 : 1, ftop = pady - 8;
        for (int d = 1; d <= 3; d++) {
            int fx = px + fdir*d;
            if (ftop >= 0 && fx>=0 && fx<GREY_W) GREY_SET(grey_buffer, ftop, fx, GREY_LEVELS-1);
        }
    }

    // Draw UFOs (drift + wrap) and check collisions. Larger saucers: a wide hull + dome.
    for (int i = 0; i < LUN_MAXUFO; i++) {
        if (!ufo[i].active) continue;
        if (phase == 0) { ufo[i].x += ufo[i].vx; ufo[i].x = (ufo[i].x + (GREY_W<<8)) % (GREY_W<<8); }
        int ux = ufo[i].x>>8, uy = ufo[i].y>>8;
        for (int dx = -4; dx <= 4; dx++) {          // hull: wide bar, 2 rows
            int gx = ux+dx;
            if (gx>=0&&gx<GREY_W) {
                if (uy>=0&&uy<GREY_H)     GREY_SET(grey_buffer,uy,gx,GREY_LEVELS-1);
                if (uy+1>=0&&uy+1<GREY_H) GREY_SET(grey_buffer,uy+1,gx,GREY_LEVELS-1);
            }
        }
        for (int dx = -2; dx <= 2; dx++) {          // dome on top
            int gx = ux+dx, gy = uy-1;
            if (gx>=0&&gx<GREY_W&&gy>=0&&gy<GREY_H) GREY_SET(grey_buffer,gy,gx,GREY_LEVELS-1);
        }
    }

    // ── FLYING physics ──
    if (phase == 0) {
        // Rotation: Main knob + CV In 1 → heading. Centre = upright (0). ±~64 = ±90°.
        int32_t rot = (shared.knob_main - 2048) + shared.cv_x;   // ±~4095
        sang = (uint8_t)((rot * 64) / 4095);                     // -64..+64 (wraps as uint8)

        // Thrust along the nose (nose points "up" rotated by sang). Up vector = (sin, -cos).
        bool thrust = (shared.pu1_held || shared.sw_position == 2) && fuel > 0;
        if (thrust) {
            svx += (sin_a(sang) * LUN_THR) >> 8;
            svy += (-cos_a(sang) * LUN_THR) >> 8;
            fuel--;
        }
        svy += LUN_GRAV;                            // gravity
        sx += svx; sy += svy;
        if (sx < 0)          sx += GREY_W<<8;       // wrap X
        if (sx >= GREY_W<<8) sx -= GREY_W<<8;
        if (sy < 0) { sy = 0; if (svy < 0) svy = 0; }

        int shipx = sx>>8, shipy = sy>>8;

        // UFO collision (larger saucer → wider hitbox: |dx|≤5 and |dy|≤3).
        for (int i = 0; i < LUN_MAXUFO; i++) if (ufo[i].active) {
            int ddx = (ufo[i].x>>8)-shipx, ddy = (ufo[i].y>>8)-shipy;
            if (ddx > -6 && ddx < 6 && ddy > -4 && ddy < 4) {
                phase = 2; expl = 1; timer = 0;
                shared.ast_gate_seq = shared.ast_gate_seq + 1;
            }
        }

        // Ground contact.
        if (shipx >= 0 && shipx < GREY_W && shipy >= ground[shipx] - 3) {
            bool on_pad = (shipx >= padx0 && shipx <= padx1);
            bool slow   = (svy < LUN_VLAND && (svx < 200 && svx > -200));
            bool upright= (sang < 16 || sang > 240);       // near 0
            if (on_pad && slow && upright) { phase = 1; timer = 60;
                                             shared.ast_gate_seq = shared.ast_gate_seq + 1; }
            else { phase = 2; expl = 1; timer = 0;
                   shared.ast_gate_seq = shared.ast_gate_seq + 1; }
        }

        // CV Out 1 = altitude above ground under the ship.
        int gh = (shipx>=0&&shipx<GREY_W) ? ground[shipx] : GREY_H;
        int alt = gh - shipy; if (alt < 0) alt = 0;
        shared.alt_cv1 = (alt * 4095) / GREY_H - 2048;

        // Draw ship: a small lander triangle around (shipx,shipy), nose along -sang.
        int nx = shipx + (sin_a(sang)*5>>8),  ny = shipy + (-cos_a(sang)*5>>8);   // nose
        uint8_t bl = sang + 96, br = sang + 160;                                   // base corners
        int lx = shipx + (sin_a(bl)*4>>8), ly = shipy + (-cos_a(bl)*4>>8);
        int rx = shipx + (sin_a(br)*4>>8), ry = shipy + (-cos_a(br)*4>>8);
        draw_line(nx,ny,lx,ly,GREY_LEVELS-1); draw_line(nx,ny,rx,ry,GREY_LEVELS-1);
        draw_line(lx,ly,rx,ry,GREY_LEVELS-1);
        // Thrust flame: a plume shooting out the TAIL, directly opposite the nose (i.e. the
        // exhaust direction). Drawn as a short fan of lines from the tail centre, length
        // flickering, so it reads clearly as a rocket jet rather than a stray line.
        if ((shared.pu1_held || shared.sw_position == 2) && fuel > 0) {
            int tailx = (lx + rx) / 2, taily = (ly + ry) / 2;      // tail centre
            int flen = 6 + (int)(lcg_rand() & 3);                  // flicker 6..9
            // exhaust unit direction = opposite the nose = (-sin(sang), +cos(sang))
            int ex = tailx - (sin_a(sang) * flen >> 8);
            int ey = taily + (cos_a(sang) * flen >> 8);
            draw_line(tailx, taily, ex, ey, GREY_LEVELS-1);        // central jet
            // two side flares fanning ±~25° for a plume shape
            uint8_t fa = sang + 128;                               // straight back bearing
            int e2x = tailx + (sin_a((uint8_t)(fa-18)) * flen >> 8);
            int e2y = taily + (-cos_a((uint8_t)(fa-18)) * flen >> 8);
            int e3x = tailx + (sin_a((uint8_t)(fa+18)) * flen >> 8);
            int e3y = taily + (-cos_a((uint8_t)(fa+18)) * flen >> 8);
            draw_line(tailx, taily, e2x, e2y, GREY_LEVELS-2);
            draw_line(tailx, taily, e3x, e3y, GREY_LEVELS-2);
        }
    } else if (phase == 1) {
        // LANDED: hold message, then advance stage. Double-struck for a bold/thick weight.
        draw_text(70, 40, "LANDED", GREY_LEVELS-1);
        draw_text(71, 40, "LANDED", GREY_LEVELS-1);
        // still draw the resting ship
        int shipx = sx>>8, shipy = sy>>8;
        if (shipx>=0&&shipx<GREY_W&&shipy>=0&&shipy<GREY_H) GREY_SET(grey_buffer,shipy,shipx,GREY_LEVELS-1);
        if (--timer <= 0) { stage++; fuel = 600; regen(); reset_ship(); phase = 0; }
    } else {
        // CRASH: expanding explosion, then restart at stage 1.
        int mx = sx>>8, my = sy>>8;
        for (int a = 0; a < 256; a += 20) {
            int ex = mx + (cos_a((uint8_t)a)*expl>>8), ey = my + (sin_a((uint8_t)a)*expl>>8);
            if (ex>=0&&ex<GREY_W&&ey>=0&&ey<GREY_H) GREY_SET(grey_buffer,ey,ex,GREY_LEVELS-1);
        }
        if (++expl > 16) { stage = 1; fuel = 600; regen(); reset_ship(); phase = 0; }
    }

    // HUD: STAGE n (top-left) + a big obvious FUEL bar (top-right), crisp text.
    text_mode = true;
    // "ST n" drawn twice with a 1px offset = a bold/thicker weight. Shifted right 1 char,
    // and down one line (y 2 -> 14).
    draw_text(11, 14, "ST", GREY_LEVELS-1);   draw_text(12, 14, "ST", GREY_LEVELS-1);
    draw_number(31, 14, stage, GREY_LEVELS-1); draw_number(32, 14, stage, GREY_LEVELS-1);

    // Fuel: a thick outlined bar on the right that empties left-to-right (obvious at a
    // glance; no text label needed — it's clearly a gauge).
    const int fx = 112, fy = 15, fw = 62, fh = 8;  // bar box (ends col 174); down one line
    // outline
    for (int i = 0; i <= fw; i++) {
        GREY_SET(grey_buffer, fy,      fx + i, GREY_LEVELS-1);
        GREY_SET(grey_buffer, fy + fh, fx + i, GREY_LEVELS-1);
    }
    for (int j = 0; j <= fh; j++) {
        GREY_SET(grey_buffer, fy + j, fx,      GREY_LEVELS-1);
        GREY_SET(grey_buffer, fy + j, fx + fw, GREY_LEVELS-1);
    }
    int fill = (fuel * (fw - 2)) / 600;            // inner fill width
    for (int i = 0; i < fill; i++)
        for (int j = 2; j < fh - 1; j++)
            GREY_SET(grey_buffer, fy + j, fx + 1 + i, GREY_LEVELS-1);
}

// ── 3DMAZE: chunky first-person raycast maze (ZX81 3D Monster Maze style) ────────────
// Randomly generated maze. Main knob = smooth angle turn; PU1/held switch-DOWN = walk
// forward. Walls raycast (DDA), shaded by distance (near bright → far dim) for depth;
// ceiling/floor left black for the chunky look. A monster roams and looms larger as it
// nears in line of sight; catching the player = flash + new maze. Positions 8.8 fixed.
#define MAZ_W    15          // maze grid (odd = clean carve)
#define MAZ_H    15
#define MAZ_FOV  48          // half-FOV in angle units (48/256*360 ≈ 67° total)
#define MAZ_STEP 2           // cast every Nth column (perf)
#define MAZ_MAXD 20          // max DDA cell steps
static void __not_in_flash_func(screensaver_maze)() {
    static bool init = false;
    static uint8_t wall[MAZ_H][MAZ_W];
    static int32_t px, py;                 // player pos, 8.8 (cell units)
    static uint8_t pang;                   // heading 0..255
    static int exitx, exity, exit_face;   // EXIT: wall cell + which side faces open (0E1S2W3N)
    static int caught = 0;                 // >0: reached-exit flash countdown

    auto rnd = [](){ return (int)(lcg_rand() & 0x7fffffff); };
    auto solid = [&](int cx, int cy)->bool {
        if (cx < 0 || cx >= MAZ_W || cy < 0 || cy >= MAZ_H) return true;
        return wall[cy][cx] != 0;
    };
    auto gen = [&](){
        for (int y=0;y<MAZ_H;y++) for (int x=0;x<MAZ_W;x++) wall[y][x]=1;
        // Randomized DFS carve on odd cells.
        int stx[MAZ_W*MAZ_H], sty[MAZ_W*MAZ_H], sp=0;
        int cx=1, cy=1; wall[cy][cx]=0; stx[sp]=cx; sty[sp]=cy; sp++;
        while (sp>0) {
            cx=stx[sp-1]; cy=sty[sp-1];
            int dirs[4]={0,1,2,3};
            for (int i=3;i>0;i--){ int j=rnd()%(i+1); int t=dirs[i];dirs[i]=dirs[j];dirs[j]=t; }
            bool moved=false;
            for (int d=0; d<4; d++) {
                int nx=cx, ny=cy;
                if (dirs[d]==0) ny-=2; else if (dirs[d]==1) ny+=2;
                else if (dirs[d]==2) nx-=2; else nx+=2;
                if (nx>0&&nx<MAZ_W-1&&ny>0&&ny<MAZ_H-1&&wall[ny][nx]) {
                    wall[(cy+ny)/2][(cx+nx)/2]=0; wall[ny][nx]=0;
                    stx[sp]=nx; sty[sp]=ny; sp++; moved=true; break;
                }
            }
            if (!moved) sp--;
        }
        px = (1<<8) + 128; py = (1<<8) + 128;                // start in cell (1,1)
        // Face the open corridor out of the start cell (so forward works immediately).
        // Forward-cardinal = (pang+64), so pang = cardinal_angle - 64 to look that way.
        if      (!solid(2,1)) pang = 192;    // east  (0-64)
        else if (!solid(1,2)) pang = 0;      // south (64-64)
        else if (!solid(0,1)) pang = 64;     // west  (128-64)
        else                  pang = 128;    // north (192-64)
        // EXIT: pick a WALL cell (preferably far from the start) that borders an open cell,
        // and record which side faces open — that face is drawn as a glowing white panel.
        exitx = exity = exit_face = -1;
        for (int tries = 0; tries < 200 && exit_face < 0; tries++) {
            int wx = 1 + rnd() % (MAZ_W - 2), wy = 1 + rnd() % (MAZ_H - 2);
            if (!wall[wy][wx]) continue;                 // must be a wall cell
            if (wx + wy < (MAZ_W + MAZ_H) / 2) continue; // bias to the far half
            if      (!solid(wx,   wy-1)) { exit_face = 3; }   // north face open
            else if (!solid(wx,   wy+1)) { exit_face = 1; }   // south
            else if (!solid(wx-1, wy))   { exit_face = 2; }   // west
            else if (!solid(wx+1, wy))   { exit_face = 0; }   // east
            if (exit_face >= 0) { exitx = wx; exity = wy; }
        }
        // Fallback: scan for any wall bordering open (guarantees an exit exists).
        for (int wy = MAZ_H-2; wy >= 1 && exit_face < 0; wy--)
            for (int wx = MAZ_W-2; wx >= 1 && exit_face < 0; wx--) {
                if (!wall[wy][wx]) continue;
                if      (!solid(wx,wy-1)) exit_face=3; else if (!solid(wx,wy+1)) exit_face=1;
                else if (!solid(wx-1,wy)) exit_face=2; else if (!solid(wx+1,wy)) exit_face=0;
                if (exit_face>=0) { exitx=wx; exity=wy; }
            }
    };
    if (!init) { init = true; gen(); }

    dilate_cap = 2;                                // thicker walls (dilate one more pixel)
    effect_invert = shared.pu2_held;               // PU2 held → invert (white-on-black accent)
    memset(grey_buffer, 0, GREY_SIZE);

    // ── Reached-exit flash, then a fresh maze. ──
    if (caught > 0) {
        caught--;
        if (caught & 4) memset(grey_buffer, GREY_LEVELS-1, GREY_SIZE);   // strobe
        if (caught == 0) { shared.ast_gate_seq = shared.ast_gate_seq + 1; gen(); }
        return;
    }
    // Reached the exit? (player cell adjacent to the exit cell on its open face.)
    {
        int pcx0 = px>>8, pcy0 = py>>8;
        int ex = exitx + (exit_face==0) - (exit_face==2);   // the open cell in front of the panel
        int ey = exity + (exit_face==1) - (exit_face==3);
        if (pcx0 == ex && pcy0 == ey) caught = 24;          // win → flash → new maze
    }

    // ── Movement. Two modes:
    //   AUTOPLAY: Knob X / CV In 1 non-zero → self-drives forward at that speed, choosing a
    //     random open direction at each junction (a hands-free screensaver).
    //   MANUAL:   X/CV1 ~zero → Main knob turns the view; PU1/held-DOWN walks forward.
    // Either way the player is RAIL-LOCKED to corridor centres (no wall-straddle glitch).
    static int auto_card = -1;                      // current autoplay travel cardinal
    const int32_t CEN = 128;
    int cellx = px >> 8, celly = py >> 8;
    int32_t cxc = (cellx << 8) + CEN, cyc = (celly << 8) + CEN;

    // AUTO engages when Knob X is turned up from its fully-CCW (off) end. X sets the base
    // walk speed; CV In 1 is BIPOLAR around that — it slows or speeds the walk, but can't
    // stop it while X is engaged (speed clamps to a lively minimum). X fully CCW = MANUAL.
    bool auto_on = (shared.knob_x > 200);          // small dead-zone at the CCW end
    int autosp = shared.knob_x / 96 + shared.cv_x / 96;   // X base ± CV1 (bipolar)
    if (auto_on && autosp < 6) autosp = 6;         // never stall while engaged
    // openings out of the current cell (0=E,1=S,2=W,3=N)
    auto open_dir = [&](int c)->bool {
        int dx=(c==0)-(c==2), dy=(c==1)-(c==3); return !solid(cellx+dx, celly+dy); };

    static int last_cell = -1;                     // cell we last made a decision in
    static bool turning = false;                    // currently rotating to a new direction
    if (auto_on) {                                 // ── AUTOPLAY ──
        if (auto_card < 0) { auto_card = 0; last_cell = -1; }
        int this_cell = celly*MAZ_W + cellx;
        // Decide ONCE per cell (no teleport). Straight travel flows smoothly; only when a
        // turn is actually needed do we pin to the cell centre so the pivot is clean.
        if (this_cell != last_cell) {
            last_cell = this_cell;
            if (!open_dir(auto_card)) {             // straight blocked → turn
                int left = (auto_card+3)&3, right = (auto_card+1)&3;
                bool lo = open_dir(left), ro = open_dir(right);
                if (lo && ro)      auto_card = (rnd()&1) ? left : right;
                else if (lo)       auto_card = left;
                else if (ro)       auto_card = right;
                else               auto_card = (auto_card+2)&3;          // dead end → U-turn
                px = cxc; py = cyc;                 // pin to centre only for the turn pivot
                turning = true;                     // rotate to face it before walking on
            }
        }
        // Rotate the view gradually toward the travel direction (visible turn); once aligned,
        // walk forward. Turning blocks forward motion only until aligned, then resumes.
        uint8_t target = (uint8_t)((uint8_t)(auto_card*64) - 64);
        int8_t adiff = (int8_t)(target - pang);
        if (turning && (adiff > 4 || adiff < -4)) {
            pang = (uint8_t)(pang + (adiff > 0 ? 4 : -4));   // ~4/frame visible turn
        } else {
            pang = target; turning = false;
            int dx=(auto_card==0)-(auto_card==2), dy=(auto_card==1)-(auto_card==3);
            if (dx) { px += dx*autosp; py = cyc; }   // advance, snap perpendicular to centre
            else    { py += dy*autosp; px = cxc; }
        }
    } else {                                       // ── MANUAL ──
        last_cell = -1; turning = false;
        auto_card = -1;
        int32_t turn = (shared.knob_main - 2048);
        pang = (uint8_t)(pang - turn / 512);       // smooth view turn (Main only in manual)
        if (shared.pu1_held || shared.sw_position == 2) {
            int32_t sp = 40;
            uint8_t h = (uint8_t)(pang + 64);      // camera forward → cardinal
            int card;
            if      (h < 32 || h >= 224) card = 0;
            else if (h < 96)             card = 1;
            else if (h < 160)            card = 2;
            else                         card = 3;
            int dx = (card==0) - (card==2), dy = (card==1) - (card==3);
            if (!solid(cellx+dx, celly+dy)) { if (dx){px+=dx*sp;py=cyc;} else {py+=dy*sp;px=cxc;} }
            else { px = cxc; py = cyc; }
        } else {                                   // ease to centre when idle
            if (px > cxc) px -= (px-cxc > 24 ? 24 : px-cxc);
            else if (px < cxc) px += (cxc-px > 24 ? 24 : cxc-px);
            if (py > cyc) py -= (py-cyc > 24 ? 24 : py-cyc);
            else if (py < cyc) py += (cyc-py > 24 ? 24 : cyc-py);
        }
    }

    // ── Vector 3D: project the corner VERTICES of visible wall faces and draw their edges
    //    (wireframe corridor). For each wall cell near the player, each side that borders an
    //    OPEN cell is a wall face (a vertical quad); we transform its two base corners into
    //    camera space, perspective-project, and draw the quad's 4 edges (near-clipped).
    const int HALF = GREY_W/2, HORIZON = GREY_H/2;
    const int FOCAL = 90;                  // focal length (px) — sets FOV/zoom
    const int NEAR  = 24;                  // near-plane depth in 8.8 (~0.1 cell)
#ifdef TV_NTSC
    // NTSC crops top/bottom, so shrink the walls vertically → borders top & bottom that let
    // more of the receding top/bottom diagonals show (and nothing clips off the crop).
    const int32_t WALLH = (1 << 8) * 205 / 256;   // ~0.8× wall half-height
#else
    const int32_t WALLH = 1 << 8;          // PAL: full wall half-height (1 cell)
#endif
    int32_t cs = cos_a((uint8_t)(256 - pang)), sn = sin_a((uint8_t)(256 - pang)); // inverse rot

    // World (wx,wy) → camera space (right, forward). 8.8 throughout.
    auto to_cam = [&](int32_t wx, int32_t wy, int32_t &camx, int32_t &camy){
        int32_t rxw = wx - px, ryw = wy - py;
        camx = (rxw*cs - ryw*sn) >> 8;              // right
        camy = (rxw*sn + ryw*cs) >> 8;              // forward (depth)
    };
    // Project a camera-space point (camx, camy forward, v up) → screen (clamped so Bresenham
    // never runs a huge off-screen span). Assumes camy > 0.
    auto proj_cam = [&](int32_t camx, int32_t camy, int32_t v, int &sxp, int &syp){
        if (camy < 1) camy = 1;
        int32_t sxr = HALF + (int)((camx * FOCAL) / camy);
        int32_t syr = HORIZON - (int)((v * FOCAL) / camy);
        if (sxr < -GREY_W)   sxr = -GREY_W;
        if (sxr > 2*GREY_W)  sxr = 2*GREY_W;
        if (syr < -GREY_H)   syr = -GREY_H;
        if (syr > 2*GREY_H)  syr = 2*GREY_H;
        sxp = (int)sxr; syp = (int)syr;
    };
    // Collect visible wall faces, then draw FAR→NEAR (painter's algorithm): each face is a
    // black-filled trapezoid (occludes faces behind it) with bright edges on top → correct
    // wireframe occlusion. The face's near edge is CLIPPED to the near plane so faces you're
    // right up against still render (no see-through) instead of being dropped whole.
    // Store projected corners AND the endpoint depths (da,db) so depth can be interpolated
    // PER COLUMN in the depth buffer (a single per-face average depth is wrong for angled
    // walls and is the cause of the see-through).
    struct Face { int atx,aty,abx,aby, btx,bty,bbx,bby; int32_t da, db; int lvl; bool exit; };
    static Face faces[256];
    int nf = 0;
    auto addface = [&](int32_t ax, int32_t ay, int32_t bx, int32_t by, bool is_exit=false){
        if (nf >= 256) return;
        int32_t cax, cay, cbx, cby;
        to_cam(ax, ay, cax, cay);
        to_cam(bx, by, cbx, cby);
        if (cay <= NEAR && cby <= NEAR) return;     // whole face behind the near plane → skip
        // Clip the segment A→B to camy = NEAR where one endpoint is behind it.
        if (cay <= NEAR) {                          // move A up to the near plane
            int32_t t = ((NEAR - cay) << 8) / (cby - cay);   // 0..256 along A→B
            cax = cax + (((cbx - cax) * t) >> 8);
            cay = NEAR;
        } else if (cby <= NEAR) {                    // move B up to the near plane
            int32_t t = ((NEAR - cby) << 8) / (cay - cby);
            cbx = cbx + (((cax - cbx) * t) >> 8);
            cby = NEAR;
        }
        Face f;
        proj_cam(cax, cay,  WALLH, f.atx, f.aty);
        proj_cam(cax, cay, -WALLH, f.abx, f.aby);
        proj_cam(cbx, cby,  WALLH, f.btx, f.bty);
        proj_cam(cbx, cby, -WALLH, f.bbx, f.bby);
        f.da = cay; f.db = cby;                      // endpoint depths (for per-column interp)
        int32_t avg = (cay + cby) >> 1;
        int lvl = 4 - (int)(avg >> 9);
        if (lvl < 1) lvl = 1;
        if (lvl > 4) lvl = 4;
        f.lvl = lvl;
        f.exit = is_exit;
        faces[nf++] = f;
    };

    int pcx = px>>8, pcy = py>>8;
    for (int gy = pcy-8; gy <= pcy+8; gy++)
        for (int gx = pcx-8; gx <= pcx+8; gx++) {
            if (gx<0||gx>=MAZ_W||gy<0||gy>=MAZ_H) continue;
            if (!wall[gy][gx]) continue;
            int32_t X = gx<<8, Y = gy<<8, U = 1<<8;
            bool ex = (gx==exitx && gy==exity);      // this is the exit wall cell
            if (!solid(gx, gy-1)) addface(X,   Y,   X+U, Y,   ex && exit_face==3); // north
            if (!solid(gx, gy+1)) addface(X,   Y+U, X+U, Y+U, ex && exit_face==1); // south
            if (!solid(gx-1, gy)) addface(X,   Y,   X,   Y+U, ex && exit_face==2); // west
            if (!solid(gx+1, gy)) addface(X+U, Y,   X+U, Y+U, ex && exit_face==0); // east
        }
    // Per-column depth buffer holding inverse depth (1/z, bigger = nearer). Perspective-
    // correct: 1/z is LINEAR in screen space, so we interpolate it per column and compare.
    // Draw order no longer matters — each column keeps whatever is truly nearest.
    static int32_t colinv[GREY_W];
    for (int i = 0; i < GREY_W; i++) colinv[i] = 0;          // 0 = infinitely far
    const int32_t INVK = 1 << 20;
    for (int i = 0; i < nf; i++) {
        Face &f = faces[i];
        int lx = f.atx, rx = f.btx;                 // screen x of the A-end and B-end verticals
        int xl = lx, xr = rx;
        if (xl > xr) { xl = rx; xr = lx; }
        if (xr < 0 || xl >= GREY_W) continue;
        int span = (rx - lx); if (span == 0) span = 1;
        int32_t ia = INVK / (f.da < 1 ? 1 : f.da);  // inverse depths at the two ends
        int32_t ib = INVK / (f.db < 1 ? 1 : f.db);
        // Chunky: draw in 2-wide pixel pairs (double the width of every pixel).
        int x0 = xl < 0 ? 0 : xl, x1 = xr >= GREY_W ? GREY_W-1 : xr;
        x0 &= ~1;                                    // align pairs to even columns
        for (int x = x0; x <= x1; x += 2) {
            int t = ((x - lx) << 8) / span;         // 0..256 from A-end to B-end
            int32_t inv = ia + (((ib - ia) * t) >> 8);       // 1/z at this column pair
            if (inv <= colinv[x]) continue;         // something nearer already owns this pair
            colinv[x] = inv; if (x+1 < GREY_W) colinv[x+1] = inv;
            int yt = f.aty + (((f.bty - f.aty) * t) >> 8);   // top edge y here
            int yb = f.aby + (((f.bby - f.aby) * t) >> 8);   // bottom edge y
            if (yt > yb) { int tmp = yt; yt = yb; yb = tmp; }
            int cyt = yt < 0 ? 0 : yt, cyb = yb >= GREY_H ? GREY_H-1 : yb;
            bool vert = (x <= xl+1) || (x >= xr-1);          // near a vertical face edge
            uint8_t fill = f.exit ? (GREY_LEVELS-1) : 0;     // EXIT face = glowing white panel
            for (int xx = x; xx <= x+1 && xx < GREY_W; xx++) {
                for (int y = cyt; y <= cyb; y++) GREY_SET(grey_buffer, y, xx, fill); // fill
                if (yt >= 0 && yt < GREY_H) GREY_SET(grey_buffer, yt, xx, f.lvl);   // top edge
                if (yb >= 0 && yb < GREY_H) GREY_SET(grey_buffer, yb, xx, f.lvl);   // bottom edge
                if (vert) for (int y = cyt; y <= cyb; y++) GREY_SET(grey_buffer, y, xx, f.lvl);
            }
        }
    }

    // (No monster — the goal is the white EXIT panel; reaching it is detected above.)
}

// ── Alt-boot selector ───────────────────────────────────────────────────────────────
// Alt boot (DOWN held through power-on) offers a set of performance-tool/screensaver
// hybrids, shown one at a time as a SCROLLING selector: the Main knob scrolls through the
// modes. Top line = mode name (left) + "n/N" position (right); below it is the per-mode
// help (inputs/outputs). Switch MID/DOWN plays the shown mode. Add a hybrid: add a name
// here, its help lines below, and a case in the alt_mode dispatch (update_framebuffer).
static const char *ALT_NAMES[] = { "COMET", "PATCHTEROIDS", "BOING", "STARFIELD", "RADAR", "LUNAR", "3DMAZE", "FOURTRIG" };
#define ALT_COUNT ((int)(sizeof(ALT_NAMES)/sizeof(ALT_NAMES[0])))
#define ALT_HYBRID_PATCH 1   // index of PATCHTEROIDS (its CV bridge is special on Core 0)
#define ALT_FOURTRIG     7   // index of FOURTRIG (reads PU1/PU2/audio as its own triggers)
#define ALT_DEFAULT 0        // mode used if you boot straight to MID/DOWN (COMET)
static int alt_select = ALT_DEFAULT;   // chosen hybrid index (Core 1 only)

// Per-mode help: up to 5 short lines (inputs / outputs / notes). "" = blank line.
static const char *ALT_HELP[ALT_COUNT][5] = {
    /* COMET        */ { "MAIN/CV1:SPEED", "Y/CV2:TAIL", "", "", "" },
    /* PATCHTEROIDS */ { "MAIN/CV1:STEER", "PU1/DOWN:FIRE", "OUT1:PITCH", "OUT2:GATE", "" },
    /* BOING        */ { "X/CV1:SPIN/IMPULSE", "MAIN/CV2:BOUNCE", "Y:H-SPEED", "PU1/DOWN:KICK", "OUT1:HT OUT2:HIT" },
    /* STARFIELD    */ { "MAIN:SPEED", "X/CV1:TURN H", "Y/CV2:TURN V", "", "" },
    /* RADAR        */ { "MAIN:AIM PU1:FIRE", "PU2/CV1:PLACE ENEMY", "OUT1:SWEEP", "OUT2:HIT", "" },
    /* LUNAR        */ { "MAIN/CV1:ROTATE", "PU1/DOWN:THRUST", "OUT1:ALT", "OUT2:CRASH", "" },
    /* 3DMAZE       */ { "MAIN:TURN PU1:FWD", "X/CV1:AUTORUN", "PU2:INVERT", "FIND EXIT", "OUT2:EXIT" },
    /* FOURTRIG     */ { "TRIG:A1 A2 PU1 PU2", "X:BANK Y/CV2:SET", "MAIN/CV1:CHAOS", "DOWN:GLITCH", "OUT2:TRIG" },
};

// Draw text right-justified so it ends at grey column `gright` (font advance 9 cells/glyph).
static void draw_text_right(int gright, int gy, const char *s, uint8_t level) {
    int n = 0; for (const char *p = s; *p; p++) n++;
    int gx = gright - n * 9;
    if (gx < 0) gx = 0;
    draw_text(gx, gy, s, level);
}

static void __not_in_flash_func(draw_alt_menu)() {
    text_mode = true;
    // Main knob (0..4095) → selection index; only the current mode is shown (scrolling).
    int sel = (shared.knob_main * ALT_COUNT) / 4096;
    if (sel < 0) sel = 0;
    if (sel >= ALT_COUNT) sel = ALT_COUNT - 1;
    alt_select = sel;
    shared.alt_hybrid = sel;        // tell Core 0 which hybrid's CV behaviour to run

    memset(grey_buffer, 0, GREY_SIZE);

    // Top line: title (left) + "n/N" position (right). Nudged down one char line.
    draw_text(4, 16, ALT_NAMES[sel], GREY_LEVELS - 1);
    char pos[8]; pos[0] = '1' + sel; pos[1] = '/'; pos[2] = '0' + ALT_COUNT; pos[3] = 0;
    draw_text_right(GREY_W - 4, 16, pos, GREY_LEVELS - 1);

    // Per-mode help (inputs/outputs) below.
    for (int h = 0; h < 5; h++) {
        const char *t = ALT_HELP[sel][h];
        if (t && t[0]) draw_text(4, 34 + h * 12, t, GREY_LEVELS - 1);
    }
}

// ── SPECTRUM: audio analyser (Goertzel filter bank) ─────────────────────────────────
// A bank of 24 integer Goertzel detectors, log-spaced ~80 Hz…8 kHz (fs 48 kHz), run once
// per frame over the frame's audio block (Core 1). Each → one vertical bar (low freq left).
// coeff = 2·cos(2π·f/fs) in Q13 (×8192). Bars decay smoothly; UP = spiky peak-hold, MID =
// solid bargraph. Knob Y = gain (shared.scope_audio_scale), like the scope.
#define SPEC_BANDS 24
static const int32_t spec_coeff[SPEC_BANDS] = {
    16383,16383,16382,16381,16380,16377,16374,16369,16362,16351,16335,16311,
    16274,16220,16140,16020,15842,15578,15186,14607,13755,12514,10725,8192 };
// Per-band gain TILT (Q8, 256 = 1×): cut the low end, lift the highs so a swept sine reads
// roughly even (bass is otherwise over-represented — many log bands + more natural LF
// energy). Moderate rising ramp ~0.27× (low) → ~3.0× (high) — between the too-flat and
// over-corrected extremes.
static const int32_t spec_tilt[SPEC_BANDS] = {
    54,64,76,90,107,127,151,179,213,253,300,356,
    423,502,596,708,840,997,1184,1405,1668,1980,2350,2790 };

static void __not_in_flash_func(spectrum_render)(uint8_t sw, int32_t knob, bool swap) {
    static int  bar[SPEC_BANDS]  = {0};   // current (decaying) magnitudes, cells

    dilate_cap = 1;

    // Spectrum's OWN X/Y, resolved by the Core-0 pickup system (PICK_SPEC_GAIN/ROT) so they
    // are distinct from the scope's baseline/gain and only change when the knob is physically
    // moved (no jump when entering the mode).
    int32_t ky = shared.spec_gain;                 // 0..4095 → gain
    int32_t kx = shared.spec_rot;                  // 0..4095 → radial rotate offset

    // Decay speed set by how far through the SPECTRUM third the Main knob is (like the fade-
    // rate/sweep-speed sub-mappings of the other two zones). Low end of the zone = slow,
    // lingering decay; high end = fast, snappy.
    int32_t kspan = 4095 - SPECTRUM_THRESH;
    int32_t kpos  = knob - SPECTRUM_THRESH;
    if (kpos < 0) kpos = 0;
    if (kpos > kspan) kpos = kspan;
    int fall = 1 + (kpos * 6) / kspan;            // bar fall: 1 (slow) .. 7 (fast) cells/frame

    // ── Analyse the frame's audio block. Read the newest N samples (don't disturb the
    //    scope's audio_read_idx — take a private window ending at the write head). ──
    const int N = 512;
    uint32_t aw = audio_write_idx;
    for (int b = 0; b < SPEC_BANDS; b++) {
        int32_t c = spec_coeff[b];
        int32_t s1 = 0, s2 = 0;
        uint32_t idx = aw - N;
        for (int n = 0; n < N; n++) {
            int32_t x = audio_ring[idx & AUDIO_RING_MASK] >> 3;   // scale down (overflow guard)
            int32_t s0 = ((c * s1) >> 13) - s2 + x;
            s2 = s1; s1 = s0;
            idx++;
        }
        // Goertzel magnitude²: s1² + s2² − coeff·s1·s2 . Then sqrt → magnitude.
        int32_t p = s1*s1 + s2*s2 - (int32_t)(((int64_t)c * s1 * s2) >> 13);
        if (p < 0) p = 0;
        int32_t mag = (int32_t)isqrt_u((uint32_t)p);
        mag = (mag * spec_tilt[b]) >> 8;                          // per-band tilt (tame lows)
        // Gain from Knob Y — gentle so the useful range spans the whole knob (not maxed in
        // the bottom 20%). h in cells.
        int h = (int)(((int64_t)mag * ky) >> 16);                 // tune the shift for range
        if (h > GREY_H - 1) h = GREY_H - 1;
        // Decay: bars fall smoothly instead of snapping down.
        if (h >= bar[b]) bar[b] = h; else { bar[b] -= fall; if (bar[b] < h) bar[b] = h; }
    }

    if (sw == 1) {
        // ── MID: vertical bargraph, each bar styled like a stack of LEDs — solid white with
        //    thin BLACK divider lines every SEG cells; the topmost occupied segment is grey-
        //    shaded by its fractional fill (5 levels). SWAP flips bass↔treble across X. ──
        memset(grey_buffer, 0, GREY_SIZE);
        const int bw  = GREY_W / SPEC_BANDS;       // ~7 px per band
        const int SEG = 6;                         // LED segment height (cells)
        const int base = GREY_H - 1 - NTSC_BOTTOM_INSET;   // bar baseline (raised on NTSC)
        for (int b = 0; b < SPEC_BANDS; b++) {
            int bi = swap ? (SPEC_BANDS - 1 - b) : b;   // swap → reverse bin order on screen
            int x0 = bi * bw, x1 = x0 + bw - 1;    // 1px gap between bars
            int hgt = bar[b];
            for (int seg_base = 0; seg_base < hgt; seg_base += SEG) {
                int seg_fill = hgt - seg_base;      // cells filled in this segment
                bool topmost = (seg_fill < SEG);
                int lvl = GREY_LEVELS - 1;          // full segments = white
                if (topmost) {                      // part-lit top: level by fraction
                    lvl = 1 + (seg_fill * (GREY_LEVELS - 1)) / SEG;   // 1..4
                    if (lvl > GREY_LEVELS - 1) lvl = GREY_LEVELS - 1;
                }
                int cells = topmost ? seg_fill : (SEG - 1);   // leave 1 cell for the divider
                int ytop = base - (seg_base + cells - 1);
                int ybot = base - seg_base;
                for (int gx = x0; gx < x1 && gx < GREY_W; gx++)
                    for (int gy = ytop; gy <= ybot; gy++)
                        if (gy >= 0 && gy < GREY_H) GREY_SET(grey_buffer, gy, gx, lvl);
            }
        }
    } else {
        // ── UP: RADIAL BLOB — 24 vertices at even angles, pushed out by magnitude and
        //    CONNECTED around the perimeter (closed loop). FADE (not clear) → grey echo trail.
        //    Knob X rotates the whole blob; SWAP reverses the band→angle order. ──
        static int echo_ctr = 0;
        int echo_every = 8 - fall;
        if (echo_every < 1) echo_every = 1;
        if (++echo_ctr >= echo_every) { echo_ctr = 0; fade_step(); }
        const int ccx = GREY_W / 2, ccy = GREY_H / 2;
        const int RMIN = 6, RMAX = GREY_H / 2 - 3;
        uint8_t rot = (uint8_t)(kx >> 4);          // Knob X → 0..255 rotation offset
        int vx[SPEC_BANDS], vy[SPEC_BANDS];
        for (int b = 0; b < SPEC_BANDS; b++) {
            int bi = swap ? (SPEC_BANDS - 1 - b) : b;
            uint8_t a = (uint8_t)((bi * 256) / SPEC_BANDS + rot);
            int len = RMIN + (bar[b] * (RMAX - RMIN)) / (GREY_H - 1);
            vx[b] = ccx + (cos_a(a) * len >> 8);
            vy[b] = ccy + (sin_a(a) * len >> 8);
        }
        // 2×2-thick perimeter (plot_dot is 2px wide; one extra vertical offset → 2×2).
        for (int b = 0; b < SPEC_BANDS; b++) {
            int nb = (b + 1) % SPEC_BANDS;
            draw_line(vx[b], vy[b],   vx[nb], vy[nb],   GREY_LEVELS-1);
            draw_line(vx[b], vy[b]+1, vx[nb], vy[nb]+1, GREY_LEVELS-1);
        }
    }
}

static void __not_in_flash_func(update_framebuffer)() {
    text_mode = false;   // default: normal (dilated) rendering; text screens set it true
    dilate_cap = WHITE_DILATE;   // default full dilation; some screens lower it per-frame
    // Snapshot volatile shared state once. (CV is read per-sample from the etch
    // ring below, not from this snapshot.)
    int32_t  knob    = shared.knob_main;
    int32_t  ox      = shared.etch_offset_x;   // resolved etch X/Y mapping (Core 0 pickup)
    int32_t  oy      = shared.etch_offset_y;
    int32_t  gxn     = shared.etch_cvgain_x;
    int32_t  gyn     = shared.etch_cvgain_y;
    uint8_t  sw      = shared.sw_position;
    // Normal-mode switch view with UP<->MID SWAPPED (DOWN unchanged). All normal-mode
    // render/behaviour uses nsw; alt-boot keeps the raw physical sw_position.
    uint8_t  nsw     = (sw == 0) ? 1 : (sw == 1) ? 0 : 2;
    MainMode mode    = main_mode(knob);        // thirds: etch / scope / spectrum
    // (Pulse In 1 is now a configurable trigger source — see below; clearing the screen
    //  is available as the CLS behaviour.)

    // Alt boot mode: a selectable set of performance-tool/screensaver hybrids.
    // Switch UP = selector menu (Main knob picks); MID/DOWN = run the selected hybrid.
    if (shared.alt_mode) {
        effect_invert = false;                    // default; a mode may set it (maze: PU2)
        if (shared.sw_position == 0) {           // UP → selector
            draw_alt_menu();
        } else {                                  // MID/DOWN → play selected
            shared.alt_hybrid = alt_select;       // keep Core 0's CV bridge in sync
            switch (alt_select) {
                case 1:  screensaver_asteroids(); break;   // PATCHTEROIDS
                case 2:  screensaver_boing();     break;   // BOING (rotating ball)
                case 3:  screensaver_starfield(); break;   // STARFIELD (fly through space)
                case 4:  screensaver_radar();     break;   // RADAR (radar shooter)
                case 5:  screensaver_lunar();     break;   // LUNAR (lunar lander)
                case 6:  screensaver_maze();      break;   // 3DMAZE (first-person maze)
                case 7:  screensaver_fourtrig();  break;   // FOURTRIG (four trigger stamps)
                default: screensaver_bounce();    break;   // 0 = COMET
            }
        }
        expand_grey_to_fb();
        return;
    }

    // Config menu takeover (entered by moving X/Y while DOWN — state owned by Core 0).
    // Force invert off so the menu is always readable (white-on-black).
    if (shared.menu_active) { effect_invert = false; draw_menu(); expand_grey_to_fb(); return; }

    // Three independent trigger sources: switch-DOWN→cfg_sw, PU1→cfg_pu1, PU2→cfg_pu2.
    // Each has its own FxState so they don't fight. Read-and-clear the rising latches.
    bool down_held   = shared.down_held;
    bool down_rising = shared.down_rising; shared.down_rising = false;
    bool pu1_held    = shared.pu1_held;
    bool pu1_rising  = shared.pu1_rising;  shared.pu1_rising  = false;
    bool pu2_held    = shared.pu2_held;
    bool pu2_rising  = shared.pu2_rising;  shared.pu2_rising  = false;

    bool swap_xy = false;
    fx_down.strobe_invert = false;
    fx_pu1.strobe_invert  = false;
    fx_pu2.strobe_invert  = false;
    bool finished = false;
    finished |= apply_behaviour(shared.cfg_sw,  down_held, down_rising, fx_down, swap_xy);
    finished |= apply_behaviour(shared.cfg_pu1, pu1_held,  pu1_rising,  fx_pu1,  swap_xy);
    finished |= apply_behaviour(shared.cfg_pu2, pu2_held,  pu2_rising,  fx_pu2,  swap_xy);
    effect_invert = fx_down.strobe_invert || fx_pu1.strobe_invert || fx_pu2.strobe_invert;
    if (finished) { expand_grey_to_fb(); return; }

    if (mode == MODE_SPECTRUM) {
        etch_have_prev = false;
        spectrum_render(nsw, knob, swap_xy);   // knob-in-zone = decay; swap reverses bins
        expand_grey_to_fb();
        return;
    }

    if (mode == MODE_SCOPE) {
        etch_have_prev = false;   // in scope mode: next etch sample starts fresh

        // Sweep speed from knob: lerp SCOPE_STEP_SLOW..FAST across the MIDDLE third of the
        // knob (ETCH_THRESH..SPECTRUM_THRESH). 8.8 fixed-point cols/frame.
        int32_t kspan = SPECTRUM_THRESH - ETCH_THRESH;
        int32_t kpos  = knob - ETCH_THRESH; if (kpos < 0) kpos = 0; if (kpos > kspan) kpos = kspan;
        uint32_t step = SCOPE_STEP_SLOW +
            (uint32_t)((int64_t)(SCOPE_STEP_FAST - SCOPE_STEP_SLOW) * kpos / kspan);

        // Centre line (0V baseline) row is set by the X knob in scope mode.
        const int mid = clamp(shared.scope_baseline, 0, GREY_H - 1);
        int32_t ascale = shared.scope_audio_scale;   // Y knob: audio gain (256 = 1×)

        // Advance the sweep by `step` (8.8). For each whole column crossed, pull a
        // FRESH audio sample (so fast sweeps draw a real waveform, not one fat bar/
        // frame), draw the bar there, and drive the sweep-locked fade.
        const uint32_t FADE_EVERY_COLS = GREY_W / (GREY_LEVELS - 1);  // 180/4 = 45
        uint32_t aw = audio_write_idx;                 // snapshot Core 0 write head
        // How many fresh audio samples are available to consume this frame.
        uint32_t avail = aw - audio_read_idx;
        if (avail > AUDIO_RING_SIZE) {                 // overran: keep only the newest
            audio_read_idx = aw - AUDIO_RING_SIZE;
            avail = AUDIO_RING_SIZE;
        }
        // Count whole columns we'll draw this frame, to spread audio samples across them.
        scope_acc += step;
        uint32_t cols_this_frame = scope_acc >> 8;
        uint32_t consumed = 0;
        while (scope_acc >= 256) {
            scope_acc -= 256;
            int gx = scope_x;
            // FX_SWAP_XY in scope = reverse sweep direction while held.
            if (swap_xy) scope_x = (scope_x + GREY_W - 1) % GREY_W;  // step left
            else         scope_x = (scope_x + 1) % GREY_W;           // step right

            // Pick this column's audio sample. If we have at least as many fresh
            // samples as columns, step through them evenly; else take the latest.
            int16_t a;
            if (avail >= cols_this_frame && cols_this_frame > 0) {
                // even decimation: map column index → sample index across the frame
                uint32_t si = audio_read_idx + (consumed * avail / cols_this_frame);
                a = audio_ring[si & AUDIO_RING_MASK];
            } else {
                a = audio_ring[(aw - 1) & AUDIO_RING_MASK];  // latest sample
            }
            consumed++;

            // Scale audio around 0 by the Y-knob gain (256 = 1×), then map to a row
            // with +ve voltage going UP (row 0 = top): gpy = mid - scaled·mid/2048.
            int32_t sc = ((int32_t)a * ascale) >> 8;        // gain applied around 0
            int gpy = mid - (int)((sc * mid) / 2048);       // invert: +ve = up
            gpy = clamp(gpy, 0, GREY_H - 1);
            int lo = gpy < mid ? gpy : mid;
            int hi = gpy < mid ? mid : gpy;

            if (nsw == 1) {  // (swapped) static: clear column for a single clean trace
                for (int gy = 0; gy < GREY_H; gy++) GREY_SET(grey_buffer, gy, gx, 0);
            }
            for (int gy = lo; gy <= hi; gy++)
                GREY_SET(grey_buffer, gy, gx, GREY_LEVELS - 1);

            if (nsw == 0) {  // (swapped) fade locked to sweep
                if (++fade_cols >= FADE_EVERY_COLS) { fade_cols = 0; fade_step(); }
            }
        }
        audio_read_idx = aw;   // consumed up to the snapshot head
    } else {   // MODE_ETCH (spectrum returned early above)
        // Etch-a-sketch: drain every CV sample Core 0 queued and plot at white (L2).
        uint32_t w = etch_write_idx;        // snapshot Core 0's write head
        uint32_t avail = w - etch_read_idx; // unsigned wrap-safe count
        if (avail > ETCH_RING_SIZE) {       // overran: only the last RING_SIZE survive
            etch_read_idx = w - ETCH_RING_SIZE;
        }
        while (etch_read_idx != w) {
            uint32_t slot = etch_read_idx & ETCH_RING_MASK;
            int32_t rx = etch_x_ring[slot];
            int32_t ry = etch_y_ring[slot];
            // pos = offset + (cv * cvgain) >> 12.  offset & cvgain resolved on Core 0.
            int gx = ox + (int)((rx * gxn) >> 12);
            int gy = oy + (int)((ry * gyn) >> 12);
            if (swap_xy) { int t = gx; gx = gy; gy = t; }   // FX_SWAP_XY: transpose axes
            gx = clamp(gx, 0, GREY_W - 1);
            gy = clamp(gy, 0, GREY_H - 1);
            etch_read_idx++;

            // Deadband: ignore samples within ETCH_DEADBAND cells of the last anchor.
            // CV In has ±1–2 LSB jitter; at half-res that flips the cell and, with the
            // line interpolation accumulating in static mode, paints a parallel ghost
            // line. Only commit a new anchor once CV genuinely moves beyond the band —
            // and DON'T update the anchor when skipping, so wobble can't drift it.
            if (etch_have_prev) {
                int adx = gx - etch_prev_x; if (adx < 0) adx = -adx;
                int ady = gy - etch_prev_y; if (ady < 0) ady = -ady;
                if (adx <= ETCH_DEADBAND && ady <= ETCH_DEADBAND) continue;
                draw_line(etch_prev_x, etch_prev_y, gx, gy, GREY_LEVELS - 1);
            } else {
                plot_dot(gx, gy, GREY_LEVELS - 1);
            }
            etch_prev_x = gx;
            etch_prev_y = gy;
            etch_have_prev = true;
        }
        // Etch fade (now on MIDDLE after the UP<->MID swap): rate from knob across the etch
        // zone — knob 0 = fastest (~0.25s), knob (ETCH_THRESH-1) = slowest (~3s).
        if (nsw == 0) {
            int32_t kp = knob; if (kp < 0) kp = 0; if (kp >= ETCH_THRESH) kp = ETCH_THRESH - 1;
            uint32_t interval = ETCH_FADE_FAST +
                (uint32_t)((ETCH_FADE_SLOW - ETCH_FADE_FAST) * kp / (ETCH_THRESH - 1));
            if (++fade_cols >= interval) { fade_cols = 0; fade_step(); }
        }
    }

    // Expand grey buffer → 1-bit frame_buffer (must finish before build_frame_words)
    expand_grey_to_fb();
}

// ─────────────────────────────────────────────────────────────────────────────
// DMA IRQ handler (Core 1)
// Fires when the DMA finishes the current frame word stream.
// We restart the DMA immediately (to avoid any gap in video output),
// then signal Core 1's update loop via vblank_ready.
// ─────────────────────────────────────────────────────────────────────────────
static void __not_in_flash_func(dma_irq_handler)() {
    dma_hw->ints1 = 1u << dma_chan;  // clear IRQ flag on DMA_IRQ_1

    // Restart DMA from the same (now-completed) active buffer — next frame uses same data
    // until Core 1 swaps in a new back buffer. This ensures video never glitches.
    dma_channel_set_read_addr(dma_chan, word_buf[active_buf], true);

    vblank_ready = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Core 1 entry point
// ─────────────────────────────────────────────────────────────────────────────
static void __not_in_flash_func(core1_main)() {
    // Claim PIO0 SM0
    PIO pio = pio0;
    uint sm  = 0;

    // Override GPIO 8 (Pu1) and GPIO 9 (Pu2) from ComputerCard's gpio_out to PIO control.
    // Both pins form the 2-bit video DAC; Pulse Out 2 is no longer a normal output.
    gpio_set_function(VIDEO_GPIO,     GPIO_FUNC_PIO0);
    gpio_set_function(VIDEO_GPIO + 1, GPIO_FUNC_PIO0);

    // Load PIO program
    uint offset = pio_add_program(pio, &video_out_program);

    // Configure SM
    pio_sm_config c = video_out_program_get_default_config(offset);
    sm_config_set_out_pins(&c, VIDEO_GPIO, 2);     // GPIO 8,9 = 2-bit video DAC
    sm_config_set_out_shift(&c, false, true, 32);  // shift left, autopull, threshold=32
    sm_config_set_clkdiv(&c, 144.0f/7.0f);         // 144 MHz / 20.571 = 7.000 MHz = 142.857 ns/pixel

    pio_sm_set_consecutive_pindirs(pio, sm, VIDEO_GPIO, 2, true);  // GPIO 8,9 output
    pio_sm_init(pio, sm, offset, &c);

    // Claim DMA channel (use a channel not used by ComputerCard)
    // ComputerCard uses channels for ADC DMA (dma_claim_unused_channel) and SPI DMA.
    // We claim one explicitly: channel 4 (ComputerCard uses 0,1,2,3 typically).
    // Safe approach: use dma_claim_unused_channel at startup before ComputerCard inits.
    // Since Core 1 starts after Run(), we use channel 6 (well above CC's range).
    dma_chan = 6;
    dma_channel_claim(dma_chan);

    // Build initial frame (back buffer = 1, active = 0)
    memset(grey_buffer, 0, GREY_SIZE);
    memset(frame_buffer, 0, FB_SIZE);
    expand_grey_to_fb();          // ensure frame_buffer is valid before first pack
    build_frame_words(0, false);
    build_frame_words(1, false);
    active_buf = 0;

    // Configure DMA: read from word_buf[0], write to PIO TX FIFO, loop
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, true));  // pace to PIO TX FIFO

    dma_channel_configure(
        dma_chan, &dc,
        &pio->txf[sm],           // write to PIO SM TX FIFO
        word_buf[active_buf],    // read from front buffer
        FRAME_WORDS,             // transfer count
        false                    // don't start yet
    );

    // Enable DMA IRQ on channel 6, use DMA_IRQ_1 (Core 1 owns it; Core 0's ComputerCard uses DMA_IRQ_0)
    dma_channel_set_irq1_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    // Start PIO SM and DMA
    pio_sm_set_enabled(pio, sm, true);
    dma_channel_start(dma_chan);

    // Main Core 1 loop: update framebuffer each vblank
    while (1) {
        // Wait for DMA IRQ to signal frame completion
        while (!vblank_ready) {
            tight_loop_contents();
        }
        vblank_ready = false;

        // Advance the dither orientation every 2 frames (rotates the 2×2 patterns so
        // greys average out over time → smoother, less fixed checkerboard).
        static uint32_t frame_counter = 0;
        frame_counter++;
        dither_orient = (frame_counter >> 1) & 3;

        // Update framebuffer with new Eurorack I/O state
        update_framebuffer();

        // Build new word stream into back buffer
        int back = 1 - active_buf;
        bool invert = effect_invert;  // INVERT behaviour / strobe set this on Core 1
        build_frame_words(back, invert);

        // Swap buffers: next DMA IRQ handler will restart using the new active_buf.
        // The current DMA is already running (restarted in IRQ handler) using the OLD active_buf.
        // We update active_buf now so the NEXT restart uses the new one.
        active_buf = back;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputerCard subclass — Core 0
// ─────────────────────────────────────────────────────────────────────────────
class CathodeRay : public ComputerCard {
public:
    CathodeRay() {
        memset((void *)&shared, 0, sizeof(shared));
        shared.cfg_sw  = BHV_CYCLE_FX;    // defaults reproduce original firmware
        shared.cfg_pu1 = BHV_CYCLE_FX;
        shared.cfg_pu2 = BHV_INVERT;
        // Core 1 is launched here (constructor body), before Run() is called.
        // ComputerCard's hardware init runs in its constructor (which has already
        // completed by this point), so GPIO 8 is set up as gpio_out.
        // Core 1 will override it to PIO immediately on entry.
        // ComputerCard claims its DMA channels inside AudioWorker() (called from Run()),
        // which happens after this constructor returns — so Core 1's dma_channel_claim
        // for channel 6 races with nothing: CC uses channels 0 and 1.
        multicore_launch_core1(core1_trampoline);
    }

    static void core1_trampoline() {
        core1_main();
    }

    void __not_in_flash_func(ProcessSample)() override {
        // Main knob: far-CCW = etch (CV1 vs CV2); rest = scope, slow→fast CW.
        shared.knob_main = KnobVal(Knob::Main);

        shared.audio_y      = AudioIn1();
        shared.cv_x         = CVIn1();
        shared.cv_y         = CVIn2();

        // Push this CV sample into the etch ring so Core 1 can plot every sample.
        // Write the data BEFORE advancing the index so Core 1 never reads a half-
        // updated slot (single 32-bit index write is atomic on RP2040).
        uint32_t w = etch_write_idx;
        etch_x_ring[w & ETCH_RING_MASK] = (int16_t)shared.cv_x;
        etch_y_ring[w & ETCH_RING_MASK] = (int16_t)shared.cv_y;
        etch_write_idx = w + 1;

        // Push the audio sample into the audio ring (for the scope waveform).
        uint32_t aw = audio_write_idx;
        audio_ring[aw & AUDIO_RING_MASK] = (int16_t)shared.audio_y;
        audio_write_idx = aw + 1;

        // Pulse In 1 is its own trigger source (cfg_pu1). Held + latched rising edge
        // (Core 1 reads-and-clears; a Core 1 poll could miss fast pulses).
        shared.pu1_held = PulseIn1();
        if (PulseIn1RisingEdge()) shared.pu1_rising = true;

        // Pulse In 2 as a trigger source: publish held level + latch the rising edge.
        shared.pu2_held = PulseIn2();
        if (PulseIn2RisingEdge()) shared.pu2_rising = true;

        // Audio In 1/2 as trigger inputs (FourTrig). Edge-detect a rising crossing of
        // +AIN_HI with hysteresis: fire once when it rises past AIN_HI, re-arm only after
        // it falls back below AIN_LO. Works for audio/percussive transients and gates.
        #define AIN_HI 500
        #define AIN_LO 150
        static bool ain1_armed = true, ain2_armed = true;
        int32_t a1 = AudioIn1(), a2 = AudioIn2();
        if (ain1_armed && a1 > AIN_HI) { shared.ain1_rising = true; ain1_armed = false; }
        else if (!ain1_armed && a1 < AIN_LO) ain1_armed = true;
        if (ain2_armed && a2 > AIN_HI) { shared.ain2_rising = true; ain2_armed = false; }
        else if (!ain2_armed && a2 < AIN_LO) ain2_armed = true;

        Switch sw = SwitchVal();
        if      (sw == Switch::Up)     shared.sw_position = 0;
        else if (sw == Switch::Middle) shared.sw_position = 1;
        else                           shared.sw_position = 2;

        // ── Dual-function X/Y knob pickup ──────────────────────────────────────
        // DOWN → knobs edit OFFSET; UP/MIDDLE → knobs edit SCALE. Boot holdoff: the
        // switch reads Down before the ADC settles (CLAUDE.md), so don't run pickup
        // until settled, or a spurious boot-DOWN freezes both knobs uncaptured.
        static uint32_t boot_ctr = 0;
        static KnobPick pkX, pkY;
        static bool pick_init = false;
        if (!pick_init) {   // defaults: centred offset/baseline, unity scale & audio, mid spec
            //     offset,        scale, audio, baseline,     specgain, specrot, bound,     bind, captured
            pkX = { (GREY_W-1)/2, 256,   256,   (GREY_H-1)/2, 2048,     2048,    PICK_SCALE, 0,   true };
            pkY = { (GREY_H-1)/2, 256,   256,   (GREY_H-1)/2, 2048,     2048,    PICK_SCALE, 0,   true };
            pick_init = true;
        }
        uint8_t swp = shared.sw_position;     // 0=UP 1=MID 2=DOWN (physical)
        // Normal-mode view with UP<->MID swapped (DOWN unchanged) — used for the etch
        // scale/offset knob-role choice so it matches the render-side swap.
        uint8_t nswp = (swp == 0) ? 1 : (swp == 1) ? 0 : 2;
        bool down = (swp == 2);

        // ── Alt-boot latch + UI state machine (Core 0 owns) ────────────────────
        // Latch alt-mode once the ADC has settled: DOWN held through settle = alt mode.
        // seen_release gates the UI machine so a boot-hold doesn't also fire an effect.
        static bool boot_latched = false, seen_release = false;
        static uint8_t ui_state = 0;          // 0=NORMAL 1=EFFECT 2=MENU
        static int32_t entry_x = 0, entry_y = 0, entry_m = 0;  // knob raw at DOWN press
        static int32_t menu_ax = 0, menu_ay = 0, menu_am = 0;  // knob raw at menu entry
        static bool menu_x_eng = false, menu_y_eng = false, menu_m_eng = false;
        static bool prev_down = false;
        int32_t rawX = KnobVal(Knob::X), rawY = KnobVal(Knob::Y);
        shared.knob_x = rawX; shared.knob_y = rawY;   // publish raw X/Y for alt-mode hybrids
        int32_t rawM = shared.knob_main;      // Main knob (selects cfg_sw in the menu)
        if (!boot_latched && boot_ctr >= 4800) {
            shared.alt_mode = down;           // DOWN held through boot → screensaver
            boot_latched = true;
        }
        if (!seen_release && boot_ctr >= 4800 && !down) seen_release = true;

        // ── Alt-mode CV bridge (hybrid-aware) ──────────────────────────────────
        // Common to all hybrids: PU1 rising latches ast_fire (Patchteroids = fire, Boing =
        //   kick); CVOut2 fires a ~10ms gate whenever ast_gate_seq changes (Patchteroids =
        //   a hit; Boing = a floor bounce).
        // Patchteroids ONLY (alt_hybrid==0): CV1 adds to the Main knob (steering); CVOut1 =
        //   pitch from ast_note. Other hybrids read Main/CV1 raw and drive CVOut1 = alt_cv1
        //   (Boing: ball height).
        // Only run the game CV bridge while PLAYING (switch MID/DOWN). When the SELECTOR is
        // showing (switch UP), leave knob_main raw so only the Main knob picks the mode
        // (CV1 must not fold into it, or it would drift the selection).
        if (shared.alt_mode && shared.sw_position != 0) {
            // PU1 → ast_fire for the game hybrids (Patchteroids/Boing). FOURTRIG reads
            // pu1_rising itself (as one of its four triggers), so don't consume it there.
            if (shared.alt_hybrid != ALT_FOURTRIG &&
                shared.pu1_rising) { shared.ast_fire = true; shared.pu1_rising = false; }

            if (shared.alt_hybrid == ALT_HYBRID_PATCH) {       // PATCHTEROIDS
                int32_t steer = shared.knob_main + shared.cv_x;   // cv_x = CVIn1, ±2048
                if (steer < 0) steer = 0;
                if (steer > 4095) steer = 4095;
                shared.knob_main = steer;                          // game reads this as turn

                // Pitch: MIDI note 36 (C2) + accumulated semitones, calibrated v/oct.
                int n = 36 + (int)shared.ast_note;
                if (n < 0) n = 0;
                if (n > 127) n = 127;
                CVOut1MIDINote((uint8_t)n);
            } else {                                           // other hybrids
                // CVOut1 = the hybrid's generic value (Boing: ball height). Raw, ±2048.
                int32_t v = shared.alt_cv1;
                if (v < -2048) v = -2048;
                if (v > 2047)  v = 2047;
                CVOut1((int16_t)v);
            }

            // Gate: edge-detect the sequence counter, emit a ~10ms high pulse on CVOut2.
            static uint32_t gate_seen = 0;
            static int      gate_ctr  = 0;
            uint32_t seq = shared.ast_gate_seq;
            if (seq != gate_seen) { gate_seen = seq; gate_ctr = 480; }  // 480 @48k ≈ 10ms
            if (gate_ctr > 0) { CVOut2(2047); gate_ctr--; } else CVOut2(-2048);
        }

        bool ui_down = false, ui_down_rising = false;  // signals published to Core 1
        if (!shared.alt_mode && seen_release) {
            bool rising = down && !prev_down;
            switch (ui_state) {
                case 0: // NORMAL
                    if (rising) { ui_state = 1; entry_x = rawX; entry_y = rawY; entry_m = rawM; }
                    break;
                case 1: // EFFECT — running an effect; a knob move opens the menu
                    if (!down) { ui_state = 0; }
                    else {
                        int dx = rawX-entry_x; if (dx<0) dx=-dx;
                        int dy = rawY-entry_y; if (dy<0) dy=-dy;
                        int dm = rawM-entry_m; if (dm<0) dm=-dm;
                        if (dx >= MENU_MOVE_THRESH || dy >= MENU_MOVE_THRESH || dm >= MENU_MOVE_THRESH) {
                            ui_state = 2; menu_ax = rawX; menu_ay = rawY; menu_am = rawM;
                            menu_x_eng = menu_y_eng = menu_m_eng = false;
                        }
                    }
                    break;
                case 2: // MENU
                    if (!down) ui_state = 0;
                    break;
            }
            if (ui_state == 1) { ui_down = down; ui_down_rising = rising; }
        }
        prev_down = down;
        shared.down_held = ui_down;
        if (ui_down_rising) shared.down_rising = true;
        shared.menu_active = (ui_state == 2);

        // Config menu live selection: Main → cfg_sw, X → cfg_pu1, Y → cfg_pu2. Each
        // engages only after moving ≥PICKUP_THRESH from the menu-entry anchor, so the
        // knob that opened the menu doesn't instantly slam a value.
        if (ui_state == 2) {
            int dx = rawX-menu_ax; if (dx<0) dx=-dx;
            int dy = rawY-menu_ay; if (dy<0) dy=-dy;
            int dm = rawM-menu_am; if (dm<0) dm=-dm;
            if (dx >= PICKUP_THRESH) menu_x_eng = true;
            if (dy >= PICKUP_THRESH) menu_y_eng = true;
            if (dm >= PICKUP_THRESH) menu_m_eng = true;
            if (menu_m_eng) shared.cfg_sw  = clamp(rawM*BHV_COUNT/4096, 0, BHV_COUNT-1);
            if (menu_x_eng) shared.cfg_pu1 = clamp(rawX*BHV_COUNT/4096, 0, BHV_COUNT-1);
            if (menu_y_eng) shared.cfg_pu2 = clamp(rawY*BHV_COUNT/4096, 0, BHV_COUNT-1);
        }
        // Knob destinations by mode + switch (3-way: etch / scope / spectrum):
        //   ETCH  → X/Y = SCALE (UP-swapped) or OFFSET
        //   SCOPE → X = BASELINE, Y = AUDIO gain
        //   SPECTRUM → X = SPEC_ROT, Y = SPEC_GAIN (own values, distinct from scope)
        //   DOWN → performance effect; knobs hold their current binding (no change).
        uint8_t desiredX, desiredY;
        MainMode pmode = main_mode(shared.knob_main);
        if (pmode == MODE_ETCH) {
            desiredX = desiredY = (nswp == 0) ? PICK_SCALE : PICK_OFFSET; // (swapped) scale/offset
        } else if (pmode == MODE_SPECTRUM) {
            desiredX = PICK_SPEC_ROT;   // spectrum: X = rotate/position
            desiredY = PICK_SPEC_GAIN;  // spectrum: Y = gain
        } else {
            desiredX = PICK_BASELINE; // scope: X = baseline vertical position
            desiredY = PICK_AUDIO;    // scope: Y = audio gain
        }
        if (down) {                   // DOWN drives the effect; freeze knob bindings
            desiredX = pkX.bound;
            desiredY = pkY.bound;
        }

        auto run_pick = [&](KnobPick &p, int32_t raw, int32_t maxpos, uint8_t desired,
                            bool invert_offset) {
            if (desired != p.bound) {          // destination changed (mode/switch)
                p.bound = desired;
                p.bind_raw = raw;              // remember physical position now
                p.captured = false;           // hold stored value until knob moves
            }
            if (!p.captured) {
                int32_t d = raw - p.bind_raw; if (d < 0) d = -d;
                if (d >= PICKUP_THRESH) p.captured = true;
            }
            if (p.captured) {
                if (p.bound == PICK_OFFSET) {
                    int32_t off = (raw * maxpos) / 4095;
                    p.stored_offset = invert_offset ? (maxpos - off) : off;  // Y flips
                } else if (p.bound == PICK_AUDIO) {
                    p.stored_audio = (raw * 512) / 4095;    // 0..2× audio gain (256 = 1×)
                } else if (p.bound == PICK_BASELINE) {
                    // scope centre-line vertical position, 0..GREY_H-1 (0V baseline row)
                    p.stored_baseline = (raw * (GREY_H - 1)) / 4095;
                } else if (p.bound == PICK_SPEC_GAIN) {
                    p.stored_specgain = raw;        // spectrum gain: raw 0..4095 (own value)
                } else if (p.bound == PICK_SPEC_ROT) {
                    p.stored_specrot  = raw;        // spectrum rotate/pos: raw 0..4095
                } else { // PICK_SCALE: 0..1024 (256=1×) attenuate/boost CV up to 4×
                    p.stored_scale = (raw * 1024) / 4095;
                }
            }
        };

        // On DOWN release, force both knobs to RE-BIND (captured=false, bind_raw=current)
        // so any movement during the effect/menu doesn't snap the etch/scope params when
        // control resumes — the knob must be deliberately moved again to take over.
        static bool pick_prev_down = false;
        if (pick_prev_down && !down) {
            pkX.captured = false; pkX.bind_raw = KnobVal(Knob::X);
            pkY.captured = false; pkY.bind_raw = KnobVal(Knob::Y);
        }
        pick_prev_down = down;

        // Only run the etch/scope knob pickup when NOT holding DOWN — while DOWN is held
        // the knobs belong to the effect/menu, so they must NOT move etch X/Y or scope.
        if (boot_ctr < 4800) {
            boot_ctr++;
        } else if (!down) {
            run_pick(pkX, KnobVal(Knob::X), GREY_W - 1, desiredX, false);
            run_pick(pkY, KnobVal(Knob::Y), GREY_H - 1, desiredY, true); // Y offset inverted
        }

        // Publish resolved etch mapping. cvgain is 12.12 fixed: Core 1 does
        // pos = offset + (cv * cvgain) >> 12, giving cv·(scale/256)·maxpos/4095.
        // scale: 256 = 1× (full CV swing → ≈±89 cells); up to 1024 = 4× boost.
        shared.etch_offset_x = pkX.stored_offset;
        shared.etch_offset_y = pkY.stored_offset;
        shared.etch_cvgain_x = (int32_t)((int64_t)pkX.stored_scale * (GREY_W - 1) * 4096 / (256 * 4095));
        shared.etch_cvgain_y = (int32_t)((int64_t)pkY.stored_scale * (GREY_H - 1) * 4096 / (256 * 4095));
        shared.scope_audio_scale = pkY.stored_audio;   // Y knob in scope: 0..512 (256=1×)
        shared.scope_baseline    = pkX.stored_baseline; // X knob in scope: baseline row
        shared.spec_gain         = pkY.stored_specgain; // Y knob in spectrum: own gain
        shared.spec_rot          = pkX.stored_specrot;  // X knob in spectrum: own rotate/pos

        MainMode m = main_mode(shared.knob_main);
        LedOn(0, m == MODE_SCOPE);         // scope mode
        LedOn(1, m == MODE_ETCH);          // etch-a-sketch mode (lower third)
        LedOn(5, m == MODE_SPECTRUM);      // spectrum analyser (upper third)
        LedOn(2, shared.menu_active);      // config menu open
        LedOn(3, !shared.alt_mode && shared.sw_position == 1); // fade/persistence (now MIDDLE)
        LedOn(4, shared.sw_position == 2); // switch DOWN held (effect/menu)
    }
};

// Global instance — must not be on stack (ComputerCard requirement, stack = 4 KB)
CathodeRay g_card;

int main() {
    set_sys_clock_khz(144000, true);
    g_card.Run();  // blocking — never returns
}
