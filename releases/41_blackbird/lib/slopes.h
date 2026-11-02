#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "sample_rate.h"

// ========================================================================
// Q16.16 Fixed-Point System for RP2040 (Cortex-M0+ has no FPU)
// ========================================================================
// Q16.16 format: 16-bit integer, 16-bit fractional
// Range: ±32768 (covers ±6V with headroom for arithmetic overflow)
// Precision: 1/65536 ≈ 0.000015 (sub-millivolt precision)
// ========================================================================

typedef int32_t q16_t;

#define Q16_SHIFT 16
#define Q16_ONE (1 << Q16_SHIFT)        // 1.0 = 65536
#define Q16_HALF (1 << (Q16_SHIFT - 1))  // 0.5 = 32768

// Fast conversion macros with rounding
#define FLOAT_TO_Q16(f) ((q16_t)((f) * (float)Q16_ONE + ((f) >= 0.0f ? 0.5f : -0.5f)))
#define Q16_TO_FLOAT(q) ((float)(q) / (float)Q16_ONE)

// Q16.16 arithmetic (compiler will optimize these)
#define Q16_MUL(a, b) ((q16_t)(((int64_t)(a) * (int64_t)(b)) >> Q16_SHIFT))
#define Q16_DIV(a, b) ((q16_t)(((int64_t)(a) << Q16_SHIFT) / (int64_t)(b)))
// Wide variant keeps full precision for intermediate values larger than 32-bit
#define Q16_MUL_WIDE(a, b) (((int64_t)(a) * (int64_t)(b)) >> Q16_SHIFT)

// Optional bounded fast-path helpers (avoid 64-bit ops when operands are small)
#define Q16_MUL_SMALL_LIMIT 46340  // sqrt(INT32_MAX) ≈ 46340 → safe 32-bit mul
static inline bool q16_can_mul_small(q16_t a, q16_t b) {
    return (a <= Q16_MUL_SMALL_LIMIT && a >= -Q16_MUL_SMALL_LIMIT &&
            b <= Q16_MUL_SMALL_LIMIT && b >= -Q16_MUL_SMALL_LIMIT);
}
static inline q16_t Q16_MUL_SMALL(q16_t a, q16_t b) {
    if (q16_can_mul_small(a, b)) {
        int32_t prod = ((int32_t)a * (int32_t)b); // safe 32-bit mul
        return (q16_t)(prod >> Q16_SHIFT);
    }
    return Q16_MUL(a, b);
}

#define Q16_DIV_SMALL_LIMIT 32767  // |a| << 16 must stay within int32_t
static inline bool q16_can_div_small(q16_t a) {
    return (a <= Q16_DIV_SMALL_LIMIT && a >= -Q16_DIV_SMALL_LIMIT);
}
static inline q16_t Q16_DIV_SMALL(q16_t a, q16_t b) {
    if (q16_can_div_small(a) && b != 0) {
        int32_t num = ((int32_t)a) << Q16_SHIFT;
        return (q16_t)(num / (int32_t)b);
    }
    return Q16_DIV(a, b);
}

// Q16 to Q12 conversion for 12-bit DAC output (±2048 range)
#define Q16_TO_Q12(q) ((int16_t)((q) >> (Q16_SHIFT - 12)))
#define Q12_TO_Q16(q) ((q16_t)((q) << (Q16_SHIFT - 12)))

// need an Init() fn. send SR as an argument
#define SAMPLE_RATE PROCESS_SAMPLE_RATE_HZ_INT
#define iSAMPLE_RATE (1.0f/(float)PROCESS_SAMPLE_RATE_HZ)
#define SAMPLES_PER_MS (PROCESS_SAMPLE_RATE_HZ/1000.0f)
#define SAMPLES_PER_MS_Q16 FLOAT_TO_Q16(SAMPLES_PER_MS)  // Pre-calculated Q16 constant

typedef enum{ SHAPE_Linear
            , SHAPE_Sine
            , SHAPE_Log
            , SHAPE_Expo
            , SHAPE_Now
            , SHAPE_Wait
            , SHAPE_Over
            , SHAPE_Under
            , SHAPE_Rebound
} Shape_t;

// Slope events for core 0 (ASL): `seq` is the tag the slope was started or
// queued with, so the receiver can tell stale events from current ones
typedef void (*Callback_t)(int channel, uint16_t seq);

