#include "slopes.h"

#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "hardware/sync.h"
#include "pico/multicore.h"

// TODO: Port STM32 dependencies to RP2040
// #include "stm32f7xx.h" // STM32-specific, removed

#include "ashapes.h" // Use our local ashapes instead of shapes
// TODO: Port wrDsp dependency to RP2040  
// #include "submodules/wrDsp/wrBlocks.h" // wrDsp dependency, need to stub

// Sample-rate & channel count are centralized in `sample_rate.h` / `slopes.h`
// (SAMPLES_PER_MS, SAMPLES_PER_MS_Q16, SLOPE_CHANNELS). Remove local fallbacks
// to guarantee consistency with the actual ISR rate.

// ========================================================================
// Q11 Fixed-Point LUT System for RP2040 (Cortex-M0+ has no FPU)
// ========================================================================
// Q11 format: signed 12-bit in 16-bit container (-2048 to +2047)
// Directly matches bipolar 12-bit DAC: -2048 = -6V, 0 = 0V, +2047 = +6V
// Performance: ~40-60 cycles vs ~1500+ cycles for powf()
// Memory: 1.5KB vs 3KB for float LUTs (50% savings)
// ========================================================================

#define LUT_SIZE 256
#define Q11_MAX 2047      // 2^11 - 1
#define Q11_MIN -2048     // -2^11
#define Q11_SCALE 2047.0f // Scale factor for [0,1] → Q11

typedef int16_t q11_t;

// Shape LUTs in Q11 format - aligned for fast access on RP2040
static q11_t lut_sin[LUT_SIZE] __attribute__((aligned(4)));
static q11_t lut_exp[LUT_SIZE] __attribute__((aligned(4)));
static q11_t lut_log[LUT_SIZE] __attribute__((aligned(4)));
static bool luts_initialized = false;

// Convert float [0.0, 1.0] to Q11 [0, 2047]
static inline q11_t float_to_q11(float x) {
    if (x >= 1.0f) return Q11_MAX;
    if (x <= 0.0f) return 0;
    return (q11_t)(x * Q11_SCALE + 0.5f);  // +0.5 for rounding
}

// Convert Q11 [0, 2047] to float [0.0, 1.0]
static inline float q11_to_float(q11_t x) {
    return (float)x / Q11_SCALE;
}

// Initialize shape LUTs - called once at startup
static void init_shape_luts(void) {
    if (luts_initialized) return;
    
    printf("Initializing Q11 shape LUTs for RP2040...\n");
    
    for (int i = 0; i < LUT_SIZE; i++) {
        float t = (float)i / (float)(LUT_SIZE - 1);  // 0.0 to 1.0
        
        // Pre-calculate expensive shape functions (done once at startup)
        // These return values in [0, 1] range
        float sin_val = -0.5f * (cosf(M_PI * t) - 1.0f);
        float exp_val = powf(2.0f, 10.0f * (t - 1.0f));
        float log_val = 1.0f - powf(2.0f, -10.0f * t);
        
        // Convert to Q11 format
        lut_sin[i] = float_to_q11(sin_val);
        lut_exp[i] = float_to_q11(exp_val);
        lut_log[i] = float_to_q11(log_val);
    }
    
    luts_initialized = true;
    printf("Q11 Shape LUTs initialized: %d entries, %d bytes total\n", 
           LUT_SIZE, LUT_SIZE * 3 * sizeof(q11_t));
    printf("  Memory savings vs float: %d bytes (%.1f%% reduction)\n",
           LUT_SIZE * 3 * (sizeof(float) - sizeof(q11_t)),
           (1.0f - (float)sizeof(q11_t) / sizeof(float)) * 100.0f);
    printf("  Expected speedup: ~30-50x for exp/log, ~10x for sin\n");
}

// Export LUT table pointers for S_step_one_sample_q16 in ISR
const q11_t* lut_sin_ptr = NULL;
const q11_t* lut_log_ptr = NULL;
const q11_t* lut_exp_ptr = NULL;

// Ultra-fast Q11 LUT lookup with linear interpolation
// Performance: ~40-60 cycles on Cortex-M0+ (vs ~1500+ for powf())
// CRITICAL HOT PATH: Place in RAM for deterministic timing
__attribute__((section(".time_critical.lut_lookup_q11")))
static float lut_lookup_q11(const q11_t* lut, float in) {
    // Clamp input to [0, 1] range
    in = (in < 0.0f) ? 0.0f : ((in > 1.0f) ? 1.0f : in);
    
    // Convert to fixed-point index with 8-bit sub-precision
    // This avoids float multiply in the interpolation hot path
    uint32_t fidx = (uint32_t)(in * (LUT_SIZE - 1) * 256.0f);
    uint32_t idx = fidx >> 8;          // Table index (integer part)
    uint32_t frac = fidx & 0xFF;       // Fractional part (0-255)
    
    // Load two Q11 values from LUT
    int32_t v0 = (int32_t)lut[idx];      // Sign-extend to 32-bit
    int32_t v1 = (int32_t)lut[idx + 1];
    
    // Linear interpolation in fixed-point
    // result = v0 + (frac/256) * (v1 - v0)
    int32_t delta = v1 - v0;
    int32_t result = v0 + ((delta * (int32_t)frac) >> 8);  // Scale back down
    
    // Convert Q11 result back to float [0, 1]
    return (float)result / Q11_SCALE;
}

// Q16-native LUT lookup - eliminates redundant float conversions in hot path
// Directly converts Q16 input to Q16 output via Q11 LUT
// Performance: Same as lut_lookup_q11 but saves 2 float conversions per call
__attribute__((section(".time_critical.lut_lookup_q16")))
static inline q16_t lut_lookup_q16(const q11_t* lut, q16_t in_q16) {
    // Clamp Q16 input to [0, 1] range (0 to Q16_ONE)
    if (in_q16 <= 0) return 0;
    if (in_q16 >= Q16_ONE) in_q16 = Q16_ONE - 1;  // Prevent overflow
    
    // Convert Q16 [0, Q16_ONE] to fixed-point index with 8-bit sub-precision
    // fidx = in_q16 * (LUT_SIZE - 1) / Q16_ONE * 256
    // Simplified: fidx = (in_q16 * (LUT_SIZE - 1) * 256) >> Q16_SHIFT
    uint32_t fidx = ((uint32_t)in_q16 * (LUT_SIZE - 1) * 256) >> Q16_SHIFT;
    uint32_t idx = fidx >> 8;          // Table index (integer part)
    uint32_t frac = fidx & 0xFF;       // Fractional part (0-255)
    
    // Load two Q11 values from LUT
    int32_t v0 = (int32_t)lut[idx];      // Sign-extend to 32-bit
    int32_t v1 = (int32_t)lut[idx + 1];
    
    // Linear interpolation in fixed-point
    int32_t delta = v1 - v0;
    int32_t result_q11 = v0 + ((delta * (int32_t)frac) >> 8);
    
    // Convert Q11 [0, 2047] to Q16 [0, Q16_ONE]
    // Q16 = Q11 * (Q16_ONE / Q11_MAX) = Q11 * (65536 / 2047)
    // Use shift instead: Q16 = Q11 << (Q16_SHIFT - 11) with rounding
    return (q16_t)((result_q11 << (Q16_SHIFT - 11)) + ((result_q11 * 65536) / 2047 - (result_q11 << (Q16_SHIFT - 11))));
}


// TODO: Add missing shape function stubs
// Q16.16 step helpers avoid float conversion for common gate-like shapes
static inline q16_t shapes_step_now_q16(q16_t here_q16)
{
    // Output stays at 0 until we reach the end of the segment
    // Equivalent to: (in >= 1.0f) ? 1.0f : 0.0f;
    return (here_q16 >= Q16_ONE) ? Q16_ONE : 0;
}

static inline q16_t shapes_step_wait_q16(q16_t here_q16)
{
    // Output is 0 while here > 0, and jumps to 1 at the end.
    // Equivalent to: (in <= 0.0f) ? 0.0f : 1.0f;
    return (here_q16 <= 0) ? 0 : Q16_ONE;
}

// 'over', 'under' and 'rebound': crow's easings (crow lib/shapes.c), in Q16.
// 'under' dips ~10% below the start before rising, 'over' overshoots the end
// by ~10% then settles, 'rebound' bounces three times into the end.
#define EASE_BACK_A_Q16  177051  // 2.70158
#define EASE_BACK_B_Q16  111515  // 1.70158

