#include "events_lockfree.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "debug.h"

// ARM Cortex-M0+ memory barriers for RP2040
#define DMB() __asm volatile ("dmb" ::: "memory")
#define DSB() __asm volatile ("dsb" ::: "memory")

// Global lock-free queues
metro_lockfree_queue_t g_metro_lockfree_queue;
input_lockfree_queue_t g_input_lockfree_queue;
clock_lockfree_queue_t g_clock_lockfree_queue;
asl_done_lockfree_queue_t g_asl_done_lockfree_queue;

// Statistics
static volatile uint32_t metro_events_posted = 0;
static volatile uint32_t metro_events_processed = 0;
static volatile uint32_t metro_events_dropped = 0;
static volatile uint32_t metro_events_coalesced = 0;
static volatile uint32_t input_events_posted = 0;
static volatile uint32_t input_events_processed = 0;
static volatile uint32_t input_events_dropped = 0;
static volatile uint32_t clock_events_posted = 0;
static volatile uint32_t clock_events_processed = 0;
static volatile uint32_t clock_events_dropped = 0;
static volatile uint32_t clock_events_coalesced = 0;
static volatile uint32_t asl_done_events_posted = 0;
static volatile uint32_t asl_done_events_processed = 0;
static volatile uint32_t asl_done_events_dropped = 0;

// Initialize lock-free event system
void events_lockfree_init(void) {
    // Initialize metro queue
    g_metro_lockfree_queue.header.write_idx = 0;
    g_metro_lockfree_queue.header.read_idx = 0;
    g_metro_lockfree_queue.header.size = LOCKFREE_QUEUE_SIZE;
    g_metro_lockfree_queue.header.mask = LOCKFREE_QUEUE_MASK;
    
    // Initialize input queue  
    g_input_lockfree_queue.header.write_idx = 0;
    g_input_lockfree_queue.header.read_idx = 0;
    g_input_lockfree_queue.header.size = LOCKFREE_QUEUE_SIZE;
    g_input_lockfree_queue.header.mask = LOCKFREE_QUEUE_MASK;
    
    // Initialize clock queue
    g_clock_lockfree_queue.header.write_idx = 0;
    g_clock_lockfree_queue.header.read_idx = 0;
    g_clock_lockfree_queue.header.size = CLOCK_QUEUE_SIZE;
    g_clock_lockfree_queue.header.mask = CLOCK_QUEUE_MASK;
    
    // Initialize ASL done queue
    g_asl_done_lockfree_queue.header.write_idx = 0;
    g_asl_done_lockfree_queue.header.read_idx = 0;
    g_asl_done_lockfree_queue.header.size = LOCKFREE_QUEUE_SIZE;
    g_asl_done_lockfree_queue.header.mask = LOCKFREE_QUEUE_MASK;
    
    // Reset statistics
    metro_events_posted = 0;
    metro_events_processed = 0;
    metro_events_dropped = 0;
    metro_events_coalesced = 0;
    input_events_posted = 0;
    input_events_processed = 0;
    input_events_dropped = 0;
    clock_events_posted = 0;
    clock_events_processed = 0;
    clock_events_dropped = 0;
    clock_events_coalesced = 0;
    asl_done_events_posted = 0;
    asl_done_events_processed = 0;
    asl_done_events_dropped = 0;
    
    DEBUG_LF_PRINT("Lock-free event queues initialized (metro=%d, input=%d, clock=%d slots)\n", 
                   LOCKFREE_QUEUE_SIZE, LOCKFREE_QUEUE_SIZE, CLOCK_QUEUE_SIZE);
}

// Clear all queues (for reset)
void events_lockfree_clear(void) {
    // Reset indices to effectively clear the queues
    // Note: This is safe only if we are sure no producer is writing, 
    // or if we accept that we might lose a concurrent write.
    // In crow.reset(), we are resetting the world, so losing events is desired.
    
    g_metro_lockfree_queue.header.read_idx = g_metro_lockfree_queue.header.write_idx;
    g_input_lockfree_queue.header.read_idx = g_input_lockfree_queue.header.write_idx;
    g_clock_lockfree_queue.header.read_idx = g_clock_lockfree_queue.header.write_idx;
    g_asl_done_lockfree_queue.header.read_idx = g_asl_done_lockfree_queue.header.write_idx;
}