typedef struct{
    int         index;
    
    // Q16.16 fixed-point state - eliminates FPU operations
    q16_t       dest_q16;      // destination voltage
    q16_t       last_q16;      // previous dest voltage
    q16_t       scale_q16;     // voltage range (dest - last)
    q16_t       shaped_q16;    // current shaped output voltage
    
    // Q16.16 interpolation state
    q16_t       here_q16;      // current interp position [0.0, 1.0]
    int64_t     countdown_q16; // samples remaining (Q16 precision)
    int64_t     duration_q16;  // total samples for the current slew (Q16 precision)
    int64_t     elapsed_q16;   // samples already processed (Q16 precision)
    
    Shape_t     shape;
    Callback_t  action;
    bool        action_fired;  // ended and handed its action to core 0; the next
                               // slope (started by that action) may carry overflow
    uint16_t    seq;           // tag from S_toward_seq_q16 / S_queue_next_q16
    bool        lookahead;     // core 0 will queue the next slope (S_queue_next_q16):
                               // rendering pauses at the end until it's there
} Slope_t;

#define SLOPE_CHANNELS 4

// Sample buffering configuration for Core 1 block renderer
#define SLOPE_BUFFER_CAPACITY 32
#define SLOPE_BUFFER_LOW_WATER 8
#define SLOPE_RENDER_CHUNK 8

// refactor for dynamic SLOPE_CHANNELS
// refactor for dynamic SAMPLE_RATE
// refactor to S_init returning pointers, but internally tracking indexes?

void S_init( int channels );

Shape_t S_str_to_shape( const char* s );

// Q16.16 fixed-point API (new, optimized)
void S_toward_q16( int        index
                 , q16_t      destination_q16
                 , q16_t      ms_q16
                 , Shape_t    shape
                 , Callback_t cb
                 );
// Coalescable variant: when invoked from Core0, the latest pending command for
// the same channel may overwrite older not-yet-applied commands to avoid backlog.
void S_toward_q16_coalescable( int        index
                            , q16_t      destination_q16
                            , q16_t      ms_q16
                            , Shape_t    shape
                            , Callback_t cb
                            );
// Samples-based duration (Q16 samples) to avoid ms→samples conversion overhead
void S_toward_samples_q16( int        index
                         , q16_t      destination_q16
                         , int64_t    samples_q16
                         , Shape_t    shape
                         , Callback_t cb
                         );
// Coalescable samples-based variant
void S_toward_samples_q16_coalescable( int        index
                                    , q16_t      destination_q16
                                    , int64_t    samples_q16
                                    , Shape_t    shape
                                    , Callback_t cb
                                    );
// Float wrapper for samples-based duration
// Start a slope now, tagged `seq` (passed back with its events). With
// lookahead, core 0 promises to queue the following slope with
// S_queue_next_q16, and `cb` is NULL: there's no end event.
void S_toward_seq_q16( int        index
                     , q16_t      destination_q16
                     , int64_t    samples_q16
                     , Shape_t    shape
                     , Callback_t cb
                     , uint16_t   seq
                     , bool       lookahead
                     , bool       coalesce
                     );

// ASL look-ahead: the slope to start when the current one ends, on that
// exact sample, carrying the fraction of a sample the current one overshot.
// Without it, each segment waited 1-3 samples (varying) for core 0 to start
// the next: audio-rate ASL had cycle-to-cycle jitter, heard as noise FM.
// Optionally jump to `jump_q16` first (instant to()s before it); with no
// slope (has_slope false), just jump/hold: the sequence waits or is done.
// `started` is called (on core 0) with `seq` once it has begun; `ended` when
// it ends, if it isn't lookahead itself.
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
                     );

void S_toward_samples( int        index
                     , float      destination
                     , int32_t    samples
                     , Shape_t    shape
                     , Callback_t cb
                     );
q16_t S_get_state_q16( int index );

// Float API (legacy, wraps Q16 internally for backward compatibility)
float S_get_state( int index );
void S_toward( int        index
             , float      destination
             , float      ms
             , Shape_t    shape
             , Callback_t cb
             );
// Single-sample processing for Core 1 ISR (new, optimized for sample-accurate output)
q16_t S_step_one_sample_q16( int index );

// Core 1 buffered rendering helpers
void S_slope_buffer_reset(void);
bool S_slope_buffer_needs_fill(int index);
void S_slope_buffer_fill_block(int index, int samples);
void S_request_slope_buffer_fill(int index);
q16_t S_consume_buffered_sample_q16(int index);
bool S_slope_buffer_background_service(void);  // true if it rendered samples
extern volatile uint32_t g_slope_inline_renders;  // buffer underruns (sample rendered in the ISR instead)

// Process any queued cross-core slope commands (Core1 only).
// This avoids cross-core races on 64-bit slope state.
void S_process_pending_commands(void);

// Diagnostics
uint32_t S_get_cmd_drop_count(void);

float* S_step_v( int     index
               , float*  out
               , int     size
               );

void S_reset(void);

// Dedicated oscillator (audio-friendly; fractional phase accumulator)
// Returns true on success. Channel index is 0-based.
bool S_set_oscillator(int index, float freq_hz, float level_volts, Shape_t shape);
void S_clear_oscillator(int index);