static inline __attribute__((always_inline)) q16_t shapes_ease_in_back_q16(q16_t in)   // in^2 * (2.70158*in - 1.70158)
{
    return Q16_MUL(Q16_MUL(in, in), Q16_MUL(EASE_BACK_A_Q16, in) - EASE_BACK_B_Q16);
}

static inline __attribute__((always_inline)) q16_t shapes_ease_out_back_q16(q16_t in)  // x^2 * (2.70158*x + 1.70158) + 1, x = in - 1
{
    q16_t x = in - Q16_ONE;
    return Q16_MUL(Q16_MUL(x, x), Q16_MUL(EASE_BACK_A_Q16, x) + EASE_BACK_B_Q16) + Q16_ONE;
}

static inline __attribute__((always_inline)) q16_t shapes_ease_out_rebound_q16(q16_t in)  // 7.5625*x^2 + c, in four bounces
{
    q16_t x, c;
    if      (in < 23831) { x = in;          c = 0;     } // in < 1/2.75
    else if (in < 47663) { x = in - 35747; c = 49152; } // < 2/2.75:   x = in - 1.5/2.75,   c = 0.75
    else if (in < 59578) { x = in - 53620; c = 61440; } // < 2.5/2.75: x = in - 2.25/2.75,  c = 0.9375
    else                 { x = in - 62557; c = 64512; } //             x = in - 2.625/2.75, c = 0.984375
    return Q16_MUL(495616, Q16_MUL(x, x)) + c;          // 7.5625
}

// Over, Under and Rebound for a position 0..1 through the segment (clamped)
static inline __attribute__((always_inline)) q16_t shapes_ease_q16(Shape_t shape, q16_t in)
{
    if (in <= 0) return 0;
    if (in >= Q16_ONE) return Q16_ONE;  // land exactly on the target
    switch (shape) {
        case SHAPE_Over:  return shapes_ease_out_back_q16(in);
        case SHAPE_Under: return shapes_ease_in_back_q16(in);
        default:          return shapes_ease_out_rebound_q16(in);
    }
}

// Missing shape functions using local wrBlocks - similar to crow implementation
#include <math.h>
#include "wrblocks.h"

#ifndef M_PI
#define M_PI (3.141592653589793)
#endif

// Single sample shape functions - NOW USING Q11 LUTs for 30-50x speedup!
// HOT PATH: In RAM for consistent timing during high-frequency slope processing
__attribute__((section(".time_critical.shapes_sin")))
static float shapes_sin(float in) {
    if (!luts_initialized) {
        // Fallback to slow math if LUTs not ready (should never happen)
        return -0.5f * (cosf(M_PI * in) - 1.0f);
    }
    return lut_lookup_q11(lut_sin, in);
}

__attribute__((section(".time_critical.shapes_exp")))
static float shapes_exp(float in) {
    if (!luts_initialized) {
        // Fallback to slow math if LUTs not ready (should never happen)
        return powf(2.0f, 10.0f * (in - 1.0f));
    }
    return lut_lookup_q11(lut_exp, in);
}

__attribute__((section(".time_critical.shapes_log")))
static float shapes_log(float in) {
    if (!luts_initialized) {
        // Fallback to slow math if LUTs not ready (should never happen)
        return 1.0f - powf(2.0f, -10.0f * in);
    }
    return lut_lookup_q11(lut_log, in);
}

// Helper function for pow2
static float pow2(float in) { 
    return powf(2.0, in); 
}

// Vector shape functions using wrBlocks - matching crow implementation
static float* shapes_v_sin(float* in, int size) {
    return b_mul(
            b_add(
                b_map(cosf,
                    b_mul(in, M_PI, size)
                     , size)
                 , -1.0
                 , size)
                , -0.5
                , size);
}

static float* shapes_v_exp(float* in, int size) {
    return b_map(pow2,
            b_mul(
                b_add(in, -1.0, size)
                 , 10.0
                 , size)
                , size);
}

static float* shapes_v_log(float* in, int size) {
    return b_sub(
            b_map(pow2,
                b_mul(in, -10.0, size)
                 , size)
                , 1.0
                , size);
}


////////////////////////////////
// global vars

static uint8_t slope_count = 0;
Slope_t* slopes = NULL; // Exported for ll_timers.c conditional processing

// How long after a slope ends its overflow is still carried into the next one
// (see S_toward_q16_apply): ~43ms at 24kHz
#define SLOPE_OVERFLOW_LIMIT_Q16 ((int64_t)1023 << Q16_SHIFT)

// Phase-accumulator oscillator for audio-rate oscillate(). Fixed point: the
// RP2040 has no FPU, and per-sample software float cost ~14% of core 1 per
// voice at 24kHz (four voices overran the ISR budget).
typedef struct {
    bool     active;
    uint32_t phase;      // one cycle = 2^32, wraps
    uint32_t phase_inc;  // freq / sample_rate * 2^32
    q16_t    level_q16;  // volts
    Shape_t  shape;
} oscillator_state_t;

static oscillator_state_t g_oscillators[SLOPE_CHANNELS];

typedef struct {
    q16_t value_q16;
    uint8_t action_due;
    uint16_t seq;         // the slope's tag, for its end event
    Callback_t callback;  // Captured atomically to prevent race conditions
} slope_buffer_entry_t;

static slope_buffer_entry_t slope_output_buffers[SLOPE_CHANNELS][SLOPE_BUFFER_CAPACITY];
volatile uint32_t g_slope_inline_renders = 0;  // samples rendered in the ISR because a buffer ran dry
static uint8_t slope_buffer_head[SLOPE_CHANNELS];
static uint8_t slope_buffer_tail[SLOPE_CHANNELS];
static volatile uint8_t slope_buffer_flush_request[SLOPE_CHANNELS];
static volatile uint32_t slope_fill_request_mask = 0;

static slope_buffer_entry_t S_render_one_sample_q16(int index);
static void S_toward_q16_apply( int        index
                              , q16_t      destination_q16
                              , q16_t      ms_q16
                              , Shape_t    shape
                              , Callback_t cb
                              , uint8_t    use_samples_q16
                              , int64_t    duration_q16_override
                              );

// ========================================================================
// Cross-core slope command queue (Core0 -> Core1)
// Eliminates races on 64-bit slope state (countdown/duration/etc).
// ========================================================================

typedef struct {
    int8_t index;        // slope channel (0-based)
    q16_t dest_q16;
    q16_t ms_q16;        // milliseconds in Q16 (when use_samples_q16==0)
    Shape_t shape;
    Callback_t cb;
    uint8_t coalesce;    // 1 if safe to overwrite older pending cmds for same index
    uint8_t use_samples_q16; // 1 when duration_q16 holds samples (Q16), 0 for ms_q16
    int64_t duration_q16;    // samples in Q16 when use_samples_q16==1
    uint8_t kind;            // SLOPE_CMD_START, or SLOPE_CMD_QUEUE_NEXT (S_queue_next_q16)
    uint8_t lookahead;
    uint16_t seq;
    uint8_t has_jump;        // QUEUE_NEXT only
    uint8_t has_slope;       // QUEUE_NEXT only
    q16_t jump_q16;          // QUEUE_NEXT only
    Callback_t started;      // QUEUE_NEXT only
} slope_cmd_t;
#define SLOPE_CMD_START 0
#define SLOPE_CMD_QUEUE_NEXT 1

// The slope queued to follow each channel's current one (S_queue_next_q16)
typedef struct {
    bool valid;
    bool has_jump;
    bool has_slope;
    bool lookahead;
    uint16_t seq;
    q16_t jump_q16;
    q16_t dest_q16;
    int64_t samples_q16;
    Shape_t shape;
    Callback_t started;
    Callback_t ended;
} slope_next_t;
static slope_next_t slope_next[SLOPE_CHANNELS];
static void slope_queue_next_apply(const slope_cmd_t* cmd);
static void slope_promote_next(int index, int64_t overflow_q16);