// Metro queue functions

// Post metro event from Core 1 (audio) - NEVER BLOCKS!
bool __not_in_flash_func(metro_lockfree_post)(int metro_id, int stage) {
    metro_lockfree_queue_t* queue = &g_metro_lockfree_queue;
    
    // Load current write index
    uint32_t current_write = queue->header.write_idx;
    uint32_t next_write = (current_write + 1) & queue->header.mask;
    
    // Check if queue has space (leave one slot empty to distinguish full from empty)
    if (next_write == queue->header.read_idx) {
        // Queue full - avoid backlog by overwriting the newest pending event for this metro.
        // This is only used under overload; steady-state behavior is unchanged.
        uint32_t read_idx = queue->header.read_idx;
        uint32_t pos = read_idx;
        bool found = false;
        uint32_t found_pos = 0;
        while (pos != current_write) {
            if (queue->events[pos].metro_id == metro_id) {
                found = true;
                found_pos = pos; // keep last match
            }
            pos = (pos + 1) & queue->header.mask;
        }
        if (found) {
            // Write timestamp first, then stage last (best-effort coherence)
            queue->events[found_pos].timestamp_us = time_us_32();
            DMB();
            queue->events[found_pos].stage = stage;
            DMB();

            metro_events_posted++;
            metro_events_coalesced++;
            return true;
        }

        // No matching pending event to overwrite: drop (don't block audio core!)
        metro_events_dropped++;
        return false;
    }
    
    // Store event data
    queue->events[current_write].metro_id = metro_id;
    queue->events[current_write].stage = stage;
    queue->events[current_write].timestamp_us = time_us_32();
    
    // Memory barrier - ensure event data is written before updating index
    DMB();
    
    // Commit the write (this makes the event visible to consumer)
    queue->header.write_idx = next_write;
    
    metro_events_posted++;
    return true;
}

// Clock queue functions

bool __not_in_flash_func(clock_lockfree_post)(int coro_id) {
    clock_lockfree_queue_t* queue = &g_clock_lockfree_queue;
    uint32_t current_write = queue->header.write_idx;
    uint32_t next_write = (current_write + 1) & queue->header.mask;
    if (next_write == queue->header.read_idx) {
        // Queue full - overwrite newest pending resume for this coroutine.
        uint32_t read_idx = queue->header.read_idx;
        uint32_t pos = read_idx;
        bool found = false;
        uint32_t found_pos = 0;
        while (pos != current_write) {
            if (queue->events[pos].coro_id == coro_id) {
                found = true;
                found_pos = pos;
            }
            pos = (pos + 1) & queue->header.mask;
        }
        if (found) {
            queue->events[found_pos].timestamp_us = time_us_32();
            DMB();
            // coro_id is unchanged (same coro), only timestamp updates
            DMB();
            clock_events_posted++;
            clock_events_coalesced++;
            return true;
        }
        clock_events_dropped++;
        return false;
    }

    queue->events[current_write].coro_id = coro_id;
    queue->events[current_write].timestamp_us = time_us_32();

    DMB();
    queue->header.write_idx = next_write;
    clock_events_posted++;
    return true;
}

bool clock_lockfree_get(clock_event_lockfree_t* event) {
    clock_lockfree_queue_t* queue = &g_clock_lockfree_queue;
    uint32_t current_read = queue->header.read_idx;
    if (current_read == queue->header.write_idx) {
        return false;
    }

    *event = queue->events[current_read];

    DMB();
    uint32_t next_read = (current_read + 1) & queue->header.mask;
    queue->header.read_idx = next_read;
    clock_events_processed++;
    return true;
}

// Peek at next clock event without removing it (single-consumer safe)
bool clock_lockfree_peek(clock_event_lockfree_t* event) {
    clock_lockfree_queue_t* queue = &g_clock_lockfree_queue;
    uint32_t current_read = queue->header.read_idx;
    if (current_read == queue->header.write_idx) {
        return false;
    }
    *event = queue->events[current_read];
    return true;
}

