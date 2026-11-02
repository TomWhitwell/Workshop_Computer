#include "clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// Unified time source (ms since boot)
#include "pico/time.h" // For get_absolute_time(), to_ms_since_boot
#include "hardware/structs/timer.h"
// Monotonic milliseconds since boot (unifies all clock math). Same value as
// to_ms_since_boot(get_absolute_time()), but reads the timer directly so core 1
// can call it while core 0 writes flash (time_us_64() lives in flash).
uint32_t __not_in_flash_func(clock_ms_since_boot)(void) {
    uint32_t hi = timer_hw->timerawh;
    uint32_t lo;
    while (true) {
        lo = timer_hw->timerawl;
        uint32_t next_hi = timer_hw->timerawh;
        if (hi == next_hi) break;
        hi = next_hi;
    }
    return (uint32_t)((((uint64_t)hi << 32) | lo) / 1000u);
}
static inline uint32_t clock_now_ms(void) {
    return clock_ms_since_boot();
}

#include "clock_ll.h" // linked list for clocks
#include "l_crowlib.h" // L_queue_clock_* functions
#include "events_lockfree.h" // for clock_lockfree_reset_stats & stats

// External functions to control pulse outputs
extern void hardware_pulse_output_set(int channel, bool state);
extern bool pulseout_has_custom_action(int channel);
extern void pulseout_execute_action(int channel);

// Pulse output state tracking
static uint32_t pulse1_end_time = 0;
static bool pulse1_active = false;

// Enhanced RP2040 timing - use sample-accurate timing instead of milliseconds
static uint64_t sample_counter = 0; // Global sample counter for precise timing
static uint32_t HAL_GetTick(void) {
    return to_ms_since_boot(get_absolute_time());
}

// New sample-based timing functions for improved precision - PUBLIC API
void clock_set_sample_counter(uint64_t samples) {
    sample_counter = samples;
}

uint64_t clock_get_sample_counter(void) {
    return sample_counter;
}

void __not_in_flash_func(clock_increment_sample_counter)(void) {
    sample_counter++;
}



///////////////////////////////
// private types

// Fixed-point representation: Q16.16 for beats, 32-bit milliseconds for time
#define Q16_SHIFT 16
#define Q16_ONE   (1u << Q16_SHIFT)

typedef struct{
    uint32_t beat_q16;           // beats in Q16.16
    uint32_t last_beat_time_ms;  // HAL_GetTick() snapshot at last reference update
    uint32_t beat_duration_ms;   // milliseconds per beat (rounded)
    uint32_t beat_duration_inv_q16; // beats per ms in Q16.16 (reciprocal of beat_duration_ms)
} clock_reference_t;

typedef struct{
    uint32_t wakeup_ms;   // absolute ms tick for next wakeup
    uint64_t beat_q16;    // current internal beat position (Q16.16)
    int32_t  error_q16;   // accumulated fractional ms error
    bool     running;
} clock_thread_HD_t;

////////////////////////////////////
// global data

static clock_thread_HD_t internal; // for internal clocksource
static clock_source_t clock_source = CLOCK_SOURCE_INTERNAL;
static bool clock_source_manual = false;

static clock_reference_t reference;

// Q16.16 representation of beat count (beats with fractional component)
static uint32_t precise_beat_q16 = 0;

// Monitoring counters
static uint32_t clock_schedule_successes = 0;
static uint32_t clock_schedule_failures = 0;
static uint32_t clock_active_max = 0;
static uint32_t clock_pool_capacity = 0;

static inline void clock_update_active_max(void) {
    extern int sleep_count, sync_count;
    uint32_t active = (uint32_t)(sleep_count + sync_count);
    if (active > clock_active_max) {
        clock_active_max = active;
    }
}

/////////////////////////////////////////////
// private declarations

static void clock_internal_run(uint32_t ms);

/////////////////////////////////////////////
// public defs

void clock_init( int max_clocks )
{
    ll_init(max_clocks); // init linked-list for managing clock threads

    clock_pool_capacity = (uint32_t)max_clocks;

    clock_set_source( CLOCK_SOURCE_INTERNAL );
    clock_update_reference(0, 0.5); // set to zero beats, at 120bpm (0.5s/beat)

    // start clock sources
    clock_internal_init();
    clock_crow_init();
}

static inline uint32_t precision_beat_of_now_q16(uint32_t time_now_ms){
    // Compute beats since last reference update in Q16.16: elapsed_ms * beats_per_ms_q16
    uint32_t elapsed_ms = time_now_ms - reference.last_beat_time_ms;
    uint64_t frac_q16 = (uint64_t)elapsed_ms * (uint64_t)reference.beat_duration_inv_q16; // Q16.16 * ms
    return reference.beat_q16 + (uint32_t)frac_q16;
}