#ifdef BB_DEBUG_HARNESS
// Segment timing capture for the test harness: samples rendered per channel
// (less any discarded unplayed), and where channel 0's next 64 slopes begin
// once armed (by zeroing the count)
volatile uint32_t g_slope_render_pos[SLOPE_CHANNELS];
volatile uint32_t g_slope_seg_starts[64];
volatile uint32_t g_slope_seg_start_count;
static inline void slope_debug_seg_start(int index, uint32_t pos) {
    if (index != 0 || g_slope_seg_start_count >= 64) return;  // harness re-arms: count = 0
    g_slope_seg_starts[g_slope_seg_start_count] = pos;
    g_slope_seg_start_count = g_slope_seg_start_count + 1;
}
// Samples held because a lookahead slope ended before core 0 queued the next
// (audible: the output stalls, then jumps to catch up), per channel, and when
// the last 32 such stalls began (time_us_32)
volatile uint32_t g_slope_starved_samples[SLOPE_CHANNELS];
volatile uint32_t g_slope_starve_times[32];
volatile uint32_t g_slope_starve_count;
// One output's samples as the ISR takes them, 512 at a time once armed (by
// zeroing the count): volts in 1/4096ths
#define OUT_CAPTURE_LEN 512
volatile int16_t g_out_capture[OUT_CAPTURE_LEN];
volatile uint32_t g_out_capture_count = OUT_CAPTURE_LEN;
volatile uint32_t g_out_capture_channel = 0;
#endif

#define SLOPE_CMD_QUEUE_SIZE 32
static volatile slope_cmd_t slope_cmd_queue[SLOPE_CMD_QUEUE_SIZE];
static volatile uint32_t slope_cmd_write_idx = 0;
static volatile uint32_t slope_cmd_read_idx = 0;
static volatile uint32_t slope_cmd_drop_count = 0;
static volatile uint32_t slope_cmd_coalesce_count = 0;

static inline bool slope_cmd_enqueue(const slope_cmd_t* cmd) {
    uint32_t irq_state = save_and_disable_interrupts();

    // Optional coalescing: if a prior coalescable command for the same channel
    // is still pending in the queue, overwrite it with the latest request.
    if (cmd->coalesce) {
        bool found = false;
        uint32_t found_pos = 0;
        uint32_t pos = slope_cmd_read_idx;
        while (pos != slope_cmd_write_idx) {
            // Only coalesce against other coalescable commands for same channel
            if (slope_cmd_queue[pos].coalesce && slope_cmd_queue[pos].index == cmd->index) {
                found = true;
                found_pos = pos; // keep the last one we see
            }
            pos = (pos + 1) % SLOPE_CMD_QUEUE_SIZE;
        }
        if (found) {
            slope_cmd_queue[found_pos] = *cmd; // struct copy (volatile)
            slope_cmd_coalesce_count++;
            restore_interrupts(irq_state);
            return true;
        }
    }

    uint32_t next_write = (slope_cmd_write_idx + 1) % SLOPE_CMD_QUEUE_SIZE;
    if (next_write == slope_cmd_read_idx) {
        restore_interrupts(irq_state);
        slope_cmd_drop_count++;
        return false; // queue full
    }
    slope_cmd_queue[slope_cmd_write_idx] = *cmd; // struct copy (volatile)
    slope_cmd_write_idx = next_write;
    restore_interrupts(irq_state);
    return true;
}

uint32_t S_get_cmd_drop_count(void) {
    return slope_cmd_drop_count;
}

static inline bool slope_cmd_dequeue(slope_cmd_t* out) {
    uint32_t irq_state = save_and_disable_interrupts();
    if (slope_cmd_read_idx == slope_cmd_write_idx) {
        restore_interrupts(irq_state);
        return false; // empty
    }
    uint32_t read = slope_cmd_read_idx;
    *out = slope_cmd_queue[read];
    slope_cmd_read_idx = (read + 1) % SLOPE_CMD_QUEUE_SIZE;
    restore_interrupts(irq_state);
    return true;
}

// Process queued commands (Core1 only). Limit per call to bound latency.
void __not_in_flash_func(S_process_pending_commands)(void) {
    const int kMaxPerCall = 4;
    slope_cmd_t cmd;
    int processed = 0;
    while (processed < kMaxPerCall && slope_cmd_dequeue(&cmd)) {
        // Interrupts off: the ISR reads (and, if its buffer runs dry, renders)
        // this slope's state
        uint32_t irq = save_and_disable_interrupts();
        if (cmd.kind == SLOPE_CMD_QUEUE_NEXT) {
            slope_queue_next_apply(&cmd);
        } else {
            S_toward_q16_apply(cmd.index, cmd.dest_q16, cmd.ms_q16, cmd.shape, cmd.cb,
                       cmd.use_samples_q16, cmd.duration_q16);
            slopes[cmd.index].seq = cmd.seq;
            slopes[cmd.index].lookahead = cmd.lookahead;
        }
        restore_interrupts(irq);
        processed++;
    }
}

static inline int slope_buffer_fill_level(int index);
static inline void slope_buffer_clear_channel(int index) {
#ifdef BB_DEBUG_HARNESS
    g_slope_render_pos[index] = g_slope_render_pos[index] - slope_buffer_fill_level(index);  // never played
#endif
    slope_buffer_head[index] = 0;
    slope_buffer_tail[index] = 0;
}

static inline uint8_t slope_buffer_next(uint8_t value) {
    return (uint8_t)((value + 1) % SLOPE_BUFFER_CAPACITY);
}

static inline int slope_buffer_fill_level(int index) {
    uint8_t head = slope_buffer_head[index];
    uint8_t tail = slope_buffer_tail[index];
    if (head >= tail) {
        return head - tail;
    }
    return SLOPE_BUFFER_CAPACITY - (tail - head);
}

static inline int slope_buffer_space_remaining(int index) {
    // Leave one slot empty to distinguish full vs empty
    return (SLOPE_BUFFER_CAPACITY - 1) - slope_buffer_fill_level(index);
}

static inline bool slope_buffer_push(int index, slope_buffer_entry_t entry) {
    uint8_t head = slope_buffer_head[index];
    uint8_t next_head = slope_buffer_next(head);
    if (next_head == slope_buffer_tail[index]) {
        return false; // buffer full
    }
    slope_output_buffers[index][head] = entry;
    __dmb();
    slope_buffer_head[index] = next_head;
    return true;
}

static inline bool slope_buffer_pop(int index, slope_buffer_entry_t* out) {
    uint8_t tail = slope_buffer_tail[index];
    if (tail == slope_buffer_head[index]) {
        return false;
    }
    *out = slope_output_buffers[index][tail];
    __dmb();
    slope_buffer_tail[index] = slope_buffer_next(tail);
    return true;
}

void S_slope_buffer_reset(void) {
    uint32_t irq_state = save_and_disable_interrupts();
    slope_fill_request_mask = 0;
    restore_interrupts(irq_state);
    for (int ch = 0; ch < SLOPE_CHANNELS; ch++) {
        slope_buffer_clear_channel(ch);
        slope_buffer_flush_request[ch] = 0;
    }
}

bool __not_in_flash_func(S_slope_buffer_needs_fill)(int index) {
    if (index < 0 || index >= SLOPE_CHANNELS) { return false; }
    return slope_buffer_fill_level(index) <= SLOPE_BUFFER_LOW_WATER;
}

__attribute__((section(".time_critical.S_slope_buffer_fill_block")))
void S_slope_buffer_fill_block(int index, int samples) {
    if (index < 0 || index >= SLOPE_CHANNELS || samples <= 0 || slopes == NULL) {
        return;
    }
    if (slope_buffer_flush_request[index]) {
        slope_buffer_clear_channel(index);
        slope_buffer_flush_request[index] = 0;
    }
    Slope_t* self = &slopes[index];
    while (samples-- > 0 && slope_buffer_space_remaining(index) > 0) {
        // A lookahead slope that has ended: wait for core 0 to queue the next
        // one rather than render ahead holding (the buffered samples are the
        // time core 0 has; if they run out, the ISR renders holds meanwhile)
        if (self->lookahead && self->countdown_q16 <= 0 && !slope_next[index].valid
            && !g_oscillators[index].active) {
            break;
        }
        // Interrupts off per sample: the ISR renders this slope itself when
        // the buffer runs dry, and preempting a render half-way corrupted it
        // (one release ended a sample short, stuck at 2V/72)
        uint32_t irq = save_and_disable_interrupts();
        slope_buffer_entry_t entry = S_render_one_sample_q16(index);
        bool pushed = slope_buffer_push(index, entry);
        restore_interrupts(irq);
        if (!pushed) {
            break;
        }
    }
}