uint32_t clock_lockfree_queue_depth(void) {
    clock_lockfree_queue_t* queue = &g_clock_lockfree_queue;
    uint32_t write_idx = queue->header.write_idx;
    uint32_t read_idx = queue->header.read_idx;
    if (write_idx >= read_idx) {
        return write_idx - read_idx;
    } else {
        return (queue->header.size - read_idx) + write_idx;
    }
}

// Clock stats accessors
uint32_t clock_events_posted_count(void)   { return clock_events_posted; }
uint32_t clock_events_processed_count(void){ return clock_events_processed; }
uint32_t clock_events_dropped_count(void)  { return clock_events_dropped; }
uint32_t clock_events_coalesced_count(void){ return clock_events_coalesced; }
void clock_lockfree_reset_stats(void) {
    clock_events_posted = 0;
    clock_events_processed = 0;
    clock_events_dropped = 0;
    clock_events_coalesced = 0;
}

// Reset all queue stats
void events_lockfree_reset_stats(void) {
    metro_events_posted = metro_events_processed = metro_events_dropped = 0;
    metro_events_coalesced = 0;
    input_events_posted = input_events_processed = input_events_dropped = 0;
    clock_events_posted = clock_events_processed = clock_events_dropped = 0;
    clock_events_coalesced = 0;
    asl_done_events_posted = asl_done_events_processed = asl_done_events_dropped = 0;
}

// Metro stats accessors
uint32_t metro_events_posted_count(void)    { return metro_events_posted; }
uint32_t metro_events_processed_count(void) { return metro_events_processed; }
uint32_t metro_events_dropped_count(void)   { return metro_events_dropped; }
uint32_t metro_events_coalesced_count(void) { return metro_events_coalesced; }

// Input stats accessors
uint32_t input_events_posted_count(void)    { return input_events_posted; }
uint32_t input_events_processed_count(void) { return input_events_processed; }
uint32_t input_events_dropped_count(void)   { return input_events_dropped; }

// ASL done stats accessors
uint32_t asl_done_events_posted_count(void)    { return asl_done_events_posted; }
uint32_t asl_done_events_processed_count(void) { return asl_done_events_processed; }
uint32_t asl_done_events_dropped_count(void)   { return asl_done_events_dropped; }

// Get metro event from Core 0 (control) - NEVER BLOCKS!
bool metro_lockfree_get(metro_event_lockfree_t* event) {
    metro_lockfree_queue_t* queue = &g_metro_lockfree_queue;
    
    // Load current read index  
    uint32_t current_read = queue->header.read_idx;
    
    // Check if queue has data
    if (current_read == queue->header.write_idx) {
        // Queue empty
        return false;
    }
    
    // Copy event data
    *event = queue->events[current_read];
    
    // Memory barrier - ensure event data is read before updating index
    DMB();
    
    // Commit the read (this frees the slot for producer)
    uint32_t next_read = (current_read + 1) & queue->header.mask;
    queue->header.read_idx = next_read;
    
    metro_events_processed++;
    return true;
}

// Peek at next metro event without removing it (single-consumer safe)
bool metro_lockfree_peek(metro_event_lockfree_t* event) {
    metro_lockfree_queue_t* queue = &g_metro_lockfree_queue;
    uint32_t current_read = queue->header.read_idx;
    if (current_read == queue->header.write_idx) {
        return false; // empty
    }
    *event = queue->events[current_read];
    return true;
}

// Get metro queue depth (for monitoring)
uint32_t metro_lockfree_queue_depth(void) {
    metro_lockfree_queue_t* queue = &g_metro_lockfree_queue;
    uint32_t write_idx = queue->header.write_idx;
    uint32_t read_idx = queue->header.read_idx;
    
    if (write_idx >= read_idx) {
        return write_idx - read_idx;
    } else {
        return (queue->header.size - read_idx) + write_idx;
    }
}

// Input queue functions