// This function must only be called when time_now changes!
// TIME SENSITIVE. this function is run every 1ms, so optimize it for speed.
void __not_in_flash_func(clock_update)(uint32_t time_now)
{
    // increments the beat count if we've crossed into the next beat
    clock_internal_run(time_now);

    // calculate the Q16.16 beat count for .syncing checks
    precise_beat_q16 = precision_beat_of_now_q16(time_now);

    // TODO can we use <= for time comparison or does it create double-trigs?
    // this should reduce latency by 1ms if it works.
sleep_next:
    if(sleep_head // list is not empty
    && sleep_head->wakeup <= time_now){ // time to awaken
        L_queue_clock_resume(sleep_head->coro_id); // event!
        extern int sleep_count;
        if (sleep_count > 0) sleep_count--;
        ll_insert_idle(ll_pop(&sleep_head)); // return to idle list
        goto sleep_next; // check the next sleeper too!
    }
sync_next:
    if(sync_head // list is not empty
    && sync_head->wakeup <= precise_beat_q16){ // time to awaken
        L_queue_clock_resume(sync_head->coro_id); // event!
        extern int sync_count;
        if (sync_count > 0) sync_count--;
        ll_insert_idle(ll_pop(&sync_head)); // return to idle list
        goto sync_next; // check the next syncer too!
    }
}

bool clock_schedule_resume_sleep( int coro_id, float seconds )
{
    uint32_t now_ms = clock_now_ms();
    uint32_t delta_ms = (uint32_t)(seconds * 1000.0f + 0.5f);
    uint32_t wakeup = now_ms + delta_ms;
    bool ok = ll_insert_event(&sleep_head, coro_id, wakeup);
    if (ok) {
        extern int sleep_count;
        sleep_count++;
        clock_schedule_successes++;
        clock_update_active_max();
    } else {
        clock_schedule_failures++;
    }
    return ok;
}

bool clock_schedule_resume_sync( int coro_id, float beats ){
    if (beats <= 0.0f) {
        return false;
    }

    uint32_t dbeats_q16 = (uint32_t)(beats * (float)Q16_ONE + 0.5f);
    if (dbeats_q16 == 0) {
        return false;
    }

    // Use up-to-the-moment beat position to schedule the next multiple
    uint32_t now_beats_q16 = precision_beat_of_now_q16(clock_now_ms());
    uint32_t mod = now_beats_q16 % dbeats_q16;
    uint32_t awaken = now_beats_q16 - mod + dbeats_q16;

    // check we haven't already passed it in the sub-beat & add another step if we have
    if (awaken <= precise_beat_q16) {
        awaken += dbeats_q16;
    }

    bool ok = ll_insert_event(&sync_head, coro_id, awaken);
    if (ok) {
        extern int sync_count;
        sync_count++;
        clock_schedule_successes++;
        clock_update_active_max();
    } else {
        clock_schedule_failures++;
    }
    return ok;
}

// this function directly sleeps for an amount of beats (not sync'd to the beat)
bool clock_schedule_resume_beatsync( int coro_id, float beats ){
    // beats can be fractional; compute milliseconds using fixed-point duration
    uint32_t beat_ms = reference.beat_duration_ms;
    uint32_t delta_ms = (uint32_t)(beats * (float)beat_ms + 0.5f);
    uint32_t now_ms = clock_now_ms();
    uint32_t wakeup = now_ms + delta_ms;
    bool ok = ll_insert_event(&sleep_head, coro_id, wakeup);
    if (ok) {
        extern int sleep_count;
        sleep_count++;
        clock_schedule_successes++;
        clock_update_active_max();
    } else {
        clock_schedule_failures++;
    }
    return ok;
}

// Called from core 1 via clock_update(), so must stay in RAM.
void __not_in_flash_func(clock_update_reference)(float beats, float beat_duration)
{
    // Convert to fixed point
    reference.beat_q16 = (uint32_t)(beats * (float)Q16_ONE + 0.5f);
    uint32_t beat_ms = (uint32_t)(beat_duration * 1000.0f + 0.5f);
    if (beat_ms == 0) {
        beat_ms = 1; // avoid division by zero
    }
    reference.beat_duration_ms = beat_ms;
    reference.beat_duration_inv_q16 = (uint32_t)(((uint64_t)Q16_ONE * 1) / beat_ms); // beats per ms in Q16
    reference.last_beat_time_ms = clock_now_ms();
}

void __not_in_flash_func(clock_update_reference_from)(float beats, float beat_duration, clock_source_t source)
{
    if( clock_source == source ){
        clock_update_reference( beats, beat_duration );
    }
}