void __not_in_flash_func(S_request_slope_buffer_fill)(int index) {
    if (index < 0 || index >= SLOPE_CHANNELS) {
        return;
    }
    uint32_t irq_state = save_and_disable_interrupts();
    slope_fill_request_mask |= (1u << index);
    restore_interrupts(irq_state);
}

__attribute__((section(".time_critical.S_consume_buffered_sample_q16")))
q16_t S_consume_buffered_sample_q16(int index) {
    if (index < 0 || index >= SLOPE_CHANNELS) { return 0; }
    if (slope_buffer_flush_request[index]) {
        slope_buffer_clear_channel(index);
        slope_buffer_flush_request[index] = 0;
    }
    slope_buffer_entry_t entry;
    if (!slope_buffer_pop(index, &entry)) {
        // Buffer ran dry: render this sample here in the ISR. The output stays
        // correct; it only costs ISR time. Counted, as a sign the background
        // rendering on core 1 isn't keeping up.
        entry = S_render_one_sample_q16(index);
        g_slope_inline_renders++;
    }
#ifdef BB_DEBUG_HARNESS
    if ((uint32_t)index == g_out_capture_channel && g_out_capture_count < OUT_CAPTURE_LEN) {
        int32_t v = entry.value_q16 >> 4;
        g_out_capture[g_out_capture_count] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
        g_out_capture_count = g_out_capture_count + 1;
    }
#endif
    if (entry.action_due && entry.callback != NULL) {
        extern void queue_slope_action_callback(int channel, Callback_t callback, uint16_t seq);
        queue_slope_action_callback(index, entry.callback, entry.seq);
    }
    return entry.value_q16;
}

// Returns true if it rendered samples into a buffer (for core 1 load profiling)
bool __not_in_flash_func(S_slope_buffer_background_service)(void) {
    static int service_index = 0;
    if (!slopes) {
        return false;
    }

    // Apply any queued cross-core slope commands first
    S_process_pending_commands();

    // Service explicit refill requests first (set by ProcessSample when buffers dip)
    int requested_channel = -1;
    {
        uint32_t irq_state = save_and_disable_interrupts();
        uint32_t pending = slope_fill_request_mask;
        if (pending) {
            requested_channel = __builtin_ctz(pending);
            slope_fill_request_mask &= ~(1u << requested_channel);
        }
        restore_interrupts(irq_state);
    }

    if (requested_channel >= 0) {
        if (S_slope_buffer_needs_fill(requested_channel)) {
            S_slope_buffer_fill_block(requested_channel, SLOPE_RENDER_CHUNK);
            return true;
        }
        return false;
    }

    for (int i = 0; i < SLOPE_CHANNELS; i++) {
        int channel = (service_index + i) % SLOPE_CHANNELS;
        if (S_slope_buffer_needs_fill(channel)) {
            S_slope_buffer_fill_block(channel, SLOPE_RENDER_CHUNK);
            service_index = (channel + 1) % SLOPE_CHANNELS;
            return true;
        }
    }
    service_index = (service_index + 1) % SLOPE_CHANNELS;
    return false;
}

// Compute normalized progress (0-1 in Q16) from elapsed samples
static inline q16_t slope_progress_from_elapsed(const Slope_t* self)
{
    if( self->duration_q16 <= 0 ){
        return (self->elapsed_q16 >= 0) ? Q16_ONE : 0;
    }

    int64_t elapsed = self->elapsed_q16;
    if( elapsed <= 0 ){
        return 0;
    }
    if( elapsed >= self->duration_q16 ){
        return Q16_ONE;
    }
    return (q16_t)(((elapsed << Q16_SHIFT)) / self->duration_q16);
}

// Advance the slope by 'samples_q16' (Q16 samples) and refresh cached progress
static inline void slope_advance(Slope_t* self, int64_t samples_q16)
{
    if( samples_q16 <= 0 ){
        return;
    }

    if( self->duration_q16 <= 0 ){
        // Zero-duration slews still use countdown for callback scheduling
        self->countdown_q16 -= samples_q16;
        if( self->countdown_q16 < 0 ){
            self->countdown_q16 = 0;
        }
        return;
    }

    self->elapsed_q16 += samples_q16;
    if( self->elapsed_q16 > self->duration_q16 ){
        self->elapsed_q16 = self->duration_q16;
    }

    self->countdown_q16 -= samples_q16;
    if( self->countdown_q16 < 0 ){
        self->countdown_q16 = 0;
    }

    self->here_q16 = slope_progress_from_elapsed(self);
}


////////////////////////////////
// private declarations
// Forward declarations for RAM-placed functions
static float* step_v( Slope_t* self, float* out, int size );
static float* static_v( Slope_t* self, float* out, int size );
static float* motion_v( Slope_t* self, float* out, int size );
static float* breakpoint_v( Slope_t* self, float* out, int size );
static float* shaper_v( Slope_t* self, float* out, int size );
// shaper_v now applies the shape directly; no separate shaper() helper.

////////////////////////////////
// public definitions

void S_init( int channels )
{
    // Initialize Q11 LUTs first for optimal performance
    init_shape_luts();
    
    // Export LUT pointers for S_step_one_sample_q16 in Core 1 ISR
    lut_sin_ptr = lut_sin;
    lut_log_ptr = lut_log;
    lut_exp_ptr = lut_exp;
    
    slope_count = channels;
    slopes = malloc( sizeof ( Slope_t ) * channels );
    if( !slopes ){ printf("slopes malloc failed\n"); return; }
    for( int j=0; j<SLOPE_CHANNELS; j++ ){
        slopes[j].index  = j;
        slopes[j].dest_q16   = 0;
        slopes[j].last_q16   = 0;
        slopes[j].scale_q16  = 0;
        slopes[j].shaped_q16 = 0;
        slopes[j].shape  = SHAPE_Linear;
        slopes[j].action = NULL;
        slopes[j].action_fired = false;
        slopes[j].seq = 0;
        slopes[j].lookahead = false;
        slope_next[j].valid = false;

        slopes[j].here_q16      = 0;
        slopes[j].countdown_q16 = -(int64_t)Q16_ONE;  // -1.0 in Q16
        slopes[j].duration_q16  = 0;
        slopes[j].elapsed_q16   = 0;

        // reset oscillator state for this channel
        g_oscillators[j].active = false;
        g_oscillators[j].phase = 0;
        g_oscillators[j].phase_inc = 0;
        g_oscillators[j].level_q16 = 0;
        g_oscillators[j].shape = SHAPE_Sine;
    }
    S_slope_buffer_reset();
}

bool S_set_oscillator(int index, float freq_hz, float level_volts, Shape_t shape)
{
    if (index < 0 || index >= SLOPE_CHANNELS) { return false; }
    if (freq_hz <= 0.0f) { return false; }

    if (freq_hz > (float)SAMPLE_RATE / 2.0f) { freq_hz = (float)SAMPLE_RATE / 2.0f; } // Nyquist

    oscillator_state_t* osc = &g_oscillators[index];
    bool was_active = osc->active;
    // keep phase continuity if already active; else start at 0
    if (!was_active) { osc->phase = 0; }
    osc->phase_inc = (uint32_t)((double)freq_hz / (double)SAMPLE_RATE * 4294967296.0);
    osc->level_q16 = FLOAT_TO_Q16(level_volts);
    osc->shape = shape;
    osc->active = true;
    return true;
}

void __not_in_flash_func(S_clear_oscillator)(int index)
{
    if (index < 0 || index >= SLOPE_CHANNELS) { return; }
    g_oscillators[index].active = false;
}

void S_reset(void)
{
    if( !slopes ){
        return;
    }
    for( int j = 0; j < slope_count; j++ ){
        slopes[j].dest_q16 = 0;
        slopes[j].last_q16 = 0;
        slopes[j].scale_q16 = 0;
        slopes[j].shaped_q16 = 0;
        slopes[j].shape = SHAPE_Linear;
        slopes[j].action = NULL;
        slopes[j].action_fired = false;
        slopes[j].seq = 0;
        slopes[j].lookahead = false;
        slope_next[j].valid = false;
        slopes[j].here_q16 = 0;
        slopes[j].countdown_q16 = -(int64_t)Q16_ONE;  // -1.0 in Q16
        slopes[j].duration_q16 = 0;
        slopes[j].elapsed_q16 = 0;

        g_oscillators[j].active = false;
        g_oscillators[j].phase = 0;
        g_oscillators[j].phase_inc = 0;
        g_oscillators[j].level_q16 = 0;
        g_oscillators[j].shape = SHAPE_Sine;
    }

    for (int j = 0; j < slope_count; j++) {
        slope_buffer_flush_request[j] = 1;
    }
}