// Post input detection event from Core 1 (audio) - NEVER BLOCKS!
bool input_lockfree_post(int channel, float value, int detection_type) {
    input_lockfree_queue_t* queue = &g_input_lockfree_queue;
    
    // Load current write index
    uint32_t current_write = queue->header.write_idx;
    uint32_t next_write = (current_write + 1) & queue->header.mask;
    
    // Check if queue has space
    if (next_write == queue->header.read_idx) {
        // Queue full - drop event (don't block audio core!)
        input_events_dropped++;
        return false;
    }
    
    // Store event data
    queue->events[current_write].channel = channel;
    queue->events[current_write].value = value;
    queue->events[current_write].detection_type = detection_type;
    queue->events[current_write].timestamp_us = time_us_32();
    // Note: extra union is left uninitialized for simple modes
    
    // Memory barrier - ensure event data is written before updating index
    DMB();
    
    // Commit the write
    queue->header.write_idx = next_write;
    
    input_events_posted++;
    return true;
}

// Post extended input detection event with extra data (for scale mode) - NEVER BLOCKS!
bool input_lockfree_post_extended(const input_event_lockfree_t* event) {
    input_lockfree_queue_t* queue = &g_input_lockfree_queue;
    
    // Load current write index
    uint32_t current_write = queue->header.write_idx;
    uint32_t next_write = (current_write + 1) & queue->header.mask;
    
    // Check if queue has space
    if (next_write == queue->header.read_idx) {
        // Queue full - drop event (don't block audio core!)
        input_events_dropped++;
        return false;
    }
    
    // Copy full event data (including extra union)
    queue->events[current_write] = *event;
    
    // Memory barrier - ensure event data is written before updating index
    DMB();
    
    // Commit the write
    queue->header.write_idx = next_write;
    
    input_events_posted++;
    return true;
}

// Get input detection event from Core 0 (control) - NEVER BLOCKS!
bool input_lockfree_get(input_event_lockfree_t* event) {
    input_lockfree_queue_t* queue = &g_input_lockfree_queue;
    
    // Load current read index
    uint32_t current_read = queue->header.read_idx;
    
    // Check if queue has data
    if (current_read == queue->header.write_idx) {
        // Queue empty
        return false;
    }
    
    // Copy event data
    *event = queue->events[current_read];
    
    // Memory barrier - ensure event data is read before updating index
    DMB();
    
    // Commit the read
    uint32_t next_read = (current_read + 1) & queue->header.mask;
    queue->header.read_idx = next_read;
    
    input_events_processed++;
    return true;
}

// Get input queue depth (for monitoring)
uint32_t input_lockfree_queue_depth(void) {
    input_lockfree_queue_t* queue = &g_input_lockfree_queue;
    uint32_t write_idx = queue->header.write_idx;
    uint32_t read_idx = queue->header.read_idx;
    
    if (write_idx >= read_idx) {
        return write_idx - read_idx;
    } else {
        return (queue->header.size - read_idx) + write_idx;
    }
}

// ASL done queue functions

// Post ASL done event from Core 1 (audio callback) - NEVER BLOCKS!
bool asl_done_lockfree_post(int channel) {
    asl_done_lockfree_queue_t* queue = &g_asl_done_lockfree_queue;
    
    // Load current write index
    uint32_t current_write = queue->header.write_idx;
    uint32_t next_write = (current_write + 1) & queue->header.mask;
    
    // Check if queue has space
    if (next_write == queue->header.read_idx) {
        // Queue full - drop event (don't block audio core!)
        asl_done_events_dropped++;
        return false;
    }
    
    // Store event data
    queue->events[current_write].channel = channel;
    queue->events[current_write].timestamp_us = time_us_32();
    
    // Memory barrier - ensure event data is written before updating index
    DMB();
    
    // Commit the write
    queue->header.write_idx = next_write;
    
    asl_done_events_posted++;
    return true;
}

// Get ASL done event from Core 0 (control) - NEVER BLOCKS!
bool asl_done_lockfree_get(asl_done_event_lockfree_t* event) {
    asl_done_lockfree_queue_t* queue = &g_asl_done_lockfree_queue;
    
    // Load current read index
    uint32_t current_read = queue->header.read_idx;
    
    // Check if queue has data
    if (current_read == queue->header.write_idx) {
        // Queue empty
        return false;
    }
    
    // Copy event data
    *event = queue->events[current_read];
    
    // Memory barrier - ensure event data is read before updating index
    DMB();
    
    // Commit the read
    uint32_t next_read = (current_read + 1) & queue->header.mask;
    queue->header.read_idx = next_read;
    
    asl_done_events_processed++;
    return true;
}

