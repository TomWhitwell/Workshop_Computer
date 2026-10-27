#include "ll_timers.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"
#include "ashapes.h"  // For output quantization
#include "slopes.h"   // For q16_t and Q16_SHIFT
#include "sample_rate.h"

// Timer implementation for RP2040 Workshop Computer with block processing optimization
// Aligned block size (32 samples) for consistent timing with audio processing

typedef struct {
    timer_callback_t callback;
    float period_seconds;
    bool active;
    uint32_t period_samples;      // Period in ProcessSample-rate samples
    uint64_t next_trigger_sample; // When to trigger next (64-bit for long-running systems)
    float period_error;           // Accumulated fractional sample error for precision
} bb_timer_t;

static bb_timer_t* timers = NULL;
static int max_timers = 0;
volatile uint64_t global_sample_counter = 0; // Incremented in ProcessSample() ISR - 64-bit for precision
static spin_lock_t* timers_lock = NULL;

#define TIMER_SAMPLE_RATE PROCESS_SAMPLE_RATE_HZ

// Block processing state - aligned with audio blocks for consistent timing
static int sample_accumulator = 0; // Count samples until next block processing

void Timer_Init(int num_timers) {
    max_timers = num_timers;
    if (timers) {
        free(timers);
    }
    timers = malloc(sizeof(bb_timer_t) * max_timers);
    timers_lock = spin_lock_instance(7); // Dedicated lock for timer state
    
    for (int i = 0; i < max_timers; i++) {
        timers[i].callback = NULL;
        timers[i].period_seconds = 1.0f;
        timers[i].active = false;
        timers[i].period_samples = (uint32_t)TIMER_SAMPLE_RATE; // Default 1 second at ProcessSample rate
        timers[i].next_trigger_sample = 0;
        timers[i].period_error = 0.0f;
    }
    global_sample_counter = 0;
    printf("Timer: Init %d timers\n", num_timers);
}

// Consistent 64-bit read of the sample counter without tearing across cores
static inline uint64_t read_global_sample_counter(void) {
    uint32_t hi1, lo, hi2;
    do {
        hi1 = (uint32_t)(global_sample_counter >> 32);
        lo  = (uint32_t)(global_sample_counter & 0xFFFFFFFFu);
        hi2 = (uint32_t)(global_sample_counter >> 32);
    } while (hi1 != hi2);
    return ((uint64_t)hi1 << 32) | lo;
}

void Timer_Start(int timer_id, timer_callback_t callback) {
    if (timer_id < 0 || timer_id >= max_timers) {
        printf("Timer: Invalid timer ID %d\n", timer_id);
        return;
    }

    uint32_t irq_state = spin_lock_blocking(timers_lock);
    timers[timer_id].callback = callback;
    timers[timer_id].active = true;
    // Schedule first trigger based on current sample counter
    timers[timer_id].next_trigger_sample = read_global_sample_counter() + timers[timer_id].period_samples;
    spin_unlock(timers_lock, irq_state);
    // printf("Timer: Start timer %d (next trigger at sample %u)\n", timer_id, timers[timer_id].next_trigger_sample);
}

// Called from core 1 (Metro_bang), so must stay in RAM and must not printf.
void __not_in_flash_func(Timer_Stop)(int timer_id) {
    if (timer_id < 0 || timer_id >= max_timers) {
        return;
    }

    uint32_t irq_state = spin_lock_blocking(timers_lock);
    timers[timer_id].active = false;
    spin_unlock(timers_lock, irq_state);
}

void Timer_Set_Params(int timer_id, float seconds) {
    if (timer_id < 0 || timer_id >= max_timers) {
        printf("Timer: Invalid timer ID %d\n", timer_id);
        return;
    }
    
    uint32_t irq_state = spin_lock_blocking(timers_lock);
    timers[timer_id].period_seconds = seconds;
    // Convert seconds to samples at ProcessSample rate with precise fractional handling
    float precise_samples = seconds * TIMER_SAMPLE_RATE;
    timers[timer_id].period_samples = (uint32_t)precise_samples;
    timers[timer_id].period_error = precise_samples - (float)timers[timer_id].period_samples;
    spin_unlock(timers_lock, irq_state);
    
    // printf("Timer: Set timer %d period to %.3f seconds (%u samples + %.6f fractional)\n", 
    //        timer_id, seconds, timers[timer_id].period_samples, timers[timer_id].period_error);
}