Shape_t S_str_to_shape( const char* s )
{
    char ps = (char)*s;
    if( ps < 0x61 ){ ps += 0x20; } // convert upper to lowercase
    switch( ps ){ // match on first char unless necessary
        case 's': return SHAPE_Sine;
        case 'e': return SHAPE_Expo;
        case 'n': return SHAPE_Now;
        case 'w': return SHAPE_Wait;
        case 'o': return SHAPE_Over;
        case 'u': return SHAPE_Under;
        case 'r': return SHAPE_Rebound;
        case 'l': if( s[1]=='o' ){ return SHAPE_Log; } // else flows through
        default: return SHAPE_Linear; // unmatched
    }
}

// Q16 API - returns fixed-point voltage
q16_t S_get_state_q16( int index )
{
    if( index < 0 || index >= SLOPE_CHANNELS ){ return 0; }
    Slope_t* self = &slopes[index]; // safe pointer
    return self->shaped_q16;
}

// Float API - wraps Q16 for backward compatibility
float S_get_state( int index )
{
    if( index < 0 || index >= SLOPE_CHANNELS ){ return 0.0; }
    return Q16_TO_FLOAT(slopes[index].shaped_q16);
}

__attribute__((section(".time_critical.S_render_one_sample_q16")))
static slope_buffer_entry_t S_render_one_sample_q16(int index)
{
    slope_buffer_entry_t entry = { .value_q16 = 0, .action_due = 0, .seq = 0, .callback = NULL };
    if( index < 0 || index >= SLOPE_CHANNELS || slopes == NULL ){ return entry; }
    Slope_t* self = &slopes[index];
#ifdef BB_DEBUG_HARNESS
    g_slope_render_pos[index] = g_slope_render_pos[index] + 1;
#endif
    
    // Oscillator fast-path (audio-friendly fractional phase accumulator)
    oscillator_state_t* osc = &g_oscillators[index];
    if (osc->active) {
        uint32_t ph = osc->phase;
        osc->phase = ph + osc->phase_inc;  // wraps at one cycle
        // split phase into rising/falling halves to match ASL lfo semantics:
        // each half sweeps 0..1 (Q16) through the shape
        bool rising = (ph < 0x80000000u);
        q16_t half_q16 = (q16_t)((ph << 1) >> 16);
        q16_t shaped_q16;
        switch (osc->shape) {
            case SHAPE_Sine: shaped_q16 = lut_lookup_q16(lut_sin_ptr ? lut_sin_ptr : lut_sin, half_q16); break;
            case SHAPE_Log:  shaped_q16 = lut_lookup_q16(lut_log_ptr ? lut_log_ptr : lut_log, half_q16); break;
            case SHAPE_Expo: shaped_q16 = lut_lookup_q16(lut_exp_ptr ? lut_exp_ptr : lut_exp, half_q16); break;
            case SHAPE_Over:
            case SHAPE_Under:
            case SHAPE_Rebound: shaped_q16 = shapes_ease_q16(osc->shape, half_q16); break;
            case SHAPE_Linear:
            default:         shaped_q16 = half_q16; break;
        }
        // rising: -level -> +level; falling: +level -> -level
        q16_t swing_q16 = Q16_MUL(shaped_q16, osc->level_q16 * 2);
        q16_t sample_q16 = rising ? (swing_q16 - osc->level_q16) : (osc->level_q16 - swing_q16);
        extern q16_t AShaper_quantize_single_q16(int index, q16_t voltage_q16);
        entry.value_q16 = AShaper_quantize_single_q16(index, sample_q16);
        return entry;
    }

    const int64_t one_sample_q16 = (int64_t)Q16_ONE;

    // Finished, and the next slope is queued: start it on this sample, as far
    // in as the current one overshot (plus any samples held meanwhile)
    if( self->countdown_q16 <= 0 && slope_next[index].valid ) {
        int64_t overflow_q16 = -self->countdown_q16;
        if( overflow_q16 >= SLOPE_OVERFLOW_LIMIT_Q16 ){ overflow_q16 = 0; }
#ifdef BB_DEBUG_HARNESS
        slope_debug_seg_start(index, g_slope_render_pos[index] - 1);
#endif
        slope_promote_next(index, overflow_q16);
    }

    // Finished: hold at the destination, but keep counting the samples since
    // it ended (negative countdown, to a limit), so a slope started by its
    // action can begin where it would have been with no gap
    if( self->countdown_q16 <= 0 ) {
#ifdef BB_DEBUG_HARNESS
        if( self->lookahead ){
            if( self->countdown_q16 > -one_sample_q16 ){  // the first held sample: a new stall
                g_slope_starve_times[g_slope_starve_count % 32] = time_us_32() | (uint32_t)index;
                g_slope_starve_count = g_slope_starve_count + 1;
            }
            g_slope_starved_samples[index] = g_slope_starved_samples[index] + 1;
        }
#endif
        if( self->countdown_q16 > -SLOPE_OVERFLOW_LIMIT_Q16 ) {
            self->countdown_q16 -= one_sample_q16;
        }
        entry.value_q16 = self->shaped_q16;
        if( self->action != NULL ) {  // finished without rendering (see S_toward_q16_apply)
            entry.callback = self->action;
            entry.seq = self->seq;
            self->action = NULL;
            entry.action_due = 1;
            self->action_fired = true;
        }
        return entry;
    }
    
    // countdown may go a fraction of a sample below 0 here: that's how far
    // past the end this sample is, also carried into the next slope
    self->countdown_q16 -= one_sample_q16;
    self->elapsed_q16   += one_sample_q16;

    // Ended part-way through this sample, with the next lookahead slope
    // queued: this sample is already that far into it. (Rendering the end
    // value here and starting the next on the following sample put every
    // audio-rate corner a sample late: one sample stuck on the peak, then a
    // double step.) Slopes with an end action still end on their own sample.
    if( self->countdown_q16 < 0 && self->lookahead && self->action == NULL
        && slope_next[index].valid ) {
#ifdef BB_DEBUG_HARNESS
        slope_debug_seg_start(index, g_slope_render_pos[index] - 1);
#endif
        slope_promote_next(index, -self->countdown_q16);
    }
    
    if( self->elapsed_q16 > self->duration_q16 ) {
        self->elapsed_q16 = self->duration_q16;
    }
    
    q16_t here_q16;
    if( self->duration_q16 <= 0 ) {
        here_q16 = Q16_ONE;
    } else if( self->elapsed_q16 <= 0 ) {
        here_q16 = 0;
    } else if( self->elapsed_q16 >= self->duration_q16 ) {
        here_q16 = Q16_ONE;
    } else {
        here_q16 = (q16_t)(((self->elapsed_q16 << Q16_SHIFT)) / self->duration_q16);
    }
    self->here_q16 = here_q16;
    
    q16_t shaped_q16;
    extern const q11_t* lut_sin_ptr;
    extern const q11_t* lut_log_ptr;
    extern const q11_t* lut_exp_ptr;
    extern q16_t lut_lookup_q16(const q11_t* lut, q16_t in_q16);
    
    switch( self->shape ) {
        case SHAPE_Sine:
            shaped_q16 = lut_lookup_q16(lut_sin_ptr, here_q16);
            break;
        case SHAPE_Log:
            shaped_q16 = lut_lookup_q16(lut_log_ptr, here_q16);
            break;
        case SHAPE_Expo:
            shaped_q16 = lut_lookup_q16(lut_exp_ptr, here_q16);
            break;
        case SHAPE_Now:
            shaped_q16 = (here_q16 >= Q16_ONE) ? Q16_ONE : 0;
            break;
        case SHAPE_Wait:
            shaped_q16 = (here_q16 <= 0) ? 0 : Q16_ONE;
            break;
        case SHAPE_Over:
        case SHAPE_Under:
        case SHAPE_Rebound:
            shaped_q16 = shapes_ease_q16(self->shape, here_q16);
            break;
        case SHAPE_Linear:
        default:
            shaped_q16 = here_q16;
            break;
    }
    
    q16_t voltage_q16 = Q16_MUL(shaped_q16, self->scale_q16) + self->last_q16;
    self->shaped_q16 = voltage_q16;
    
    extern q16_t AShaper_quantize_single_q16(int index, q16_t voltage_q16);
    q16_t quantized_q16 = AShaper_quantize_single_q16(index, voltage_q16);
    entry.value_q16 = quantized_q16;
    
    // CRITICAL: Atomically capture and clear callback when action becomes due
    // This prevents race condition where new S_toward could overwrite action
    // between checking and queuing
    if( self->countdown_q16 <= 0 && self->action != NULL ) {
        entry.callback = self->action;
        entry.seq = self->seq;
        self->action = NULL;  // Clear immediately after capturing
        entry.action_due = 1;
        self->action_fired = true;
    }
    
    return entry;
}