void clock_start_from( clock_source_t source )
{
    if( clock_source == source ){
        // Call global clock_start_handler in Lua (matches crow behavior)
        extern lua_State* get_lua_state(void);
        lua_State* L = get_lua_state();
        if (L) {
            lua_getglobal(L, "clock_start_handler");
            if (lua_isfunction(L, -1)) {
                if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
            }
        }
    }
}

void clock_stop_from( clock_source_t source )
{
    if( clock_source == source ){
        // Call global clock_stop_handler in Lua (matches crow behavior)
        extern lua_State* get_lua_state(void);
        lua_State* L = get_lua_state();
        if (L) {
            lua_getglobal(L, "clock_stop_handler");
            if (lua_isfunction(L, -1)) {
                if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);
            }
        }
    }
}

void clock_set_source( clock_source_t source )
{
    if (clock_source_manual) {
        return; // Respect manual lock
    }
    if( source >= 0 && source < CLOCK_SOURCE_LIST_LENGTH ){
        clock_source = source;
    }
}

void clock_set_source_manual(clock_source_t source)
{
    if( source >= 0 && source < CLOCK_SOURCE_LIST_LENGTH ){
        clock_source = source;
        clock_source_manual = true;
    }
}

void clock_set_source_auto_mode(void)
{
    clock_source_manual = false;
}

clock_source_t clock_get_source(void)
{
    return clock_source;
}

bool clock_is_source_manual(void)
{
    return clock_source_manual;
}

float clock_get_time_beats(void)
{
    return (float)precise_beat_q16 * (1.0f / (float)Q16_ONE);
}

float clock_get_time_seconds(void)
{
    return (float)clock_now_ms() * 0.001f;
}

float clock_get_tempo(void)
{
    if (reference.beat_duration_ms == 0) return 0.0f;
    return 60000.0f / (float)reference.beat_duration_ms;
}

void clock_cancel_coro( int coro_id )
{
    ll_remove_by_id(coro_id);
}

void clock_cancel_coro_all( void )
{
    ll_cleanup();
    extern int sleep_count, sync_count;
    sleep_count = 0;
    sync_count = 0;
}

// Stats accessors
uint32_t clock_get_schedule_failures(void)  { return clock_schedule_failures; }
uint32_t clock_get_schedule_successes(void) { return clock_schedule_successes; }
uint32_t clock_get_max_active_threads(void) { return clock_active_max; }
uint32_t clock_get_pool_capacity(void)      { return clock_pool_capacity; }

void clock_reset_stats(void)
{
    clock_schedule_failures = 0;
    clock_schedule_successes = 0;
    // current active is a baseline for max after reset
    extern int sleep_count, sync_count;
    clock_active_max = (uint32_t)(sleep_count + sync_count);
    // reset clock resume queue stats too
    clock_lockfree_reset_stats();
}


/////////////////////////////////////////////////
// in clock_internal.h

static uint32_t internal_interval_ms_q16; // beat interval in ms Q16.16
static uint32_t internal_interval_ms;     // beat interval in whole ms (rounded)

void clock_internal_init(void)
{
    internal.running = false;
    clock_internal_set_tempo(120);
    clock_internal_start(0.0f, true);
}

void clock_internal_set_tempo( float bpm )
{
    if (bpm <= 0.0f) bpm = 120.0f;
    // interval in ms as Q16.16: (60000 / bpm) * 2^16
    float interval_ms = 60000.0f / bpm;
    internal_interval_ms = (uint32_t)(interval_ms + 0.5);
    internal_interval_ms_q16 = (uint32_t)(interval_ms * (double)Q16_ONE + 0.5);
    clock_internal_start( (float)(internal.beat_q16 * (1.0f / (float)Q16_ONE)), false );
}

void clock_internal_start( float new_beat, bool transport_start )
{
    // Initialize internal beat state
    internal.beat_q16 = (uint64_t)(new_beat * (float)Q16_ONE + 0.5f);
    clock_update_reference_from( (float)new_beat
                               , (float)internal_interval_ms / 1000.0f
                               , CLOCK_SOURCE_INTERNAL );

    if( transport_start ){
        clock_start_from( CLOCK_SOURCE_INTERNAL ); // user callback
    }
    // Force a beat on the next tick. Not 0: clock_internal_run() advances
    // wakeup_ms by one interval per beat, so starting from 0 it would race
    // through a beat per ms until it caught up with uptime (thousands of
    // beats, every clock.sync firing each ms) after any tempo change or start.
    internal.wakeup_ms  = clock_now_ms();
    internal.error_q16  = 0;
    internal.running    = true;
}

void clock_internal_stop(void)
{
    internal.running = false; // actually stop the sync clock
    clock_stop_from( CLOCK_SOURCE_INTERNAL ); // user callback
}