// Timer processing - called from MainControlLoop at ~20kHz
// NO LONGER IN ISR! Safe to take time for complex calculations
// CRITICAL: Place in RAM for consistent timing at high poll rates
__attribute__((section(".time_critical.Timer_Process")))
void Timer_Process(void) {
    // Check if enough samples have passed for next block
    // global_sample_counter incremented by ProcessSample() ISR
    static uint64_t last_processed_sample = 0;
    
    // Process missed blocks to maintain accurate countdown timing
    // BUT limit catch-up to prevent infinite loops if CPU can't keep up
    int blocks_processed = 0;
    // Adaptive catch-up limit based on block size:
    // - Small blocks (≤4): Need more tolerance for Lua callback overhead
    // - Larger blocks (≥8): Less likely to fall behind significantly
    const int MAX_CATCHUP_BLOCKS = (TIMER_BLOCK_SIZE <= 4) ? 16 : 8;
    
    uint64_t sample_count = read_global_sample_counter();

    while (sample_count - last_processed_sample >= TIMER_BLOCK_SIZE 
           && blocks_processed < MAX_CATCHUP_BLOCKS) {
        Timer_Process_Block();
        // CRITICAL: Advance by exactly TIMER_BLOCK_SIZE to maintain precise timing
        last_processed_sample += TIMER_BLOCK_SIZE;
        blocks_processed++;
        sample_count = read_global_sample_counter();
    }
    
    // If we're STILL behind after catch-up limit, we're overloaded
    // Skip ahead to prevent system freeze, but this WILL cause timing drift
    if (sample_count - last_processed_sample >= TIMER_BLOCK_SIZE * MAX_CATCHUP_BLOCKS) {
        // Emergency: System is overloaded, skip ahead to prevent freeze
        last_processed_sample = sample_count - TIMER_BLOCK_SIZE;
        // This will cause frequency drift, but better than a frozen system
    }
}

// Critical: Timer callback processing - place in RAM for consistent timing
void __not_in_flash_func(Timer_Process_Block)(void) {
    // Process all timer events that occurred in this block
    // NOTE: Slope processing has moved to Core 1 ProcessSample() for sample-accurate output
    // This function now only handles timer callbacks (metros, ASL actions, etc.)
    
    // Process timer callbacks
    uint32_t irq_state = 0;
    uint64_t sample_count = read_global_sample_counter();

    for (int i = 0; i < max_timers; i++) {
        if (timers[i].callback == NULL) {
            continue;
        }

        static float accumulated_error[8] = {0}; // Track error for up to 8 timers

        while (true) {
            irq_state = spin_lock_blocking(timers_lock);
            bool fire = timers[i].active && (timers[i].next_trigger_sample <= sample_count);
            timer_callback_t cb = timers[i].callback;
            if (fire) {
                // Schedule next trigger with precise fractional error tracking
                timers[i].next_trigger_sample += timers[i].period_samples;

                if (i < 8) { // Safety check for static array
                    accumulated_error[i] += timers[i].period_error;

                    if (accumulated_error[i] >= 1.0f) {
                        timers[i].next_trigger_sample += 1;
                        accumulated_error[i] -= 1.0f;
                    } else if (accumulated_error[i] <= -1.0f) {
                        timers[i].next_trigger_sample -= 1;
                        accumulated_error[i] += 1.0f;
                    }
                }

                // Handle wrap-around for very long-running systems
                if (timers[i].next_trigger_sample < sample_count) {
                    timers[i].next_trigger_sample = sample_count + timers[i].period_samples;
                    if (i < 8) accumulated_error[i] = 0.0f; // Reset error on wrap
                }
            }
            spin_unlock(timers_lock, irq_state);

            if (!fire || cb == NULL) {
                break;
            }

            cb(i);

            // Prevent infinite loop for very short periods
            if (timers[i].period_samples < TIMER_BLOCK_SIZE) {
                break;
            }

            sample_count = read_global_sample_counter();
        }
    }
}