// Single-sample slope processing for Core 1 ISR (immediate mode)
__attribute__((section(".time_critical.S_step_one_sample_q16")))
q16_t S_step_one_sample_q16(int index)
{
    if (index < 0 || index >= SLOPE_CHANNELS) { return 0; }
    slope_buffer_entry_t entry = S_render_one_sample_q16(index);
    extern void hardware_output_set_voltage_q16(int channel, q16_t voltage_q16);
    hardware_output_set_voltage_q16(index + 1, entry.value_q16);
    if (entry.action_due && entry.callback != NULL) {
        extern void queue_slope_action_callback(int channel, Callback_t callback, uint16_t seq);
        queue_slope_action_callback(index, entry.callback, entry.seq);
    }
    return entry.value_q16;
}

// Start the queued next slope (core 1): jump first if asked, then the slope,
// `overflow_q16` samples in (or just hold, if it has no slope). Tells core 0
// it started.
static void __not_in_flash_func(slope_promote_next)(int index, int64_t overflow_q16)
{
    Slope_t* self = &slopes[index];
    slope_next_t* n = &slope_next[index];
    n->valid = false;

    if( n->has_jump ){
        extern q16_t AShaper_quantize_single_q16(int index, q16_t voltage_q16);
        self->shaped_q16 = AShaper_quantize_single_q16(index, n->jump_q16);
        self->last_q16 = self->shaped_q16;
    }
    self->seq = n->seq;
    self->lookahead = n->lookahead;
    self->action_fired = false;

    if( n->has_slope ){
        self->dest_q16      = n->dest_q16;
        self->shape         = n->shape;
        self->last_q16      = self->shaped_q16;
        self->scale_q16     = self->dest_q16 - self->last_q16;
        self->duration_q16  = n->samples_q16;
        self->countdown_q16 = n->samples_q16;
        self->elapsed_q16   = 0;
        self->here_q16      = 0;
        self->action        = n->lookahead ? NULL : n->ended;
        if( overflow_q16 >= n->samples_q16 ){  // the whole slope already elapsed (see S_toward_q16_apply)
            self->elapsed_q16   = n->samples_q16;
            self->here_q16      = Q16_ONE;
            self->shaped_q16    = self->dest_q16;
            self->countdown_q16 = n->samples_q16 - overflow_q16;
        } else if( overflow_q16 > 0 ){
            slope_advance(self, overflow_q16);
        }
    } else {  // nothing to slope to: hold here (the sequence waits, or is done)
        self->dest_q16      = self->shaped_q16;
        self->scale_q16     = 0;
        self->duration_q16  = 0;
        self->elapsed_q16   = 0;
        self->here_q16      = Q16_ONE;
        self->countdown_q16 = -overflow_q16;
        self->action        = NULL;
    }

    if( n->started != NULL ){
        extern void queue_slope_action_callback(int channel, Callback_t callback, uint16_t seq);
        queue_slope_action_callback(index, n->started, n->seq);
    }
}

// A slope queued by core 0 (core 1, interrupts off). Render starts it on the
// sample after the current one ends: it pauses there for this (see
// S_slope_buffer_fill_block), and samples the ISR had to render meanwhile
// were played, so the overflow counts them. Only if the current slope isn't
// expecting one (not lookahead), start it now, after the samples played.
static void __not_in_flash_func(slope_queue_next_apply)(const slope_cmd_t* cmd)
{
    int index = cmd->index;
    if( index < 0 || index >= SLOPE_CHANNELS ){ return; }
    Slope_t* self = &slopes[index];
    slope_next_t* n = &slope_next[index];
    n->has_jump    = cmd->has_jump;
    n->jump_q16    = cmd->jump_q16;
    n->has_slope   = cmd->has_slope;
    n->dest_q16    = cmd->dest_q16;
    n->samples_q16 = (cmd->duration_q16 > 0) ? cmd->duration_q16 : (int64_t)Q16_ONE;
    n->shape       = cmd->shape;
    n->seq         = cmd->seq;
    n->lookahead   = cmd->lookahead;
    n->started     = cmd->started;
    n->ended       = cmd->cb;
    n->valid       = true;

    if( !self->lookahead && self->countdown_q16 <= 0 ){
        int64_t played_q16 = -self->countdown_q16;
        if( played_q16 >= SLOPE_OVERFLOW_LIMIT_Q16 ){
            played_q16 = 0;  // ended long ago: start afresh
        } else {
            played_q16 -= (int64_t)slope_buffer_fill_level(index) << Q16_SHIFT;  // rendered, not played
            if( played_q16 < 0 ){ played_q16 = 0; }
        }
        slope_buffer_clear_channel(index);
        slope_buffer_flush_request[index] = 0;
#ifdef BB_DEBUG_HARNESS
        slope_debug_seg_start(index, g_slope_render_pos[index]);
#endif
        slope_promote_next(index, played_q16);
    }
}

// Q16.16 Fixed-Point Slope Engine - Core Implementation
// All arithmetic in integer math for 5-6x performance improvement on RP2040
// Internal apply (must run on Core1 with interrupts disabled to avoid preemption by ISR)
static void __not_in_flash_func(S_toward_q16_apply)( int        index
                              , q16_t      destination_q16
                              , q16_t      ms_q16
                              , Shape_t    shape
                              , Callback_t cb
                              , uint8_t    use_samples_q16
                              , int64_t    duration_q16_override
                              )
{
    if( index < 0 || index >= SLOPE_CHANNELS ){ return; }
    // If an oscillator is active on this channel, disable it when a slope is requested
    S_clear_oscillator(index);
    Slope_t* self = &slopes[index]; // safe pointer

    // update destination and shape
    self->dest_q16  = destination_q16;
    self->shape     = shape;

    // A slope started directly replaces any queued to follow (callers that
    // tag it set seq/lookahead after this)
    slope_next[index].valid = false;
    self->seq = 0;
    self->lookahead = false;
#ifdef BB_DEBUG_HARNESS
    slope_debug_seg_start(index, g_slope_render_pos[index] - slope_buffer_fill_level(index));
#endif
    
    slope_buffer_flush_request[index] = 1; // ensure buffered samples are discarded on Core 1

    // direct update & callback if the duration is 0 (ie instant)
    if( use_samples_q16 ? (duration_q16_override <= 0) : (ms_q16 <= 0) ){
        // Immediate transition: quantize once and store quantized state
        // so subsequent reads (LL_get_state) and future slopes start from
        // the quantized voltage.
        extern q16_t AShaper_quantize_single_q16(int index, q16_t voltage_q16);
        q16_t quantized_q16 = AShaper_quantize_single_q16(index, self->dest_q16);

        self->last_q16      = quantized_q16;
        self->shaped_q16    = quantized_q16;
        self->scale_q16     = 0;
        self->here_q16      = Q16_ONE; // 1.0 in Q16 - end of range
        self->duration_q16  = 0;
        self->elapsed_q16   = 0;
        
        // Cancel any pending slope
        if(self->countdown_q16 > 0){
            self->countdown_q16 = 0;
        }
        
        // Set action for instant transitions
        self->action = cb;
        self->action_fired = false;
        
        // Immediate hardware update for zero-time (instant) transitions
        // We already quantized above; just emit the quantized value
        extern void hardware_output_set_voltage_q16(int channel, q16_t voltage_q16);
        hardware_output_set_voltage_q16(index+1, quantized_q16);  // Direct Q16, no conversion!
        
        // Schedule callback for instant transitions if callback provided
        if(self->action){
            self->countdown_q16 = Q16_ONE; // Fire callback after 1 sample
        }
    } else {
        // save current output level as new starting point
        self->last_q16  = self->shaped_q16;
        self->scale_q16 = self->dest_q16 - self->last_q16;
        // Overflow: when this slope was started by the previous one's action
        // (an ASL sequence), start it as far in as the time since the previous
        // one ended: the fraction of a sample it overshot plus the wait for
        // core 0 to run the action. Without this each segment ran ~1 sample
        // long, and audio-rate ASL loops played flat (1kHz at ~907Hz).
        // Samples rendered past the end but still buffered are discarded
        // below, unplayed, so they don't count.
        int64_t overflow_q16 = 0;
        if( self->action_fired
            && self->countdown_q16 < 0 && self->countdown_q16 > -SLOPE_OVERFLOW_LIMIT_Q16 ){
            overflow_q16 = -self->countdown_q16;
            if( get_core_num() == 1 ){  // with interrupts off: the ISR can't consume meanwhile
                overflow_q16 -= (int64_t)slope_buffer_fill_level(index) << Q16_SHIFT;
                slope_buffer_clear_channel(index);
                slope_buffer_flush_request[index] = 0;
            }
            if( overflow_q16 < 0 ){ overflow_q16 = 0; }
        }
        self->action_fired = false;

        // Convert ms to samples: ms * SAMPLES_PER_MS
        // If caller provided samples directly, use them to avoid wide multiply
        int64_t samples_q16 = use_samples_q16 ? duration_q16_override
                             : Q16_MUL_WIDE(ms_q16, SAMPLES_PER_MS_Q16);

        // Preserve fractional-sample durations for true timing (no rounding)
        if( samples_q16 <= 0 ){
            samples_q16 = (int64_t)Q16_ONE; // minimum of 1 sample
        }

        self->duration_q16  = samples_q16;
        self->countdown_q16 = samples_q16;
        self->elapsed_q16   = 0;
        self->here_q16      = 0; // start of slope
        
        // NOW it's safe to update the action pointer for the new slope
        self->action = cb;

        if( overflow_q16 >= samples_q16 ){
            // The whole slope elapsed while its start waited on core 0 (short
            // audio-rate segments): go straight to the destination, and keep
            // the rest of the overflow (negative countdown) for the next one.
            // The action fires on the next sample.
            self->elapsed_q16   = samples_q16;
            self->here_q16      = Q16_ONE;
            self->shaped_q16    = self->dest_q16;
            self->countdown_q16 = samples_q16 - overflow_q16;
        } else if( overflow_q16 > 0 ){
            slope_advance(self, overflow_q16);
        }
    }
}