/////////////////////////////////////
// private clock_internal

// note how we have to track the quantization error of the clock over cycles
// long-term precision is accurate to a double, while each clock pulse will
// be quantized to the tick *before* it's absolute position.
// this is important so that the beat division counter leads the userspace
// sync() calls & ensures they don't double-trigger.
static void clock_internal_run(uint32_t ms)
{
    if( internal.running ){
        uint32_t time_now = ms;
        if( internal.wakeup_ms <= time_now ){ // allow wake at exact tick
            internal.beat_q16 += Q16_ONE;

            // Use reference tempo if clock source is external, otherwise use internal tempo
            float beat_interval_sec = (clock_source == CLOCK_SOURCE_CROW)
                                    ? ((float)reference.beat_duration_ms / 1000.0f)
                                    : ((float)internal_interval_ms / 1000.0f);

            clock_update_reference_from( (float)internal.beat_q16 / (float)Q16_ONE
                                       , beat_interval_sec
                                       , CLOCK_SOURCE_INTERNAL );

            // Schedule next wakeup in integer ms with fractional error accumulation
            internal.wakeup_ms += (internal_interval_ms_q16 >> Q16_SHIFT);
            internal.error_q16 += (int32_t)(internal_interval_ms_q16 & (Q16_ONE - 1));
            if (internal.error_q16 >= (int32_t)Q16_ONE) {
                internal.wakeup_ms += 1;
                internal.error_q16 -= (int32_t)Q16_ONE;
            }
        }
    }
}


/////////////////////////////////////////////////
// in clock_input.h

static bool clock_crow_last_time_set;
static int clock_crow_counter;
static uint32_t clock_crow_last_time_ms;

#define DURATION_BUFFER_LENGTH 4

static uint32_t duration_buf[DURATION_BUFFER_LENGTH] = {0}; // beat durations in ms
static uint8_t beat_duration_buf_pos = 0;
static uint8_t beat_duration_buf_len = 0;
static uint32_t mean_sum_ms;

static uint32_t crow_in_div_q16 = (4u << Q16_SHIFT); // beats per pulse (Q16.16)


void clock_crow_init(void)
{
    clock_crow_counter = 0;
    clock_crow_last_time_set = false;
    mean_sum_ms = 0;
    beat_duration_buf_len = 0;
    beat_duration_buf_pos = 0;
    for (int i = 0; i < DURATION_BUFFER_LENGTH; ++i) duration_buf[i] = 0;
}

// called by an event received on input
void clock_input_handler( int id, float freq )
{
    // this stub function just ignores the args
    clock_crow_handle_clock();
}
void clock_crow_handle_clock(void)
{
    uint32_t current_time_ms = clock_now_ms();

    if( clock_crow_last_time_set == false ){
        clock_crow_last_time_set = true;
        clock_crow_last_time_ms = current_time_ms;
    } else {
        uint32_t elapsed_ms = current_time_ms - clock_crow_last_time_ms;

        // Compute beat duration in ms: elapsed_ms * crow_in_div_q16 >> 16
        uint32_t beat_duration_ms = (uint32_t)(((uint64_t)elapsed_ms * (uint64_t)crow_in_div_q16) >> Q16_SHIFT);

        if( beat_duration_ms > 4000u ){ // assume clock stopped (>4s per beat)
            clock_crow_last_time_ms = current_time_ms;
        } else {
            // Ring buffer average (simple sum/len)
            if( beat_duration_buf_len < DURATION_BUFFER_LENGTH ){
                beat_duration_buf_len++;
            }

            // Subtract the old value at pos and add new
            uint32_t old = duration_buf[beat_duration_buf_pos];
            mean_sum_ms += beat_duration_ms;
            mean_sum_ms -= old;
            duration_buf[beat_duration_buf_pos] = beat_duration_ms;
            beat_duration_buf_pos = (beat_duration_buf_pos + 1) % DURATION_BUFFER_LENGTH;

            clock_crow_counter++;
            clock_crow_last_time_ms = current_time_ms;

            // Compute beats and average duration in float for API compatibility
            uint64_t beat_q16 = (uint64_t)clock_crow_counter * (uint64_t)crow_in_div_q16;
            float beat = (float)beat_q16 / (float)Q16_ONE;
            float beat_duration_sec = (beat_duration_buf_len == 0) ? 0.0f : ((float)mean_sum_ms / (float)beat_duration_buf_len) * 0.001f;
            clock_update_reference_from(beat, beat_duration_sec, CLOCK_SOURCE_CROW);
        }
    }
}

void clock_crow_in_div( float div )
{
    if (div <= 0.0f) div = 1.0f;
    crow_in_div_q16 = (uint32_t)((1.0f / div) * (float)Q16_ONE + 0.5f);
}