// Get ASL done queue depth (for monitoring)
uint32_t asl_done_lockfree_queue_depth(void) {
    asl_done_lockfree_queue_t* queue = &g_asl_done_lockfree_queue;
    uint32_t write_idx = queue->header.write_idx;
    uint32_t read_idx = queue->header.read_idx;
    
    if (write_idx >= read_idx) {
        return write_idx - read_idx;
    } else {
        return (queue->header.size - read_idx) + write_idx;
    }
}

// Statistics and monitoring

void events_lockfree_print_stats(void) {
    DEBUG_LF_PRINT("=== LOCK-FREE EVENT QUEUE STATISTICS ===\n");
    DEBUG_LF_PRINT("Metro Queue: depth=%lu/%d\n", metro_lockfree_queue_depth(), LOCKFREE_QUEUE_SIZE);
    DEBUG_LF_PRINT("  Posted: %lu, Processed: %lu, Dropped: %lu\n", 
                   metro_events_posted, metro_events_processed, metro_events_dropped);
    DEBUG_LF_PRINT("  Coalesced(on overflow): %lu\n", metro_events_coalesced);
    DEBUG_LF_PRINT("Clock Queue: depth=%lu/%d\n", clock_lockfree_queue_depth(), CLOCK_QUEUE_SIZE);
    DEBUG_LF_PRINT("  Posted: %lu, Processed: %lu, Dropped: %lu\n", 
                   clock_events_posted, clock_events_processed, clock_events_dropped);
    DEBUG_LF_PRINT("  Coalesced(on overflow): %lu\n", clock_events_coalesced);
    DEBUG_LF_PRINT("Input Queue: depth=%lu/%d\n", input_lockfree_queue_depth(), LOCKFREE_QUEUE_SIZE);
    DEBUG_LF_PRINT("  Posted: %lu, Processed: %lu, Dropped: %lu\n", 
                   input_events_posted, input_events_processed, input_events_dropped);
    DEBUG_LF_PRINT("ASL Done Queue: depth=%lu/%d\n", asl_done_lockfree_queue_depth(), LOCKFREE_QUEUE_SIZE);
    DEBUG_LF_PRINT("  Posted: %lu, Processed: %lu, Dropped: %lu\n", 
                   asl_done_events_posted, asl_done_events_processed, asl_done_events_dropped);
    
    DEBUG_LF_PRINT("Health: Metro=%s, Clock=%s, Input=%s, ASL=%s\n", 
                   (metro_lockfree_queue_depth() < LOCKFREE_QUEUE_SIZE/2) ? "OK" : "OVERLOADED",
                   (clock_lockfree_queue_depth() < CLOCK_QUEUE_SIZE/2) ? "OK" : "OVERLOADED",
                   (input_lockfree_queue_depth() < LOCKFREE_QUEUE_SIZE/2) ? "OK" : "OVERLOADED",
                   (asl_done_lockfree_queue_depth() < LOCKFREE_QUEUE_SIZE/2) ? "OK" : "OVERLOADED");
    DEBUG_LF_PRINT("=======================================\n");
}

bool events_lockfree_are_healthy(void) {
        return (metro_lockfree_queue_depth() < LOCKFREE_QUEUE_SIZE * 3 / 4) &&
            (input_lockfree_queue_depth() < LOCKFREE_QUEUE_SIZE * 3 / 4) &&
            (clock_lockfree_queue_depth() < CLOCK_QUEUE_SIZE * 3 / 4) &&
            (asl_done_lockfree_queue_depth() < LOCKFREE_QUEUE_SIZE * 3 / 4) &&
           (metro_events_dropped == 0) &&
           (input_events_dropped == 0) &&
           (clock_events_dropped == 0) &&
           (asl_done_events_dropped == 0);
}