// Public API: route to Core1 if invoked from Core0 to avoid cross-core races on 64-bit state
void S_toward_q16( int        index
                 , q16_t      destination_q16
                 , q16_t      ms_q16
                 , Shape_t    shape
                 , Callback_t cb
                 )
{
    // If slopes not initialized, ignore
    if (!slopes) { return; }

    // Core1 (audio engine): apply immediately with interrupts disabled to avoid ISR preemption
    if (get_core_num() == 1) {
        uint32_t irq = save_and_disable_interrupts();
        S_toward_q16_apply(index, destination_q16, ms_q16, shape, cb, 0, 0);
        restore_interrupts(irq);
        return;
    }

    // Core0 (Lua/control): enqueue command for Core1 to apply safely
    slope_cmd_t cmd = { .index = (int8_t)index,
                        .dest_q16 = destination_q16,
                        .ms_q16 = ms_q16,
                        .shape = shape,
                        .cb = cb,
                        .coalesce = 0,
                        .use_samples_q16 = 0,
                        .duration_q16 = 0 };
    if (!slope_cmd_enqueue(&cmd)) {
        // As a fallback (should be rare), apply locally with interrupts disabled
        // This may introduce a tiny race but avoids dropping envelopes completely
        uint32_t irq = save_and_disable_interrupts();
        S_toward_q16_apply(index, destination_q16, ms_q16, shape, cb, 0, 0);
        restore_interrupts(irq);
    }
}

void S_toward_q16_coalescable( int        index
                            , q16_t      destination_q16
                            , q16_t      ms_q16
                            , Shape_t    shape
                            , Callback_t cb
                            )
{
    // If slopes not initialized, ignore
    if (!slopes) { return; }

    // Core1 (audio engine): apply immediately with interrupts disabled
    if (get_core_num() == 1) {
        uint32_t irq = save_and_disable_interrupts();
        S_toward_q16_apply(index, destination_q16, ms_q16, shape, cb, 0, 0);
        restore_interrupts(irq);
        return;
    }

    // Core0 (Lua/control): enqueue with coalescing enabled
    slope_cmd_t cmd = { .index = (int8_t)index,
                        .dest_q16 = destination_q16,
                        .ms_q16 = ms_q16,
                        .shape = shape,
                        .cb = cb,
                        .coalesce = 1,
                        .use_samples_q16 = 0,
                        .duration_q16 = 0 };
    if (!slope_cmd_enqueue(&cmd)) {
        uint32_t irq = save_and_disable_interrupts();
        S_toward_q16_apply(index, destination_q16, ms_q16, shape, cb, 0, 0);
        restore_interrupts(irq);
    }
}

// Float API wrapper - converts float to Q16, calls Q16 implementation
void S_toward( int        index
             , float      destination
             , float      ms
             , Shape_t    shape
             , Callback_t cb
             )
{
    S_toward_q16(index, 
                 FLOAT_TO_Q16(destination), 
                 FLOAT_TO_Q16(ms), 
                 shape, 
                 cb);
}

// Samples-based API: duration provided in samples (integer), avoids ms→samples conversion
void S_toward_samples_q16( int        index
                         , q16_t      destination_q16
                         , int64_t    samples_q16
                         , Shape_t    shape
                         , Callback_t cb
                         )
{
    // If slopes not initialized, ignore
    if (!slopes) { return; }

    // Core1 fast path
    if (get_core_num() == 1) {
        uint32_t irq = save_and_disable_interrupts();
        S_toward_q16_apply(index, destination_q16, /*ms_q16*/0, shape, cb, 1, samples_q16);
        restore_interrupts(irq);
        return;
    }

    // Core0 enqueue
    slope_cmd_t cmd = { .index = (int8_t)index,
                        .dest_q16 = destination_q16,
                        .ms_q16 = 0,
                        .shape = shape,
                        .cb = cb,
                        .coalesce = 0,
                        .use_samples_q16 = 1,
                        .duration_q16 = samples_q16 };
    if (!slope_cmd_enqueue(&cmd)) {
        uint32_t irq = save_and_disable_interrupts();
        S_toward_q16_apply(index, destination_q16, 0, shape, cb, 1, samples_q16);
        restore_interrupts(irq);
    }
}

void S_toward_samples_q16_coalescable( int        index
                                    , q16_t      destination_q16
                                    , int64_t    samples_q16
                                    , Shape_t    shape
                                    , Callback_t cb
                                    )
{
    if (!slopes) { return; }

    if (get_core_num() == 1) {
        uint32_t irq = save_and_disable_interrupts();
        S_toward_q16_apply(index, destination_q16, /*ms_q16*/0, shape, cb, 1, samples_q16);
        restore_interrupts(irq);
        return;
    }

    slope_cmd_t cmd = { .index = (int8_t)index,
                        .dest_q16 = destination_q16,
                        .ms_q16 = 0,
                        .shape = shape,
                        .cb = cb,
                        .coalesce = 1,
                        .use_samples_q16 = 1,
                        .duration_q16 = samples_q16 };
    if (!slope_cmd_enqueue(&cmd)) {
        uint32_t irq = save_and_disable_interrupts();
        S_toward_q16_apply(index, destination_q16, 0, shape, cb, 1, samples_q16);
        restore_interrupts(irq);
    }
}

void S_toward_seq_q16( int        index
                     , q16_t      destination_q16
                     , int64_t    samples_q16
                     , Shape_t    shape
                     , Callback_t cb
                     , uint16_t   seq
                     , bool       lookahead
                     , bool       coalesce
                     )
{
    if (!slopes) { return; }
    slope_cmd_t cmd = { .index = (int8_t)index,
                        .dest_q16 = destination_q16,
                        .ms_q16 = 0,
                        .shape = shape,
                        .cb = cb,
                        .coalesce = coalesce ? 1 : 0,
                        .use_samples_q16 = 1,
                        .duration_q16 = samples_q16,
                        .kind = SLOPE_CMD_START,
                        .lookahead = lookahead ? 1 : 0,
                        .seq = seq };
    if (get_core_num() == 1 || !slope_cmd_enqueue(&cmd)) {
        uint32_t irq = save_and_disable_interrupts();
        S_toward_q16_apply(index, destination_q16, 0, shape, cb, 1, samples_q16);
        slopes[index].seq = seq;
        slopes[index].lookahead = lookahead;
        restore_interrupts(irq);
    }
}

void S_queue_next_q16( int        index
                     , bool       has_jump
                     , q16_t      jump_q16
                     , bool       has_slope
                     , q16_t      destination_q16
                     , int64_t    samples_q16
                     , Shape_t    shape
                     , uint16_t   seq
                     , bool       lookahead
                     , Callback_t started
                     , Callback_t ended
                     )
{
    if (!slopes || index < 0 || index >= SLOPE_CHANNELS) { return; }
    slope_cmd_t cmd = { .index = (int8_t)index,
                        .dest_q16 = destination_q16,
                        .shape = shape,
                        .cb = ended,
                        .use_samples_q16 = 1,
                        .duration_q16 = samples_q16,
                        .kind = SLOPE_CMD_QUEUE_NEXT,
                        .lookahead = (has_slope && lookahead) ? 1 : 0,
                        .seq = seq,
                        .has_jump = has_jump ? 1 : 0,
                        .has_slope = has_slope ? 1 : 0,
                        .jump_q16 = jump_q16,
                        .started = started };
    if (get_core_num() == 1 || !slope_cmd_enqueue(&cmd)) {
        uint32_t irq = save_and_disable_interrupts();
        slope_queue_next_apply(&cmd);
        restore_interrupts(irq);
    }
}

void S_toward_samples( int        index
                     , float      destination
                     , int32_t    samples
                     , Shape_t    shape
                     , Callback_t cb
                     )
{
    int64_t samples_q16 = ((int64_t)samples) << Q16_SHIFT; // integer samples → Q16
    S_toward_samples_q16(index, FLOAT_TO_Q16(destination), samples_q16, shape, cb);
}

// CRITICAL: Place in RAM - called from Timer_Process_Block at high frequency
__attribute__((section(".time_critical.S_step_v")))
float* S_step_v( int     index
               , float*  out
               , int     size
               )
{
    // turn index into pointer
    if( index < 0 || index >= SLOPE_CHANNELS ){ return out; }
    Slope_t* self = &slopes[index]; // safe pointer

    return step_v( self, out, size );
}


///////////////////////
// private defns

// CRITICAL: Dispatcher in RAM for consistent timing
__attribute__((section(".time_critical.step_v")))
static float* step_v( Slope_t* self
                    , float*   out
                    , int      size
                    )
{
    if( self->countdown_q16 <= 0 ){ // at destination (Q16 comparison)
        static_v( self, out, size );
    } else if( self->countdown_q16 > ((int64_t)size << Q16_SHIFT) ){ // no edge case (size as Q16)
        motion_v( self, out, size );
    } else {
        breakpoint_v( self, out, size );
    }
    return out;
}

// CRITICAL: Static value handler in RAM
__attribute__((section(".time_critical.static_v")))
static float* static_v( Slope_t* self, float* out, int size )
{
    // OPTIMIZATION: Only set final sample since we discard the rest
    // Skip the loop - value is static anyway
    out[size-1] = Q16_TO_FLOAT(self->here_q16);  // Convert Q16 to float for buffer
    
    int64_t threshold_q16 = -((int64_t)1024 << Q16_SHIFT); // -1024.0 in Q16
    if( self->countdown_q16 > threshold_q16 ){ // count overflow samples
        self->countdown_q16 -= ((int64_t)size << Q16_SHIFT); // size as Q16
    }
    return shaper_v( self, out, size );
}

// CRITICAL: Motion calculation in RAM - most common path for LFOs
__attribute__((section(".time_critical.motion_v")))
static float* motion_v( Slope_t* self, float* out, int size )
{
    // OPTIMIZATION: Only calculate final sample since we discard the rest
    // This reduces work by 87.5% for size=8 blocks
    
    // Advance by the whole block in one shot (Q16 precision, 64-bit safe)
    slope_advance(self, ((int64_t)size << Q16_SHIFT));
    
    // Store final value (convert Q16 to float for buffer compatibility)
    out[size-1] = Q16_TO_FLOAT(self->here_q16);
    
    return shaper_v( self, out, size );
}

// CRITICAL: Breakpoint handler in RAM - handles slope transitions and callbacks
__attribute__((section(".time_critical.breakpoint_v")))
static float* breakpoint_v( Slope_t* self, float* out, int size )
{
    if( size <= 0 ){ return out; }

    slope_advance(self, (int64_t)Q16_ONE); // Advance by one sample
    if( self->countdown_q16 <= 0 ){
        // TODO unroll overshoot and apply proportionally to the post-*act sample
        self->here_q16 = Q16_ONE; // clamp for overshoot (1.0 in Q16)
        if( self->action != NULL ){
            Callback_t act = self->action;
            self->action = NULL;
            self->shaped_q16 = self->dest_q16; // save real destination
            (*act)(self->index, self->seq);
            // side-affects: self->{dest, shape, action, countdown, delta, (here)}
        }
        if( self->action != NULL ){ // instant callback
            *out++ = Q16_TO_FLOAT(self->here_q16);
            // 1. unwind self->countdown (ADD it to countdown)
            // 2. recalc current sample with new slope
            // 3. below call should be on out[0] and size
            if(size > 1){
                return step_v( self, out, size-1 );
            } else { // handle breakpoint on last sample of frame
                return out;
            }
        } else { // slope complete, or queued response
            self->here_q16  = Q16_ONE; // 1.0 in Q16
            *out++ = Q16_TO_FLOAT(self->here_q16);
            return static_v( self, out, size-1 );
        }
    } else {
        *out++ = Q16_TO_FLOAT(self->here_q16);
        return breakpoint_v( self, out, size-1 ); // recursive call
    }
}


///////////////////////////////
// shapers

// CRITICAL: Shape application in RAM - applies expensive sin/exp/log
// vectors for optimized segments (assume: self->shape is constant)
__attribute__((section(".time_critical.shaper_v")))
static float* shaper_v( Slope_t* self, float* out, int size )
{
    // OPTIMIZATION: Only process final sample since we discard the rest
    // This avoids expensive vectorized processing of 7 unused samples

    q16_t here_q16 = self->here_q16; // Already in Q16 [0.0, 1.0]
    q16_t shaped_q16;

    // Apply shape function. For the common step shapes we stay in Q16,
    // for others we still use the existing float-based helpers.
    switch( self->shape ){
        case SHAPE_Sine:
            shaped_q16 = lut_lookup_q16(lut_sin, here_q16);  // Direct Q16→Q16, no float conversions!
            break;
        case SHAPE_Log:
            shaped_q16 = lut_lookup_q16(lut_log, here_q16);  // Direct Q16→Q16, no float conversions!
            break;
        case SHAPE_Expo:
            shaped_q16 = lut_lookup_q16(lut_exp, here_q16);  // Direct Q16→Q16, no float conversions!
            break;
        case SHAPE_Now:
            shaped_q16 = shapes_step_now_q16(here_q16);
            break;
        case SHAPE_Wait:
            shaped_q16 = shapes_step_wait_q16(here_q16);
            break;
        case SHAPE_Over:
        case SHAPE_Under:
        case SHAPE_Rebound:
            shaped_q16 = shapes_ease_q16(self->shape, here_q16);
            break;
        case SHAPE_Linear:
        default:
            shaped_q16 = here_q16; // Linear shape already in Q16
            break;
    }

    // Map to output range: shaped * scale + last (all Q16 arithmetic)
    q16_t voltage_q16 = Q16_MUL(shaped_q16, self->scale_q16) + self->last_q16;

    // Save last state
    self->shaped_q16 = voltage_q16;

    // Apply quantization before hardware output
    extern q16_t AShaper_quantize_single_q16(int index, q16_t voltage_q16);
    q16_t quantized_q16 = AShaper_quantize_single_q16(self->index, voltage_q16);

    // Update hardware output directly for real-time response
    extern void hardware_output_set_voltage_q16(int channel, q16_t voltage_q16);
    hardware_output_set_voltage_q16(self->index + 1, quantized_q16);  // Direct Q16, no conversion!

    return out;
}
