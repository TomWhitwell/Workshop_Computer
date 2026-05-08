/*
Blackbird Crow Emulator version 1.1

A fully crow-compatible program card for the Music Thing Modular Workshop Computer.
It does everything crow can do and works with all (?) existing crow scripts. 
I have also added the bb namespace with some Blackbird-specific functionality:
 - audio-rate noise action with bb.noise()
 - use the knobs, switches, audio inputs & pulse i/o of the Workshop Computer
 (and for advanced users)
 - bb.asap function that runs as fast as possible in the control thread with no detection

Tested with druid, norns, MAX/MSP, pyserial- works with ANY serial host that sends compatible strings.

Written by Dune Desormeaux / @dessertplanet , 2025

Special thanks to:
- Tom Whitwell for the Workshop System
- Chris Johnson for the ComputerCard framework
- Brian Crabtree and Trent Gill (monome & Whimsical Raps) for the original crow and the open source crow firmware
- Zack Scholl (infinitedigits) for encouragement and proving lua can work on rp2040 with his midi-to-cv project
- Ben Regnier (Q*Ben) for extensive testing and feedback without which this would not have been possible

License is GPLv3 or later- see LICENSE file for details.
*/

#include "ComputerCard.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "tusb.h"

#include "lib/fastmath.h"
#include "class/cdc/cdc_device.h"
#include "lib/debug.h"
#include "lib/usb_lockfree.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdarg>

// Lua includes
extern "C" {
#include "lua/src/lua.h"
#include "lua/src/lualib.h"
#include "lua/src/lauxlib.h"
}

// Crow ASL includes
extern "C" {
#include "lib/ashapes.h"
#include "lib/slopes.h"
#include "lib/casl.h"
#include "lib/detect.h"
#include "lib/l_crowlib.h"
#include "lib/l_bootstrap.h"
#include "lib/ll_timers.h"
#include "lib/metro.h"
#include "lib/mailbox.h"
#include "lib/clock.h"
#include "lib/events_lockfree.h"
#include "lib/flash_storage.h"
#include "lib/caw.h"
#include "lib/sample_rate.h"
}

// Generated Lua bytecode headers - Core libraries 
#include "asl.h"
#include "asllib.h"
#include "output.h"
#include "input.h"
#include "metro.h"
#include "First.h"

// Crow ecosystem library headers
#include "calibrate.h"
#include "sequins.h"
#define public public_lua
#include "public.h"
#undef public
extern "C" {
extern const unsigned char clock[];
extern const unsigned int clock_len;
}
#include "quote.h"
#include "timeline.h"
#include "hotswap.h"

// ============================================================================
// GLOBAL VARIABLES
// ============================================================================

// ============================================================================
// HARDFAULT CAPTURE (GDB-friendly)
// Stores stacked registers and active vector, then BKPTs.
// NOTE: Avoids any USB/stdio calls in fault context.
// ============================================================================
extern "C" {
static volatile uint32_t g_hardfault_stack[8]; // r0,r1,r2,r3,r12,lr,pc,xpsr
static volatile uint32_t g_hardfault_lr = 0;
static volatile uint32_t g_hardfault_sp = 0;
static volatile uint32_t g_hardfault_icsr = 0;
}

extern "C" void hardfault_capture_c(uint32_t *sp) {
    g_hardfault_sp = (uint32_t)sp;
    // Active vector number in ICSR[8:0]
    g_hardfault_icsr = *((volatile uint32_t*)0xE000ED04);
    // Stacked registers
    g_hardfault_stack[0] = sp[0];  // r0
    g_hardfault_stack[1] = sp[1];  // r1
    g_hardfault_stack[2] = sp[2];  // r2
    g_hardfault_stack[3] = sp[3];  // r3
    g_hardfault_stack[4] = sp[4];  // r12
    g_hardfault_stack[5] = sp[5];  // lr
    g_hardfault_stack[6] = sp[6];  // pc
    g_hardfault_stack[7] = sp[7];  // xpsr
    __asm volatile("mov %0, lr" : "=r"(g_hardfault_lr));

    __asm volatile("bkpt #0");
    while (1) {
        __asm volatile("nop");
    }
}

extern "C" __attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile(
        "mov r0, sp\n"
        "b hardfault_capture_c\n"
    );
}

// CV Output & Input State
static volatile int32_t g_output_state_mv[4] = {0, 0, 0, 0};
static volatile int32_t g_input_state_q12[2] = {0, 0};
static volatile int16_t g_audioin_raw[2] = {0, 0};

// Stream-equivalent values: Always maintained for .volts queries (matches crow behavior)
static float g_input_stream_volts[2] = {0.0f, 0.0f};
static float g_audioin_stream_volts[2] = {0.0f, 0.0f};

// Pulse I/O State
static volatile bool g_pulse_out_state[2] = {false, false};

// Pulse output coroutine tracking (to cancel overlapping pulses)
static volatile uint32_t g_pulse_out_coro_id[2] = {0, 0};
static volatile uint32_t g_pulse_coro_counter = 1; // ID generator for pulse coroutines

// Pulse input detection mode: 0='none', 1='change', 2='clock'
static volatile int8_t g_pulsein_mode[2] = {0, 0};

// Pulse input direction filter: 0='both', 1='rising', -1='falling'
static volatile int8_t g_pulsein_direction[2] = {0, 0};

// Pulse input clock division (for clock mode)
static volatile float g_pulsein_clock_div[2] = {1.0f, 1.0f};

// CV input clock mode tracking (input[1]/input[2] via Detect clock mode)
// Used to automatically fall back to internal clock when the clock jack is disconnected.
static volatile bool g_input_clock_mode[2] = {false, false};
static volatile float g_input_clock_div[2] = {1.0f, 1.0f};

// LED pulse stretching counters (to make 10ms pulses visible at 100Hz update rate)
static volatile uint16_t g_pulse_led_stretch[2] = {0, 0};

// Clock tick coordination between audio ISR (Core1) and scheduler service
static volatile uint32_t g_clock_ticks_pending = 0;
static volatile uint32_t g_timer_ticks_pending = 0;

// Pulse input state tracking (read from hardware, cached for Lua access)
static volatile bool g_pulsein_state[2] = {false, false};

// Pulse input edge detection counters (set at ProcessSample rate, drained in main loop)
// Counters prevent loss of back-to-back edges between loop iterations.
static volatile uint8_t g_pulsein_edge_count[2] = {0, 0};
static volatile bool g_pulsein_edge_state[2] = {false, false};
static volatile uint16_t g_pulsein_edge_overflow_count[2] = {0, 0};
static volatile uint16_t g_pulsein_clock_overflow_count[2] = {0, 0};

// Change detection coalescing for CV inputs 1/2 (avoid lock-free queue backlog)
static volatile uint8_t g_change_pending[2] = {0, 0};
static volatile bool g_change_state[2] = {false, false};
static volatile uint16_t g_change_overflow_count[2] = {0, 0};
static bool g_change_callback_active[2] = {false, false};

// Reentrancy protection for pulsein callbacks (prevents crashes from fast clocks)
static volatile bool g_pulsein_callback_active[2] = {false, false};

// Clock edge pending counters (deferred from ISR to Core 0 to avoid FP math in ISR)
static volatile uint8_t g_pulsein_clock_edge_count[2] = {0, 0};

// ============================================================================
// AUDIO-RATE NOISE GENERATOR (ProcessSample rate)
// ============================================================================
// Noise generator state for audio-rate output
static volatile bool g_noise_active[4] = {false, false, false, false};
static volatile uint8_t g_noise_active_mask = 0;  // Bitmask for fast checking if ANY noise is active
static volatile int32_t g_noise_gain[4] = {0, 0, 0, 0};  // Gain in fixed-point (0-6000 mV)
static volatile uint32_t g_noise_lock_counter[4] = {0, 0, 0, 0};  // Prevent clearing noise for a few calls

// Fast xorshift32 PRNG state for audio-rate noise
static uint32_t g_noise_state = 0xDEADBEEF;

// Slope action callback queue (Core 1 → Core 0)
// When slopes complete and have action callbacks, Core 1 queues them here
// CRITICAL: Store callback pointer in queue to prevent race condition
// when rapid triggers start new slopes before callbacks are processed
#define SLOPE_ACTION_QUEUE_SIZE 64
typedef struct {
    uint8_t channel;
    Callback_t callback;
} slope_action_entry_t;
static volatile slope_action_entry_t g_slope_action_queue[SLOPE_ACTION_QUEUE_SIZE];
static volatile uint32_t g_slope_action_write_idx = 0;
static volatile uint32_t g_slope_action_read_idx = 0;
static volatile uint32_t g_slope_action_drop_count = 0;
static volatile uint32_t g_slope_action_null_callback_count = 0;

// Queue slope action callback from Core 1 (called in ISR)
// IMPORTANT: Callback pointer is captured here to avoid race with new S_toward calls
extern "C" void queue_slope_action_callback(int channel, Callback_t callback) {
    if (callback == NULL) {
        g_slope_action_null_callback_count++;
        return;  // Don't queue NULL callbacks
    }
    
    uint32_t next_write = (g_slope_action_write_idx + 1) % SLOPE_ACTION_QUEUE_SIZE;
    if (next_write != g_slope_action_read_idx) {
        g_slope_action_queue[g_slope_action_write_idx].channel = (uint8_t)channel;
        g_slope_action_queue[g_slope_action_write_idx].callback = callback;
        g_slope_action_write_idx = next_write;
    } else {
        // Queue full - track drops for diagnostic reporting
        g_slope_action_drop_count++;
    }
}

// Process slope action callbacks on Core 0 (called from MainControlLoop)
static void process_slope_action_callbacks() {
    // Report diagnostics if drops detected
    static uint32_t last_reported_drops = 0;
    static uint32_t last_reported_nulls = 0;
    if (g_slope_action_drop_count != last_reported_drops) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[SLOPE] Queue drops: %lu\n\r", (unsigned long)g_slope_action_drop_count);
        tud_cdc_write_str(msg);
        last_reported_drops = g_slope_action_drop_count;
    }
    if (g_slope_action_null_callback_count != last_reported_nulls) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[SLOPE] NULL callbacks: %lu\n\r", (unsigned long)g_slope_action_null_callback_count);
        tud_cdc_write_str(msg);
        last_reported_nulls = g_slope_action_null_callback_count;
    }

    // Report slope command queue drops (Core0->Core1)
    // static uint32_t last_reported_cmd_drops = 0;
    // uint32_t cmd_drops = S_get_cmd_drop_count();
    // if (cmd_drops != last_reported_cmd_drops) {
    //     char msg[64];
    //     snprintf(msg, sizeof(msg), "[SLOPE] Cmd drops: %lu\n\r", (unsigned long)cmd_drops);
    //     tud_cdc_write_str(msg);
    //     last_reported_cmd_drops = cmd_drops;
    // }

    
    while (g_slope_action_read_idx != g_slope_action_write_idx) {
        // Read volatile struct members individually to avoid copy issues
        uint32_t read_idx = g_slope_action_read_idx;
        uint8_t channel = g_slope_action_queue[read_idx].channel;
        Callback_t callback = g_slope_action_queue[read_idx].callback;
        g_slope_action_read_idx = (read_idx + 1) % SLOPE_ACTION_QUEUE_SIZE;
        
        // Execute the queued callback
        if (callback != NULL) {
            callback((int)channel);
        }
    }
}

// ============================================================================
// PERFORMANCE MONITORING
// ============================================================================
static constexpr float kProcessSampleRateHz = PROCESS_SAMPLE_RATE_HZ_DOUBLE;
static constexpr float kProcessSampleBudgetUs = 1000000.0f / kProcessSampleRateHz;  // ProcessSample() rate
static constexpr uint32_t kProcessSampleOverrunThresholdUs =
    static_cast<uint32_t>(kProcessSampleBudgetUs + 0.5);  // >=100% utilization (~100us)
static constexpr uint32_t kClockServiceRateHz = 1000;      // crow-compatible cadence
static constexpr uint32_t kTimerServiceRateHz = 1500;      // ~1.5kHz target (crow-compatible cadence)
static constexpr int kLuaGcStepSize = 2; // small incremental GC step per main loop
static constexpr uint32_t kMainLoopSoftBudgetUs = 8000; // soft budget per loop to reduce stalls
static constexpr int kMaxLogMessagesPerLoop = 4; // cap logs per loop to avoid large spikes
// ProcessSample (Core 1 audio thread) performance
static volatile bool g_performance_warning = false;
static volatile uint32_t g_worst_case_us = 0;
static volatile uint32_t g_overrun_count = 0;
// Suppress perf warnings during/after flash write/clear operations.
// These flows prompt the user to reset, so the warning is noise.
static volatile uint32_t g_suppress_perf_warnings_until_us = 0;
// Keep all LEDs lit for a while after operations that ask the user to reset.
static volatile uint32_t g_force_all_leds_on_until_us = 0;
static volatile bool g_force_all_leds_armed = false;
static constexpr uint32_t kResetIndicatorHoldUs = 30000000u; // 30s
static constexpr uint32_t kResetIndicatorBlinkHalfPeriodUs = 1000000u; // 1s on, 1s off => 0.5 Hz

static inline bool u32_time_before(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}

// MainControlLoop (Core 0) performance
static volatile uint32_t g_loop_worst_case_us = 0;
static volatile uint32_t g_loop_iteration_count = 0;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Fast audio-rate noise generation in ISR - NO FLOATING POINT
// Returns noise value in millivolts (-6000 to +6000)
static inline int32_t generate_audio_noise_mv(int32_t gain_mv) {
    // xorshift32 algorithm - very fast PRNG (only shifts and XORs)
    g_noise_state ^= g_noise_state << 13;
    g_noise_state ^= g_noise_state >> 17;
    g_noise_state ^= g_noise_state << 5;
    
    // Extract 16-bit signed value from upper bits
    int16_t noise_16 = (int16_t)(g_noise_state >> 16);
    
    // Scale by gain using integer math: (noise_16 * gain_mv) / 32768
    // noise_16 is in range [-32768, 32767]
    // gain_mv is in range [0, 6000]
    // Result is in range [-6000, +6000] mV
    return ((int32_t)noise_16 * gain_mv) >> 15;  // Divide by 32768 using shift
}

static void set_output_state_simple(int channel, int32_t value_mv) {
    if (channel >= 0 && channel < 4) {
        g_output_state_mv[channel] = value_mv;
    }
}

extern "C" float get_input_state_simple(int channel) {
    if (channel >= 0 && channel < 2) {
        // Return stream-equivalent value (always maintained, matches crow .volts behavior)
        return g_input_stream_volts[channel];
    }
    return 0.0f;
}

static void set_input_state_simple(int channel, int16_t rawValue) {
    if (channel >= 0 && channel < 2) {
        g_input_state_q12[channel] = rawValue;
    }
}

// Update stream-equivalent values (called from main loop, not ISR)
// Mirror crow behavior: forward the latest smoothed ADC readings without extra gating
static void update_input_stream_values() {
    // Update CV inputs
    for (int ch = 0; ch < 2; ch++) {
        // Get current raw value
        float current_volts = (float) g_input_state_q12[ch] * (6.0f / 2047.0f);
        g_input_stream_volts[ch] = current_volts;
    }
    
    // Update audio inputs
    for (int ch = 0; ch < 2; ch++) {
        // Get current raw value
        float current_volts = (float) g_audioin_raw[ch] * (6.0f / 2047.0f);
        g_audioin_stream_volts[ch] = current_volts;
    }
}

// Store raw ADC value in ISR (no floating point)
static void set_audioin_raw(int channel, int16_t rawValue) {
    if (channel >= 0 && channel < 2) {
        g_audioin_raw[channel] = rawValue;
    }
}

// Convert to volts when accessed from Lua (outside ISR)
extern "C" float get_audioin_volts(int channel) {
    if (channel >= 0 && channel < 2) {
        // Return stream-equivalent value (always maintained, matches crow .volts behavior)
        return g_audioin_stream_volts[channel];
    }
    return 0.0f;
}

static int32_t get_output_state_simple(int channel) {
    if (channel >= 0 && channel < 4) {
        return g_output_state_mv[channel];
    }
    return 0;
}

// Forward declarations
static void public_update();

// Helper function to check if we have a complete packet (ends with \n or \r)
static bool is_packet_complete(const char* buffer, int length) {
    if (length == 0) return false;
    char last_char = buffer[length - 1];
    return (last_char == '\n' || last_char == '\r');
}

// Helper function to detect triple backticks (multi-line delimiter)
static inline bool check_for_backticks(const char* buffer, int pos) {
    // Need at least 3 chars in buffer
    if (pos < 3) return false;
    
    // Check if last 3 characters are backticks
    return (buffer[pos-3] == '`' && 
            buffer[pos-2] == '`' && 
            buffer[pos-1] == '`');
}

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

class BlackbirdCrow;
static volatile BlackbirdCrow* g_blackbird_instance = nullptr;
void core1_entry(); // defined after BlackbirdCrow - non-static so flash_storage can call it


// Forward declaration of C interface function (implemented after BlackbirdCrow class)
extern "C" void hardware_output_set_voltage(int channel, float voltage);
extern "C" void hardware_output_set_voltage_q16(int channel, q16_t voltage_q16);
extern "C" void hardware_pulse_output_set(int channel, bool state);

// Forward declaration for pulse input connection check (defined after BlackbirdCrow class)
static bool is_pulse_input_connected(int pulse_idx);
static void service_clock_from_core1(void);
static void service_timer_from_core1(void);
static void blackbird_core1_background_service(void);

// Chooses between internal and external clock based on clock-mode inputs and normalization probe connectivity.
// Implemented later in the file (after the card helpers are available).
static void auto_update_clock_source_from_connections(void);

static void service_clock_from_core1(void) {
    uint32_t ticks = 0;
    uint32_t irq_state = save_and_disable_interrupts();
    if (g_clock_ticks_pending) {
        ticks = g_clock_ticks_pending;
        g_clock_ticks_pending = 0;
    }
    restore_interrupts(irq_state);

    if (ticks == 0) {
        return;
    }

    for (uint32_t i = 0; i < ticks; i++) {
        uint32_t time_now_ms = to_ms_since_boot(get_absolute_time());
        clock_update(time_now_ms);
    }
}

static void service_timer_from_core1(void) {
    uint32_t ticks = 0;
    uint32_t irq_state = save_and_disable_interrupts();
    if (g_timer_ticks_pending) {
        ticks = g_timer_ticks_pending;
        g_timer_ticks_pending = 0;
    }
    restore_interrupts(irq_state);

    if (ticks == 0) {
        return;
    }

    for (uint32_t i = 0; i < ticks; i++) {
        Timer_Process();
    }
}

static void blackbird_core1_background_service(void) {
    S_slope_buffer_background_service();
    service_timer_from_core1();
    service_clock_from_core1();
}
static int bb_connected_index(lua_State* L);
extern "C" void L_handle_input_lockfree(input_event_lockfree_t* event);
extern "C" void L_handle_metro_lockfree(metro_event_lockfree_t* event);
extern "C" void L_handle_clock_resume_lockfree(clock_event_lockfree_t* event);

// Forward declaration of output batching functions
extern "C" void output_batch_begin();
extern "C" void output_batch_flush();
static void output_batch_queue(int channel, float volts);
static bool output_is_batching();

// ============================================================================
// MESSAGE QUEUE SYSTEM (Audio-Safe Printf Replacement)
// ============================================================================

class LuaManager;

#define MESSAGE_QUEUE_SIZE 32
#define MESSAGE_MAX_LENGTH 240

typedef struct {
    char message[MESSAGE_MAX_LENGTH];
    uint32_t timestamp;
    bool is_debug;
} queued_message_t;

static volatile queued_message_t g_message_queue[MESSAGE_QUEUE_SIZE];
static volatile uint32_t g_message_write_idx = 0;
static volatile uint32_t g_message_read_idx = 0;

// Audio-safe message queuing - replaces printf calls
static bool queue_message(bool is_debug, const char* fmt, ...) {
    // Get next write position
    uint32_t next_write = (g_message_write_idx + 1) % MESSAGE_QUEUE_SIZE;
    
    // Check for queue full
    if (next_write == g_message_read_idx) {
        return false; // Queue full, drop message
    }
    
    // Format message
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf((char*)g_message_queue[g_message_write_idx].message, 
                           MESSAGE_MAX_LENGTH, fmt, args);
    va_end(args);
    
    if (written < 0) {
        return false;
    }
    
    // Store metadata
    g_message_queue[g_message_write_idx].timestamp = to_ms_since_boot(get_absolute_time());
    g_message_queue[g_message_write_idx].is_debug = is_debug;
    
    // Update write index (atomic on single-core writes)
    g_message_write_idx = next_write;
    
    return true;
}

// Process queued messages on Core0
static void process_queued_messages_limited(int max_messages) {
    int processed = 0;
    while (g_message_read_idx != g_message_write_idx && processed < max_messages) {
        // Cast away volatile for local use
        const queued_message_t* msg = (const queued_message_t*)&g_message_queue[g_message_read_idx];

        // Output message
        printf("%s", msg->message);
        if (!strstr(msg->message, "\n") && !strstr(msg->message, "\r")) {
            printf("\n\r"); // Add line ending if not present
        }
        fflush(stdout);

        // Update read index
        g_message_read_idx = (g_message_read_idx + 1) % MESSAGE_QUEUE_SIZE;
        processed++;
    }
}

// Convenience macros for different message types
#define queue_user_message(fmt, ...) queue_message(false, fmt, ##__VA_ARGS__)
#define queue_debug_message(fmt, ...) queue_message(true, fmt, ##__VA_ARGS__)

static bool usb_log_printf(const char* fmt, ...) {
    char buffer[240];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written < 0) {
        return false;
    }
    if (!mailbox_send_response(buffer)) {
        queue_user_message("%s", buffer);
        return false;
    }
    return true;
}


// Audio input Lua C functions - similar to pulsein but simpler (read-only volts)
static int audioin_index(lua_State* L) {
    // Get the index from the table itself
    lua_getfield(L, 1, "_idx");
    int idx = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    const char* key = lua_tostring(L, 2);
    
    // Only support .volts property
    if (strcmp(key, "volts") == 0) {
        // Floating point conversion happens here, not in ISR
        float volts = get_audioin_volts(idx);
        lua_pushnumber(L, volts);
        return 1;
    } else if (strcmp(key, "_idx") == 0) {
        lua_pushinteger(L, idx);
        return 1;
    }
    
    lua_pushnil(L);
    return 1;
}

// Pulse input Lua C functions (must be at file scope, not in class)
// NO UPVALUES - get index from table's _idx field to avoid closure memory issues
static int pulsein_index(lua_State* L) {
    // Get the index from the table itself
    lua_getfield(L, 1, "_idx");
    int idx = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    const char* key = lua_tostring(L, 2);
    
    // First check real fields (but skip _idx)
    if (strcmp(key, "_idx") != 0) {
        lua_pushvalue(L, 2);
        lua_rawget(L, 1);
        if (!lua_isnil(L, -1)) {
            return 1;
        }
        lua_pop(L, 1);
    }
    
    // Synthesize dynamic properties
    if (strcmp(key, "state") == 0) {
        lua_pushboolean(L, g_pulsein_state[idx]);
        return 1;
    } else if (strcmp(key, "direction") == 0) {
        const char* dir = (g_pulsein_direction[idx] == 1) ? "rising" :
                          (g_pulsein_direction[idx] == -1) ? "falling" : "both";
        lua_pushstring(L, dir);
        return 1;
    } else if (strcmp(key, "change") == 0) {
        char callback_name[32];
        snprintf(callback_name, sizeof(callback_name), "_pulsein%d_change_callback", idx + 1);
        lua_getglobal(L, callback_name);
        return 1;
    } else if (strcmp(key, "mode") == 0) {
        const char* mode_str = (g_pulsein_mode[idx] == 2) ? "clock" :
                               (g_pulsein_mode[idx] == 1) ? "change" : "none";
        lua_pushstring(L, mode_str);
        return 1;
    } else if (strcmp(key, "division") == 0) {
        lua_pushnumber(L, g_pulsein_clock_div[idx]);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int pulsein_newindex(lua_State* L) {
    lua_getfield(L, 1, "_idx");
    int idx = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    const char* key = lua_tostring(L, 2);
    
    if (strcmp(key, "change") == 0) {
        if (lua_isfunction(L, 3) || lua_isnil(L, 3)) {
            char callback_name[32];
            snprintf(callback_name, sizeof(callback_name), "_pulsein%d_change_callback", idx + 1);
            lua_pushvalue(L, 3);
            lua_setglobal(L, callback_name);
        } else {
            return luaL_error(L, "pulsein.change must be a function or nil");
        }
        return 0;
    } else if (strcmp(key, "mode") == 0) {
        const char* mode = lua_tostring(L, 3);
        if (!mode) return luaL_error(L, "pulsein.mode must be a string");
        
        // Check if pulse input is connected for change/clock modes
        bool is_connected = is_pulse_input_connected(idx);
        
        if (strcmp(mode, "none") == 0) {
            g_pulsein_mode[idx] = 0;
        } else if (strcmp(mode, "change") == 0) {
            g_pulsein_mode[idx] = 1;
        } else if (strcmp(mode, "clock") == 0) {
            g_pulsein_mode[idx] = 2;
            // Clock mode only detects rising edges
            g_pulsein_direction[idx] = 1;
            // Set clock source to external (crow input)
            clock_set_source(CLOCK_SOURCE_CROW);
            clock_crow_in_div(g_pulsein_clock_div[idx]);
        } else {
            return luaL_error(L, "pulsein.mode must be 'none', 'change', or 'clock'");
        }
        
        if (!is_connected && g_pulsein_mode[idx] != 0) {
            printf("pulsein[%d]: mode '%s' enabled while jack not detected; waiting for connection\n\r", idx + 1, mode);
        }
        return 0;
    } else if (strcmp(key, "division") == 0) {
        float div = (float)lua_tonumber(L, 3);
        if (div <= 0) return luaL_error(L, "pulsein.division must be positive");
        g_pulsein_clock_div[idx] = div;
        // Update if currently in clock mode
        if (g_pulsein_mode[idx] == 2) {
            clock_crow_in_div(div);
        }
        return 0;
    } else if (strcmp(key, "direction") == 0) {
        const char* dir = lua_tostring(L, 3);
        if (!dir) return luaL_error(L, "pulsein.direction must be a string");
        if (strcmp(dir, "both") == 0) {
            g_pulsein_direction[idx] = 0;
        } else if (strcmp(dir, "rising") == 0) {
            g_pulsein_direction[idx] = 1;
        } else if (strcmp(dir, "falling") == 0) {
            g_pulsein_direction[idx] = -1;
        } else {
            return luaL_error(L, "pulsein.direction must be 'both', 'rising', or 'falling'");
        }
        return 0;
    } else if (strcmp(key, "state") == 0) {
        return luaL_error(L, "pulsein.state is read-only");
    }
    return 0;
}

// __call metamethod for bb.pulsein[n]{mode='change', direction='rising'}
static int pulsein_call(lua_State* L) {
    lua_getfield(L, 1, "_idx");
    int idx = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    // Argument should be a table
    if (!lua_istable(L, 2)) {
        return luaL_error(L, "pulsein() expects a table argument");
    }
    
    // Process mode field
    lua_getfield(L, 2, "mode");
    if (lua_isstring(L, -1)) {
        const char* mode = lua_tostring(L, -1);
        if (strcmp(mode, "none") == 0) {
            g_pulsein_mode[idx] = 0;
        } else if (strcmp(mode, "change") == 0) {
            g_pulsein_mode[idx] = 1;
        } else if (strcmp(mode, "clock") == 0) {
            g_pulsein_mode[idx] = 2;
            g_pulsein_direction[idx] = 1;  // Clock always uses rising edges
            clock_set_source(CLOCK_SOURCE_CROW);
            clock_crow_in_div(g_pulsein_clock_div[idx]);
        } else {
            return luaL_error(L, "pulsein mode must be 'none', 'change', or 'clock'");
        }
        
        if (!is_pulse_input_connected(idx) && g_pulsein_mode[idx] != 0) {
            printf("pulsein[%d]: mode '%s' enabled while jack not detected; waiting for connection\n\r", idx + 1, mode);
        }
    }
    lua_pop(L, 1);
    
    // Process division field (for clock mode)
    lua_getfield(L, 2, "division");
    if (lua_isnumber(L, -1)) {
        float div = (float)lua_tonumber(L, -1);
        if (div > 0) {
            g_pulsein_clock_div[idx] = div;
            if (g_pulsein_mode[idx] == 2) {
                clock_crow_in_div(div);
            }
        }
    }
    lua_pop(L, 1);
    
    // Process direction field
    lua_getfield(L, 2, "direction");
    if (lua_isstring(L, -1)) {
        const char* dir = lua_tostring(L, -1);
        if (strcmp(dir, "both") == 0) {
            g_pulsein_direction[idx] = 0;
        } else if (strcmp(dir, "rising") == 0) {
            g_pulsein_direction[idx] = 1;
        } else if (strcmp(dir, "falling") == 0) {
            g_pulsein_direction[idx] = -1;
        } else {
            return luaL_error(L, "pulsein direction must be 'both', 'rising', or 'falling'");
        }
    }
    lua_pop(L, 1);
    
    return 0;
}

// ============================================================================
// BLACKBIRD-SPECIFIC LUA API (bb namespace)
// ============================================================================

// Pulse Output Functions (bb.pulseout[1] and bb.pulseout[2])
// ----------------------------------------------------------------------------

static bool find_pulse_time_in_branch(lua_State* L, int branch_index, float* out_time) {
    int abs_index = lua_absindex(L, branch_index);
    if (!lua_istable(L, abs_index)) {
        return false;
    }

    bool found_any = false;
    int len = (int)luaL_len(L, abs_index);
    for (int i = 1; i <= len; i++) {
        lua_geti(L, abs_index, i);
        bool entry_popped = false;
        if (lua_istable(L, -1)) {
            lua_geti(L, -1, 1);
            const char* tag = lua_tostring(L, -1);
            lua_pop(L, 1);

            if (tag && (strcmp(tag, "TO") == 0 || strcmp(tag, "to") == 0)) {
                lua_geti(L, -1, 3);
                if (lua_isnumber(L, -1)) {
                    float candidate = (float)lua_tonumber(L, -1);
                    lua_pop(L, 1);  // pop time
                    if (candidate > 0.0001f) {
                        *out_time = candidate;
                        lua_pop(L, 1);  // pop entry table
                        return true;
                    } else {
                        *out_time = candidate;
                        found_any = true;
                        lua_pop(L, 1);  // pop entry table after recording fallback
                        entry_popped = true;
                        continue;
                    }
                } else {
                    lua_pop(L, 1);  // pop non-number
                }
            } else {
                if (find_pulse_time_in_branch(L, -1, out_time)) {
                    lua_pop(L, 1);  // pop entry table from recursive call
                    if (*out_time > 0.0001f) {
                        return true;
                    }
                    found_any = true;
                    entry_popped = true;
                }
            }
        }

        if (!entry_popped) {
            lua_pop(L, 1);  // pop entry table or non-table value
        }
    }
    return found_any;
}

static float extract_pulse_time_from_action(lua_State* L, int action_index) {
    float pulse_time = 0.010f;  // default 10ms if extraction fails
    int abs_index = lua_absindex(L, action_index);
    if (!lua_istable(L, abs_index)) {
        return pulse_time;
    }

    bool found_any = false;
    int len = (int)luaL_len(L, abs_index);
    for (int i = 1; i <= len; i++) {
        lua_geti(L, abs_index, i);
        if (lua_istable(L, -1)) {
            if (find_pulse_time_in_branch(L, -1, &pulse_time)) {
                lua_pop(L, 1);
                if (pulse_time > 0.0001f) {
                    return pulse_time;
                }
                found_any = true;
                continue;
            }
        }
        lua_pop(L, 1);
    }
    return found_any ? pulse_time : 0.010f;
}

// __index metamethod for bb.pulseout[n].action, bb.pulseout[n].state, bb.pulseout[n].clock
static int pulseout_index(lua_State* L) {
    lua_getfield(L, 1, "_idx");
    int idx = lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    const char* key = luaL_checkstring(L, 2);
    
    if (strcmp(key, "action") == 0) {
        // Return the stored action
        lua_getfield(L, 1, "_action");
        return 1;
    } else if (strcmp(key, "state") == 0) {
        // Return current pulse output state
        lua_pushboolean(L, g_pulse_out_state[idx]);
        return 1;
    } else if (strcmp(key, "_idx") == 0) {
        lua_pushinteger(L, idx);
        return 1;
    }
    
    // For other keys (like "clock"), check if there's a method in the metatable
    if (lua_getmetatable(L, 1)) {  // Get metatable
        lua_pushvalue(L, 2);  // Push the key
        lua_rawget(L, -2);  // Get metatable[key]
        if (!lua_isnil(L, -1)) {
            lua_remove(L, -2);  // Remove metatable, keep value
            return 1;
        }
        lua_pop(L, 2);  // Pop nil and metatable
    }
    
    lua_pushnil(L);
    return 1;
}

// __newindex metamethod for bb.pulseout[n].action = ...
static int pulseout_newindex(lua_State* L) {
    lua_getfield(L, 1, "_idx");
    int idx = lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    const char* key = luaL_checkstring(L, 2);
    
    if (strcmp(key, "action") == 0) {
        // Check if user is clearing the action with 'none'
        if (lua_isstring(L, 3)) {
            const char* str = lua_tostring(L, 3);
            if (strcmp(str, "none") == 0) {
                // Clear the action
                lua_pushstring(L, "_action");
                lua_pushnil(L);
                lua_rawset(L, 1);  // Use rawset to bypass metamethod
                // Also ensure output is low
                g_pulse_out_state[idx] = false;
                hardware_pulse_output_set(idx + 1, false);  // Use 1-based channel numbering
                return 0;
            }
        }
        
        // User is setting a custom action
        // Store the action in the table using rawset to bypass metamethod
        lua_pushstring(L, "_action");
        lua_pushvalue(L, 3);
        lua_rawset(L, 1);
        return 0;
    } else if (strcmp(key, "clock_div") == 0 || strcmp(key, "ckcoro") == 0) {
        // Allow setting clock-related fields (used by :clock() method)
        lua_pushvalue(L, 2);
        lua_pushvalue(L, 3);
        lua_rawset(L, 1);
        return 0;
    }
    
    return luaL_error(L, "pulseout: cannot set field '%s'", key);
}

// __call metamethod for bb.pulseout[n]() or bb.pulseout[n]('none') or bb.pulseout[n](action)
static int pulseout_call(lua_State* L) {
    lua_getfield(L, 1, "_idx");
    int idx = lua_tointeger(L, -1);
    lua_pop(L, 1);
    
    // If an argument is provided, set it as the action (like bb.pulseout[1]('none'))
    if (lua_gettop(L) >= 2) {
        // Check if it's 'none' string
        if (lua_isstring(L, 2)) {
            const char* str = lua_tostring(L, 2);
            if (strcmp(str, "none") == 0) {
                // Clear the action
                lua_pushstring(L, "_action");
                lua_pushnil(L);
                lua_rawset(L, 1);
                // Set output low
                g_pulse_out_state[idx] = false;
                hardware_pulse_output_set(idx + 1, false);
                return 0;
            }
        }
        
        // Otherwise, set the provided value as the action AND execute it immediately
        lua_pushstring(L, "_action");
        lua_pushvalue(L, 2);
        lua_rawset(L, 1);
        
        // Now execute the action we just stored
        // Execute via _c.tell to route through proper pulse output handling
        lua_getglobal(L, "_c");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "tell");
            if (lua_isfunction(L, -1)) {
                lua_pushstring(L, "output");
                lua_pushinteger(L, idx + 3);  // Pulse outputs are logical channels 3 & 4
                lua_pushvalue(L, 2);  // Push the action argument
                
                if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
                    const char* error = lua_tostring(L, -1);
                    if (tud_cdc_connected() && error) {
                        tud_cdc_write_str("pulseout error: ");
                        tud_cdc_write_str(error);
                        tud_cdc_write_str("\n\r");
                    }
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);  // pop non-function
            }
            lua_pop(L, 1);  // pop _c
        } else {
            lua_pop(L, 1);  // pop non-table
        }
        
        return 0;
    }
    
    // No argument - execute the current action
    lua_getfield(L, 1, "_action");
    
    // If no action is set, silently do nothing (matches output[n]() behavior)
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return 0;
    }
    
    if (lua_istable(L, -1)) {
        // It's an ASL action table - execute via _c.tell
        lua_getglobal(L, "_c");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "tell");
            if (lua_isfunction(L, -1)) {
                lua_pushstring(L, "output");
                lua_pushinteger(L, idx + 3);  // Pulse outputs are logical channels 3 & 4
                lua_pushvalue(L, -5);  // Push the action table
                if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
                    const char* error = lua_tostring(L, -1);
                    if (tud_cdc_connected() && error) {
                        tud_cdc_write_str("pulseout error: ");
                        tud_cdc_write_str(error);
                        tud_cdc_write_str("\n\r");
                    }
                    lua_pop(L, 1);
                }
            } else {
                lua_pop(L, 1);  // pop non-function
            }
            lua_pop(L, 1);  // pop _c
        } else {
            lua_pop(L, 1);  // pop non-table
        }
        lua_pop(L, 1);  // pop _action
        return 0;
    } else if (lua_isfunction(L, -1)) {
        // It's a function - call it directly
        lua_call(L, 0, 0);
        return 0;
    }
    
    lua_pop(L, 1);
    return 0;
}

// ============================================================================
// LUA MANAGER CLASS
// ============================================================================

// External C function from l_crowlib.c
extern "C" {
    int l_crowlib_crow_reset(lua_State* L);
}

class LuaManager {
public:
    lua_State* L;  // Made public for direct access in main.cpp
private:
    static LuaManager* instance;
    
    
    // Lua print function - sends output to serial
    static int lua_print(lua_State* L) {
        if (!tud_cdc_connected()) return 0;
        
        int n = lua_gettop(L);  // number of arguments
        lua_getglobal(L, "tostring");
        for (int i = 1; i <= n; i++) {
            lua_pushvalue(L, -1);  // function to be called
            lua_pushvalue(L, i);   // value to print
            lua_call(L, 1, 1);
            const char* s = lua_tostring(L, -1);  // get result
            if (s != NULL) {
                if (i > 1) tud_cdc_write_char('\t');
                tud_cdc_write_str(s);
            }
            lua_pop(L, 1);  // pop result
        }
        tud_cdc_write("\n\r",2);
        return 0;
    }
    
    // Lua time function - returns milliseconds since boot
    static int lua_time(lua_State* L) {
        uint32_t time_ms = to_ms_since_boot(get_absolute_time());
        lua_pushnumber(L, time_ms / 1000.0); // crow returns seconds
        return 1;
    }
    
    // Forward declaration - implementation after BlackbirdCrow class
    static int lua_unique_card_id(lua_State* L);
    static int lua_unique_id(lua_State* L);
    static int lua_memstats(lua_State* L);
    static int lua_perf_stats(lua_State* L);
    static int lua_clock_stats(lua_State* L);
    static int lua_pub_view_in(lua_State* L);
    static int lua_pub_view_out(lua_State* L);
    static int lua_tell(lua_State* L);
    static int lua_get_out(lua_State* L);
    static int lua_get_cv(lua_State* L);
    static int lua_hardware_pulse(lua_State* L);
    static int lua_pulse_coro_check(lua_State* L);
    
    // Lua tab.print function - pretty print tables
    static int lua_tab_print(lua_State* L) {
        if (!tud_cdc_connected()) return 0;  // Add CDC connection check
        
        if (lua_gettop(L) != 1) {
            lua_pushstring(L, "tab.print expects exactly one argument");
            lua_error(L);
        }
        
        print_table_recursive(L, 1, 0);
        tud_cdc_write_str("\n\r");  // Use CDC write instead of printf
        return 0;
    }
    
// Helper to flush CDC buffer if it's getting full
// Flush when less than 64 bytes available (keep a safety margin)
static inline void flush_if_needed() {
    if (tud_cdc_write_available() < 64) {
        tud_cdc_write_flush();
        // Give USB stack time to transmit - wait until buffer has space
        uint32_t timeout = 0;
        while (tud_cdc_write_available() < 128 && timeout < 10000) {
            sleep_us(10);
            tud_task();  // Process USB tasks
            timeout++;
        }
    }
}

// Helper function to recursively print table contents
static void print_table_recursive(lua_State* L, int index, int depth) {
    if (!lua_istable(L, index)) {
        // Not a table, just print the value
        lua_getglobal(L, "tostring");
        lua_pushvalue(L, index);
        lua_call(L, 1, 1);
        const char* s = lua_tostring(L, -1);
        if (s) tud_cdc_write_str(s);  // Use CDC write instead of printf
        lua_pop(L, 1);
        flush_if_needed();  // Check buffer after writing value
        return;
    }
    
    tud_cdc_write_str("{\n\r");  // Use CDC write and proper line endings
    flush_if_needed();  // Check buffer after opening brace
    
    // Iterate through table
    lua_pushnil(L);  // first key
    while (lua_next(L, index) != 0) {
        // Print indentation
        for (int i = 0; i < depth + 1; i++) tud_cdc_write_str("  ");
        
        // Print key
        if (lua_type(L, -2) == LUA_TSTRING) {
            tud_cdc_write_str(lua_tostring(L, -2));
            tud_cdc_write_str(" = ");
        } else if (lua_type(L, -2) == LUA_TNUMBER) {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "[%.0f] = ", lua_tonumber(L, -2));
            tud_cdc_write_str(buffer);
        } else {
            tud_cdc_write_str("[?] = ");
        }
        
        // Print value
        if (lua_istable(L, -1) && depth < 3) {  // Limit recursion depth
            print_table_recursive(L, lua_gettop(L), depth + 1);
        } else {
            lua_getglobal(L, "tostring");
            lua_pushvalue(L, -2);  // Push the value below tostring
            lua_call(L, 1, 1);
            const char* s = lua_tostring(L, -1);
            if (s) tud_cdc_write_str(s);
            lua_pop(L, 1);
        }
        
        tud_cdc_write_str(",\n\r");  // Use proper line endings
        flush_if_needed();  // Check buffer after each entry
        lua_pop(L, 1);  // remove value, keep key for next iteration
    }
    
    // Print closing brace with proper indentation
    for (int i = 0; i < depth; i++) tud_cdc_write_str("  ");
    tud_cdc_write_str("}");
    flush_if_needed();  // Check buffer after closing brace
}    // Lua panic handler - called when Lua encounters an unrecoverable error
    static int lua_panic_handler(lua_State* L) {
        const char* msg = lua_tostring(L, -1);
        if (msg == NULL) msg = "error object is not a string";
        
        tud_cdc_write_str("\n\r[PANIC] Blackbird has crashed, RIP. Please push reset button.\n\r");
        
        // Also print the actual error message
        char buffer[256];
        snprintf(buffer, sizeof(buffer), "Lua Panic: %s\n\r", msg);
        tud_cdc_write_str(buffer);
        
        // Force flush and wait for transmission
        tud_cdc_write_flush();
        uint32_t timeout = 0;
        while (tud_cdc_write_available() < 256 && timeout < 100000) {
            tud_task();
            sleep_us(100);
            timeout++;
        }
        
        exit(1); // Call _exit to halt system
        return 0; // return to Lua to abort
    }
    
    // Custom allocator with memory tracking and diagnostics
    static void* lua_custom_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
        (void)ud;  // unused
        
        // Track memory allocation statistics for diagnostics
        static size_t total_allocated = 0;
        static size_t peak_allocated = 0;
        static size_t allocation_count = 0;
        
        if (nsize == 0) {
            // Free
            if (ptr) {
                total_allocated -= osize;
                free(ptr);
            }
            return NULL;
        } else {
            // Allocate or reallocate
            void* new_ptr = realloc(ptr, nsize);
            
            if (new_ptr == NULL) {
                // Allocation failed! Use TinyUSB CDC for output
                char buffer[256];
                
                tud_cdc_write_str("\n\r");
                tud_cdc_write_str("========================================\n\r");
                tud_cdc_write_str("*** LUA MEMORY ALLOCATION FAILED ***\n\r");
                tud_cdc_write_str("========================================\n\r");
                
                snprintf(buffer, sizeof(buffer), "Requested: %zu bytes\n\r", nsize);
                tud_cdc_write_str(buffer);
                
                snprintf(buffer, sizeof(buffer), "Old size: %zu bytes\n\r", osize);
                tud_cdc_write_str(buffer);
                
                snprintf(buffer, sizeof(buffer), "Total allocated: %zu bytes (%.2f KB)\n\r", 
                         total_allocated, total_allocated / 1024.0f);
                tud_cdc_write_str(buffer);
                
                snprintf(buffer, sizeof(buffer), "Peak allocated: %zu bytes (%.2f KB)\n\r", 
                         peak_allocated, peak_allocated / 1024.0f);
                tud_cdc_write_str(buffer);
                
                snprintf(buffer, sizeof(buffer), "Allocation #%zu\n\r", allocation_count);
                tud_cdc_write_str(buffer);
                
                tud_cdc_write_str("========================================\n\r");
                tud_cdc_write_str("Try: 1) Run collectgarbage()\n\r");
                tud_cdc_write_str("     2) Simplify your script\n\r");
                tud_cdc_write_str("     3) Remove unused libraries\n\r");
                tud_cdc_write_str("========================================\n\r");
                
                
                return NULL;  // Return NULL to let Lua handle it
            }
            
            // Update statistics
            total_allocated = total_allocated - osize + nsize;
            if (total_allocated > peak_allocated) {
                peak_allocated = total_allocated;
            }
            allocation_count++;
            
            return new_ptr;
        }
    }

public:
    LuaManager() : L(nullptr) {
        instance = this;
        init();
    }
    
    ~LuaManager() {
        if (L) {
            lua_close(L);
        }
        instance = nullptr;
    }
    
    void init() {
        if (L) {
            lua_close(L);
        }
        
        // Create Lua state with custom allocator for memory tracking
        L = lua_newstate(lua_custom_alloc, NULL);
        if (!L) {
            printf("Error: Could not create Lua state\n\r");
            return;
        }
        
        // Register panic handler
        lua_atpanic(L, lua_panic_handler);
        
        
        // Load basic Lua libraries
        luaL_openlibs(L);

        // Install fast math overrides early so scripts that cache locals
        // (e.g. `local sin = math.sin`) bind to the fast versions.
        fastmath_lua_install(L, 1);
        
        // CRITICAL: Set aggressive garbage collection like crow does
        // Without this, Lua will run out of memory on embedded systems!
        // setpause = 55 (default 200) - run GC more frequently
        // setstepmul = 260 (default 200) - do more work per GC cycle
        lua_gc(L, LUA_GCSETPAUSE, 55);
        lua_gc(L, LUA_GCSETSTEPMUL, 260);
        
        // Override print function
        lua_register(L, "print", lua_print);
        
    // Add time function
    lua_register(L, "time", lua_time);
    
    // Add unique_card_id function for Workshop Computer compatibility
    lua_register(L, "unique_card_id", lua_unique_card_id);
    
    // Add unique_id function for crow compatibility (returns 3 integers)
    lua_register(L, "unique_id", lua_unique_id);
    
    // Add memory statistics function for debugging
    lua_register(L, "memstats", lua_memstats);
    
    // Add performance statistics function for monitoring
    lua_register(L, "perf_stats", lua_perf_stats);
    lua_register(L, "clock_stats", lua_clock_stats);
    
    // Add public view functions for norns integration
    lua_register(L, "pub_view_in", lua_pub_view_in);
    lua_register(L, "pub_view_out", lua_pub_view_out);
    
    // Add tell function for ^^event messages (critical for input.stream/change)
    lua_register(L, "tell", lua_tell);

    // Crow compatibility helpers: druid/norns sometimes call these at boot
    lua_register(L, "get_out", lua_get_out);
    lua_register(L, "get_cv", lua_get_cv);
    
    // Add hardware pulse output control
    lua_register(L, "hardware_pulse", lua_hardware_pulse);
    lua_register(L, "_pulse_coro_check", lua_pulse_coro_check);
    
    // Add hardware knob and switch access functions
    lua_register(L, "get_knob_main", lua_get_knob_main);
    lua_register(L, "get_knob_x", lua_get_knob_x);
    lua_register(L, "get_knob_y", lua_get_knob_y);
    lua_register(L, "get_switch_position", lua_get_switch_position);
    
    // Create tab table and add print function
    lua_newtable(L);
    lua_pushcfunction(L, lua_tab_print);
    lua_setfield(L, -2, "print");
    lua_setglobal(L, "tab");
    
    // Register CASL functions
    lua_register(L, "casl_describe", lua_casl_describe);
    lua_register(L, "casl_action", lua_casl_action);
    lua_register(L, "casl_defdynamic", lua_casl_defdynamic);
    lua_register(L, "casl_cleardynamics", lua_casl_cleardynamics);
    lua_register(L, "casl_setdynamic", lua_casl_setdynamic);
    lua_register(L, "casl_getdynamic", lua_casl_getdynamic);
    
    // Register crow backend functions for Output.lua compatibility
    lua_register(L, "LL_get_state", lua_LL_get_state);
    lua_register(L, "LL_set_volts", lua_LL_set_volts);
    lua_register(L, "set_output_scale", lua_set_output_scale);
    lua_register(L, "soutput_handler", lua_soutput_handler);
    
    // Register audio-rate noise functions
    lua_register(L, "LL_set_noise", lua_LL_set_noise);
    lua_register(L, "LL_clear_noise", lua_LL_clear_noise);

    // Register audio-rate oscillator functions
    lua_register(L, "LL_set_oscillator", lua_LL_set_oscillator);
    lua_register(L, "LL_clear_oscillator", lua_LL_clear_oscillator);
    
    // Register Just Intonation functions
    lua_register(L, "justvolts", lua_justvolts);
    lua_register(L, "just12", lua_just12);
    lua_register(L, "hztovolts", lua_hztovolts);
    
    // Register crow backend functions for Input.lua compatibility
    lua_register(L, "io_get_input", lua_io_get_input);
    lua_register(L, "set_input_stream", lua_set_input_stream);
    lua_register(L, "set_input_change", lua_set_input_change);
    lua_register(L, "set_input_window", lua_set_input_window);
    lua_register(L, "set_input_scale", lua_set_input_scale);
    lua_register(L, "set_input_volume", lua_set_input_volume);
    lua_register(L, "set_input_peak", lua_set_input_peak);
    lua_register(L, "set_input_freq", lua_set_input_freq);
    lua_register(L, "set_input_clock", lua_set_input_clock);
    lua_register(L, "set_input_none", lua_set_input_none);
    
    // Register metro system functions for full crow compatibility
    lua_register(L, "metro_start", lua_metro_start);
    lua_register(L, "metro_stop", lua_metro_stop);
    lua_register(L, "metro_set_time", lua_metro_set_time);
    lua_register(L, "metro_set_count", lua_metro_set_count);
    
    // Register clock system functions for full crow compatibility
    lua_register(L, "clock_cancel", lua_clock_cancel);
    lua_register(L, "clock_schedule_sleep", lua_clock_schedule_sleep);
    lua_register(L, "clock_schedule_sync", lua_clock_schedule_sync);
    lua_register(L, "clock_schedule_beat", lua_clock_schedule_beat);
    lua_register(L, "clock_get_time_beats", lua_clock_get_time_beats);
    lua_register(L, "clock_get_tempo", lua_clock_get_tempo);
    lua_register(L, "clock_set_source", lua_clock_set_source);
    lua_register(L, "clock_internal_set_tempo", lua_clock_internal_set_tempo);
    lua_register(L, "clock_internal_start", lua_clock_internal_start);
    lua_register(L, "clock_internal_stop", lua_clock_internal_stop);
    
    // Create _c table with l_bootstrap_c_tell (handles both hardware and crow protocol)
    lua_newtable(L);
    lua_pushcfunction(L, l_bootstrap_c_tell);
    lua_setfield(L, -2, "tell");
    lua_setglobal(L, "_c");
    
    // Create crow table as a separate table with the same tell function
    lua_newtable(L);
    lua_pushcfunction(L, l_bootstrap_c_tell);
    lua_setfield(L, -2, "tell");
    lua_setglobal(L, "crow");
    
    // Set up crow.reset and crow.init functions
    lua_getglobal(L, "crow");       // Get crow table
    lua_pushcfunction(L, l_crowlib_crow_reset);
    lua_setfield(L, -2, "reset");   // Set crow.reset
    lua_pushcfunction(L, l_crowlib_crow_reset);
    lua_setfield(L, -2, "init");    // Set crow.init (alias for reset)
    lua_pop(L, 1);                  // Pop crow table
    
    // Initialize CASL instances for all 4 outputs
    for (int i = 0; i < 4; i++) {
        casl_init(i);
    }
        
        // Load and execute embedded ASL libraries
        load_embedded_asl();

        // Track user-created globals so crow.reset() can clear them reliably
        if (luaL_dostring(L, R"(
            _user = {}
            local function __bb_trace(t, k, v)
                _user[k] = true
                rawset(t, k, v)
            end
            setmetatable(_G, { __newindex = __bb_trace })
        )") != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error setting up _user tracking: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        }
    }
    
    // Load embedded ASL libraries from compiled bytecode
    void load_embedded_asl() {
        if (!L) return;
        
        // Load ASL library first
        if (luaL_loadbuffer(L, (const char*)asl, asl_len, "asl.lua") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading ASL library: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
            return;
        }
        
        // ASL library returns the Asl table - capture it
        lua_setglobal(L, "Asl");
        
        // Also set up lowercase 'asl' for compatibility
        lua_getglobal(L, "Asl");
        lua_setglobal(L, "asl");
        
        // Load ASLLIB library
        if (luaL_loadbuffer(L, (const char*)asllib, asllib_len, "asllib.lua") != LUA_OK || lua_pcall(L, 0, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading ASLLIB library: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
            return;
        }
        
        // The ASLLIB functions are now available globally
        // Make sure basic ASL functions are available globally
        const char* setup_globals = R"(
            -- Make ASL library functions globally available
            for name, func in pairs(Asllib or {}) do
                _G[name] = func
            end
        )";
        
        if (luaL_dostring(L, setup_globals) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error setting up ASL globals: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        }
        
        // Load Output.lua class from embedded bytecode
        if (luaL_loadbuffer(L, (const char*)output, output_len, "output.lua") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading Output.lua: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            // Output.lua returns the Output table - capture it
            lua_setglobal(L, "Output");
            
            // Create output[1] through output[4] instances using Output.new()
            if (luaL_dostring(L, R"(
                output = {}
                for i = 1, 4 do
                    output[i] = Output.new(i)
                end
                print("Output objects created successfully!")
            )") != LUA_OK) {
                const char* error = lua_tostring(L, -1);
                printf("Error creating output objects: %s\n\r", error ? error : "unknown error");
                lua_pop(L, 1);
            }
        }
        
        // Load Input.lua class from embedded bytecode
        if (luaL_loadbuffer(L, (const char*)input, input_len, "input.lua") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading Input.lua: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            // Input.lua returns the Input table - capture it
            lua_setglobal(L, "Input");
            
            // Create input[1] and input[2] instances using Input.new() (crow-style)
            if (luaL_dostring(L, R"(
                input = {}
                for i = 1, 2 do
                    input[i] = Input.new(i)
                end
            )") != LUA_OK) {
                const char* error = lua_tostring(L, -1);
                printf("Error creating input objects: %s\n\r", error ? error : "unknown error");
                lua_pop(L, 1);
            }
        }
        
        // Load Metro.lua class from embedded bytecode (CRITICAL for First.lua)
        
        // Register pulse output helper function (must be before First.lua loads)
        // This avoids using luaL_dostring in _c.tell which can cause ISR issues
        if (luaL_dostring(L, R"(
            _pulse_sleep_and_clear = function(channel, coro_id, sleep_time)
                return function()
                    clock.sleep(sleep_time)
                    if _pulse_coro_check(channel, coro_id) then
                        hardware_pulse(channel, false)
                    end
                end
            end
        )") != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error registering _pulse_sleep_and_clear: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        }
        
        if (luaL_loadbuffer(L, (const char*)metro, metro_len, "metro.lua") != LUA_OK || lua_pcall(L, 0, 1, 0) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error loading Metro.lua: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            // Metro.lua returns the metro table - capture it as global "metro"
            lua_setglobal(L, "metro");
        }
        
        // Set up crow-style global handlers for event dispatching
        if (luaL_dostring(L, R"(
            -- Global change_handler function like real crow
            function change_handler(channel, state)
                if input and input[channel] and input[channel].change then
                    input[channel].change(state)
                else
                    print("change: ch" .. channel .. "=" .. tostring(state))
                end
            end
            
            -- Global stream_handler function like real crow  
            function stream_handler(channel, value)
                if input and input[channel] and input[channel].stream then
                    input[channel].stream(value)
                else
                    print("stream: ch" .. channel .. "=" .. tostring(value))
                end
            end
            
            print("Global event handlers set up successfully!")
        )") != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error setting up global handlers: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        }
        
        // Load crow ecosystem libraries (sequins, public, clock, quote, timeline)
        load_crow_ecosystem();
    }
    
    // Load crow ecosystem libraries that don't conflict with our custom setup
    void load_crow_ecosystem() {
        if (!L) return;
        
        // Helper lambda to load a library from embedded bytecode
        auto load_lib = [this](const char* lib_name, const char* global_name, 
                               const unsigned char* bytecode, size_t len) {
            if (luaL_loadbuffer(L, (const char*)bytecode, len, lib_name) != LUA_OK) {
                printf("  ERROR loading %s: %s\n\r", lib_name, lua_tostring(L, -1));
                lua_pop(L, 1);
                return;
            }
            if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
                printf("  ERROR executing %s: %s\n\r", lib_name, lua_tostring(L, -1));
                lua_pop(L, 1);
                return;
            }
            lua_setglobal(L, global_name);
        };
        
        // Load only essential libraries (sequins before public!)
        load_lib("sequins.lua", "sequins", sequins, sequins_len);
        load_lib("public.lua", "public", public_lua, public_len);
        load_lib("clock.lua", "clock", clock, clock_len);
        
        // Optional libraries - comment these out to save memory:
        // load_lib("calibrate.lua", "cal", calibrate, calibrate_len);
        load_lib("quote.lua", "quote", quote, quote_len);
        load_lib("timeline.lua", "timeline", timeline, timeline_len);
        load_lib("hotswap.lua", "hotswap", hotswap, hotswap_len);
        
        // Define delay() function (from l_crowlib.c)
        // This creates a closure using clock.run and clock.sleep
        if (luaL_dostring(L, "function delay(action, time, repeats)\n"
                             "local r = repeats or 0\n"
                             "return clock.run(function()\n"
                                     "for i=1,1+r do\n"
                                         "clock.sleep(time)\n"
                                         "action(i)\n"
                                     "end\n"
                                 "end)\n"
                         "end\n") != LUA_OK) {
            printf("  ERROR defining delay() function: %s\n\r", lua_tostring(L, -1));
            lua_pop(L, 1);
        }

                // ii is optional / not implemented in the emulator; ensure scripts that
                // reference ii.* in init() don't crash and abort initialization.
                if (luaL_dostring(L, R"(
                        if ii == nil then
                            local function __bb_ii_stub()
                                return setmetatable({}, {
                                    __index = function(t, k)
                                        local v = __bb_ii_stub()
                                        rawset(t, k, v)
                                        return v
                                    end,
                                    __call = function(...) end,
                                })
                            end
                            ii = __bb_ii_stub()
                        end
                )") != LUA_OK) {
                        printf("  ERROR installing ii stub: %s\n\r", lua_tostring(L, -1));
                        lua_pop(L, 1);
                }
        
        // Create Workshop Computer namespace table with knob and switch
        create_bb_table(L);
        
        // Print Lua memory usage for diagnostics
        int lua_mem_kb = lua_gc(L, LUA_GCCOUNT, 0);
        printf("Lua memory usage: %d KB\n\r", lua_mem_kb);
    }
    
    // Create knob table with __index metamethod for dynamic reads (returns 0.0-1.0)
    void create_knob_table(lua_State* L) {
        // Create knob table
        lua_newtable(L);  // knob table @1
        
        // Create metatable for dynamic property access
        lua_newtable(L);  // metatable @2
        
        // __index function for property access
        lua_pushcfunction(L, [](lua_State* L) -> int {
            const char* key = lua_tostring(L, 2);
            if (strcmp(key, "main") == 0) {
                return lua_get_knob_main(L);
            } else if (strcmp(key, "x") == 0) {
                return lua_get_knob_x(L);
            } else if (strcmp(key, "y") == 0) {
                return lua_get_knob_y(L);
            }
            lua_pushnil(L);
            return 1;
        });
        lua_setfield(L, -2, "__index");  // metatable.__index = func
        
        // Make table read-only
        lua_pushcfunction(L, [](lua_State* L) -> int {
            luaL_error(L, "knob table is read-only");
            return 0;
        });
        lua_setfield(L, -2, "__newindex");
        
        lua_setmetatable(L, -2);  // setmetatable(knob, metatable)
        
        // Don't set as global - will be set as bb.knob by caller
        // Leave knob table on stack for caller
    }
    // Create switch table with change callback support
    void create_switch_table(lua_State* L) {
        lua_newtable(L);  // switch table
        
        // Create metatable for property access
        lua_newtable(L);  // metatable
        
        lua_pushcfunction(L, [](lua_State* L) -> int {
            const char* key = lua_tostring(L, 2);
            if (strcmp(key, "position") == 0) {
                return lua_get_switch_position(L);
            } else if (strcmp(key, "change") == 0) {
                // Return the stored change callback
                lua_getglobal(L, "_switch_change_callback");
                return 1;
            }
            lua_pushnil(L);
            return 1;
        });
        lua_setfield(L, -2, "__index");
        
        lua_pushcfunction(L, [](lua_State* L) -> int {
            const char* key = lua_tostring(L, 2);
            if (strcmp(key, "change") == 0) {
                // Store the callback
                lua_pushvalue(L, 3);  // Copy callback function
                lua_setglobal(L, "_switch_change_callback");
                return 0;
            } else if (strcmp(key, "position") == 0) {
                luaL_error(L, "switch.position is read-only");
                return 0;
            }
            return 0;
        });
        lua_setfield(L, -2, "__newindex");
        
        lua_setmetatable(L, -2);
        
        // Don't set as global - will be set as bb.switch by caller
        
        // Initialize with nop callback
        lua_pushcfunction(L, [](lua_State* L) -> int { return 0; });
        lua_setglobal(L, "_switch_change_callback");
        
        // Leave switch table on stack for caller
    }
    
    // Create Workshop Computer namespace table
    void create_bb_table(lua_State* L) {
        // Create bb table
        lua_newtable(L);  // bb table @1
        
        // Create and add knob table
        create_knob_table(L);  // knob table @2
        lua_setfield(L, -2, "knob");  // bb.knob = knob table, pops @2
        
        // Create and add switch table
        create_switch_table(L);  // switch table @2
        lua_setfield(L, -2, "switch");  // bb.switch = switch table, pops @2
        
        // Create and add pulsein array table - simplified approach without closures
        lua_newtable(L);  // pulsein array table @2
        
        // Create pulsein[1] with simple metatable (no upvalues)
        lua_pushinteger(L, 1);
        lua_newtable(L);  // pulsein[1] table
        
        // Store the index directly in the table
        lua_pushinteger(L, 0);  // index 0 for pulsein[1]
        lua_setfield(L, -2, "_idx");
        
        // Add simple metatable
        lua_newtable(L);  // metatable
        lua_pushcfunction(L, pulsein_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, pulsein_newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, pulsein_call);
        lua_setfield(L, -2, "__call");
        lua_setmetatable(L, -2);
        
        lua_settable(L, -3);  // pulsein[1] = table
        
        // Create pulsein[2]
        lua_pushinteger(L, 2);
        lua_newtable(L);  // pulsein[2] table
        
        lua_pushinteger(L, 1);  // index 1 for pulsein[2]
        lua_setfield(L, -2, "_idx");
        
        lua_newtable(L);  // metatable
        lua_pushcfunction(L, pulsein_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, pulsein_newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, pulsein_call);
        lua_setfield(L, -2, "__call");
        lua_setmetatable(L, -2);
        
        lua_settable(L, -3);  // pulsein[2] = table
        
        lua_setfield(L, -2, "pulsein");  // bb.pulsein = pulsein array, pops @2
        
        // Create pulseout array [1] and [2] - for controlling pulse outputs
        lua_newtable(L);  // pulseout array @2
        
        // Create shared metatable with methods
        lua_newtable(L);  // metatable @3
        lua_pushcfunction(L, pulseout_index);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, pulseout_newindex);
        lua_setfield(L, -2, "__newindex");
        lua_pushcfunction(L, pulseout_call);
        lua_setfield(L, -2, "__call");
        // Store metatable in registry for reuse
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "pulseout_metatable");
        
        for (int i = 1; i <= 2; i++) {
            lua_pushinteger(L, i);
            lua_newtable(L);  // pulseout[i] table @4
            
            lua_pushinteger(L, i - 1);  // 0-indexed for C (0 = pulse1, 1 = pulse2)
            lua_setfield(L, -2, "_idx");
            
            // Set the shared metatable
            lua_pushvalue(L, -3);  // push metatable
            lua_setmetatable(L, -2);
            
            lua_settable(L, -4);  // pulseout[i] = table
        }
        
        lua_pop(L, 1);  // pop metatable
        
        lua_setfield(L, -2, "pulseout");  // bb.pulseout = array
        
        // Create audioin array [1] and [2] - for reading audio input voltages
        lua_newtable(L);  // audioin array @2
        
        // Create shared metatable
        lua_newtable(L);  // metatable @3
        lua_pushcfunction(L, audioin_index);
        lua_setfield(L, -2, "__index");
        
        for (int i = 1; i <= 2; i++) {
            lua_pushinteger(L, i);
            lua_newtable(L);  // audioin[i] table @4
            
            lua_pushinteger(L, i - 1);  // 0-indexed for C (0 = audioin1, 1 = audioin2)
            lua_setfield(L, -2, "_idx");
            
            // Set the shared metatable
            lua_pushvalue(L, -3);  // push metatable
            lua_setmetatable(L, -2);
            
            lua_settable(L, -4);  // audioin[i] = table
        }
        
        lua_pop(L, 1);  // pop metatable
        
        lua_setfield(L, -2, "audioin");  // bb.audioin = array
        
        // Create bb.connected table for querying normalization probe status
        lua_newtable(L);  // connected table @2
        
        // Create metatable with __index for dynamic property access
        lua_newtable(L);  // metatable @3
        lua_pushcfunction(L, bb_connected_index);
        lua_setfield(L, -2, "__index");
        
        // Make table read-only
        lua_pushcfunction(L, [](lua_State* L) -> int {
            luaL_error(L, "bb.connected table is read-only");
            return 0;
        });
        lua_setfield(L, -2, "__newindex");
        
        lua_setmetatable(L, -2);  // setmetatable(connected, metatable)
        
        lua_setfield(L, -2, "connected");  // bb.connected = connected table
        
        // Set bb as global
        lua_setglobal(L, "bb");  // _G.bb = bb table
        
        // Add noise function to bb table
        const char* add_noise = R"(
            bb.noise = function(gain)
                gain = gain or 1.0
                -- Clamp gain to 0.0-1.0 range
                if gain < 0.0 then gain = 0.0 end
                if gain > 1.0 then gain = 1.0 end
                return { asl._noise(gain) }
            end
        )";
        if (luaL_dostring(L, add_noise) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            printf("Error adding bb.noise: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        }
        
        // Set up default pulse output actions
        const char* setup_pulseout_defaults = R"(
            -- Add clock() method to the shared metatable (crow-style API)
            local mt1 = getmetatable(bb.pulseout[1])
            local mt2 = getmetatable(bb.pulseout[2])
            
            if mt1 and mt2 and mt1 == mt2 then
                -- They share the same metatable, set clock on it
                rawset(mt1, 'clock', function(self, div)
                    if type(div) == 'string' and (div == 'none' or div == 'off') then
                        -- Turn off clocked output
                        if self.ckcoro then 
                            clock.cancel(self.ckcoro)
                            self.ckcoro = nil
                        end
                        return
                    end
                    
                    -- Match crow output clock semantics (use main clock)
                    self.clock_div = div or self.clock_div or 1

                    -- Ensure there is always a pulse() action to run, just like output[n]:clock()
                    if rawget(self, '_action') == nil then
                        local ok, default_action = pcall(pulse)
                        if ok then
                            rawset(self, '_action', default_action)
                        else
                            print('bb.pulseout clock: pulse() unavailable (' .. tostring(default_action) .. ')')
                        end
                    end
                    
                    -- Cancel existing clock coroutine if any
                    if self.ckcoro then 
                        clock.cancel(self.ckcoro)
                        self.ckcoro = nil  -- Clear reference to prevent memory leak
                    end
                    
                    -- Start new clock coroutine that syncs to the main clock engine
                    self.ckcoro = clock.run(function()
                        while true do
                            clock.sync(self.clock_div)
                            local action = rawget(self, '_action')
                            if type(action) == 'table' then
                                _c.tell('output', self._idx + 3, action)
                            elseif type(action) == 'function' then
                                action()
                            else
                                local ok, fallback = pcall(pulse)
                                if ok then
                                    _c.tell('output', self._idx + 3, fallback)
                                end
                            end
                        end
                    end)
                end)
                
                -- Add high() and low() methods for manual control
                rawset(mt1, 'high', function(self)
                    -- Stop any clock coroutine
                    if self.ckcoro then 
                        clock.cancel(self.ckcoro)
                        self.ckcoro = nil
                    end
                    rawset(self, '_action', nil)
                    -- Set output high indefinitely via C
                    _c.tell('output', self._idx + 3, pulse(999999))
                end)
                
                rawset(mt1, 'low', function(self)
                    -- Stop any clock coroutine
                    if self.ckcoro then 
                        clock.cancel(self.ckcoro)
                        self.ckcoro = nil
                    end
                    rawset(self, '_action', nil)
                    -- Set output low via C
                    _c.tell('output', self._idx + 3, pulse(0))
                end)
            end
            -- No default pulseout clocks are started; users configure them explicitly
        )";
        
        if (luaL_dostring(L, setup_pulseout_defaults) != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            if (tud_cdc_connected()) {
                tud_cdc_write_str("ERROR setting up pulseout: ");
                tud_cdc_write_str(error ? error : "unknown error");
                tud_cdc_write_str("\n\r");
                
            }
            printf("Error setting up pulseout defaults: %s\n\r", error ? error : "unknown error");
            lua_pop(L, 1);
        } else {
            if (tud_cdc_connected()) {
                tud_cdc_write_str("Pulseout setup completed\n\r");
                
            }
        }
    }
    

    
    // CASL bridge functions
    static int lua_casl_describe(lua_State* L);
    static int lua_casl_action(lua_State* L);
    static int lua_casl_defdynamic(lua_State* L);
    static int lua_casl_cleardynamics(lua_State* L);
    static int lua_casl_setdynamic(lua_State* L);
    static int lua_casl_getdynamic(lua_State* L);
    
    // Crow backend functions for Output.lua compatibility
    static int lua_LL_get_state(lua_State* L);
    static int lua_LL_set_volts(lua_State* L);
    static int lua_set_output_scale(lua_State* L);
    static int lua_c_tell(lua_State* L);
    static int lua_soutput_handler(lua_State* L);
    
    // Audio-rate noise functions
    static int lua_LL_set_noise(lua_State* L);
    static int lua_LL_clear_noise(lua_State* L);

    // Oscillator (audio-rate) functions
    static int lua_LL_set_oscillator(lua_State* L);
    static int lua_LL_clear_oscillator(lua_State* L);
    
    // Just Intonation functions
    static int lua_justvolts(lua_State* L);
    static int lua_just12(lua_State* L);
    static int lua_hztovolts(lua_State* L);
    
    // Crow backend functions for Input.lua compatibility
    static int lua_io_get_input(lua_State* L);
    static int lua_set_input_stream(lua_State* L);
    static int lua_set_input_change(lua_State* L);
    static int lua_set_input_window(lua_State* L);
    static int lua_set_input_scale(lua_State* L);
    static int lua_set_input_volume(lua_State* L);
    static int lua_set_input_peak(lua_State* L);
    static int lua_set_input_freq(lua_State* L);
    static int lua_set_input_clock(lua_State* L);
    static int lua_set_input_none(lua_State* L);
    
    // Metro system Lua bindings
    static int lua_metro_start(lua_State* L);
    static int lua_metro_stop(lua_State* L);
    static int lua_metro_set_time(lua_State* L);
    static int lua_metro_set_count(lua_State* L);
    
    // Clock system Lua bindings
    static int lua_clock_cancel(lua_State* L);
    static int lua_clock_schedule_sleep(lua_State* L);
    static int lua_clock_schedule_sync(lua_State* L);
    static int lua_clock_schedule_beat(lua_State* L);
    static int lua_clock_get_time_beats(lua_State* L);
    static int lua_clock_get_tempo(lua_State* L);
    static int lua_clock_set_source(lua_State* L);
    static int lua_clock_internal_set_tempo(lua_State* L);
    static int lua_clock_internal_start(lua_State* L);
    static int lua_clock_internal_stop(lua_State* L);
    
    // Hardware knob and switch access
    static int lua_get_knob_main(lua_State* L);
    static int lua_get_knob_x(lua_State* L);
    static int lua_get_knob_y(lua_State* L);
    static int lua_get_switch_position(lua_State* L);
    
    // Evaluate Lua code and return result
    bool evaluate(const char* code) {
        if (!L) return false;
        
        int result = luaL_dostring(L, code);
        if (result != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            tud_cdc_write_str("lua error: ");
            tud_cdc_write_str(error ? error : "unknown error");
            tud_cdc_write_str("\n\r");
            tud_cdc_write_flush();
            lua_pop(L, 1);  // remove error from stack
            return false;
        }
        
        return true;
    }
    
    // Safe evaluation with error handling - prevents crashes from user code
    bool evaluate_safe(const char* code) {
        if (!L) return false;
        
        // ===============================================
        // OPTIMIZATION 2: Enable batching for script execution
        // ===============================================
        output_batch_begin();
        
        // Use protected call to prevent crashes
        int result = luaL_loadstring(L, code);
        if (result != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            tud_cdc_write_str("lua load error: ");
            tud_cdc_write_str(error ? error : "unknown error");
            tud_cdc_write_str("\n\r");
            tud_cdc_write_flush();
            lua_pop(L, 1);
            output_batch_flush(); // Flush even on error
            return false;
        }
        
        // Call with error handler
        result = lua_pcall(L, 0, 0, 0);
        
        // ===============================================
        // OPTIMIZATION 2: Flush batched outputs after execution
        // ===============================================
        output_batch_flush();
        
        if (result != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            tud_cdc_write_str("lua runtime error: ");
            tud_cdc_write_str(error ? error : "unknown error");
            tud_cdc_write_str("\n\r");
            tud_cdc_write_flush();
            lua_pop(L, 1);
            return false;
        }
        
        return true;
    }
    
    
    static LuaManager* getInstance() {
        return instance;
    }
};

LuaManager* LuaManager::instance = nullptr;

// Global USB buffer to ensure proper initialization across cores
static const int USB_RX_BUFFER_SIZE = 2048;  // Increased to support multi-line scripts
static char g_rx_buffer[USB_RX_BUFFER_SIZE] = {0};
static volatile int g_rx_buffer_pos = 0;
static volatile bool g_multiline_mode = false;  // Track multi-line mode (triple backticks)

// Forward declarations for USB command handling and processing
class BlackbirdCrow;
static void process_usb_byte(char c);

// Forward declarations of static wrapper functions
// (Implementations moved after BlackbirdCrow class definition)
static void handle_usb_command(const char* command);
static C_cmd_t parse_command(const char* buffer, int length);
static void handle_command_with_response(C_cmd_t cmd);

// Upload state machine (matches crow's REPL mode) - MUST BE BEFORE process_usb_byte!
typedef enum {
    REPL_normal = 0,
    REPL_reception,
    REPL_discard
} repl_mode_t;

static volatile repl_mode_t g_repl_mode = REPL_normal;
static char g_new_script[16 * 1024];  // 16KB script buffer for uploads
static volatile uint32_t g_new_script_len = 0;
static char g_new_script_name[64] = "";  // Store script title/name extracted from first line

// NOW define process_usb_byte (after the variables it needs)
static void process_usb_byte(char c) {
    // Safety check - ensure buffer position is sane
    if (g_rx_buffer_pos < 0 || g_rx_buffer_pos >= USB_RX_BUFFER_SIZE) {
        printf("ERROR: Buffer corruption detected! Resetting...\n\r");
        g_rx_buffer_pos = 0;
        g_multiline_mode = false;
        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
    }
    
    // Check for escape key (clears buffer, multiline mode, and reception mode)
    if (c == 0x1B) {  // ESC key
        g_rx_buffer_pos = 0;
        g_multiline_mode = false;
        g_repl_mode = REPL_normal;
        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
        return;
    }
    
    // Check for buffer overflow BEFORE adding character
    if (g_rx_buffer_pos >= USB_RX_BUFFER_SIZE - 1) {
        // Buffer full - send error message matching crow
        tud_cdc_write_str("!chunk too long!\n\r");
        
        g_rx_buffer_pos = 0;
        g_multiline_mode = false;  // Reset multiline state on overflow
        if (g_repl_mode == REPL_reception) {
            g_repl_mode = REPL_discard;  // Mark upload as failed
        }
        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
        return;
    }
    
    // Add character to buffer
    g_rx_buffer[g_rx_buffer_pos] = c;
    g_rx_buffer_pos++;
    g_rx_buffer[g_rx_buffer_pos] = '\0';
    
    // Check for triple backticks (multi-line delimiter)
    if (check_for_backticks(g_rx_buffer, g_rx_buffer_pos)) {
        // Toggle multiline mode
        g_multiline_mode = !g_multiline_mode;
        
        if (g_multiline_mode) {
            // Started multiline - strip the opening backticks
            g_rx_buffer_pos -= 3;
            g_rx_buffer[g_rx_buffer_pos] = '\0';
        } else {
            // Ended multiline - strip the closing backticks and execute
            g_rx_buffer_pos -= 3;
            g_rx_buffer[g_rx_buffer_pos] = '\0';
            
            // Execute the accumulated command if not empty
            if (g_rx_buffer_pos > 0) {
                handle_usb_command(g_rx_buffer);
            }
            
            // Clear buffer
            g_rx_buffer_pos = 0;
            memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
        }
        return;
    }
    
    // In single-line mode, execute on newline
    if (!g_multiline_mode && is_packet_complete(g_rx_buffer, g_rx_buffer_pos)) {
        // Strip trailing whitespace
        int clean_length = g_rx_buffer_pos;
        while (clean_length > 0 && 
               (g_rx_buffer[clean_length-1] == '\n' || 
                g_rx_buffer[clean_length-1] == '\r' || 
                g_rx_buffer[clean_length-1] == ' ' || 
                g_rx_buffer[clean_length-1] == '\t')) {
            clean_length--;
        }
        g_rx_buffer[clean_length] = '\0';
        
        // Skip empty commands
        if (clean_length > 0) {
            // Check for system commands FIRST (even in reception mode)
            C_cmd_t cmd = parse_command(g_rx_buffer, clean_length);
            if (cmd != C_none) {
                // System command - process it
                handle_command_with_response(cmd);
            } else if (g_repl_mode == REPL_reception) {
                // In reception mode and not a command - accumulate as script data
                if (g_new_script_len + clean_length < sizeof(g_new_script)) {
                    memcpy(&g_new_script[g_new_script_len], g_rx_buffer, clean_length);
                    g_new_script_len += clean_length;
                    g_new_script[g_new_script_len++] = '\n';
                }
            } else {
                // Normal REPL mode - execute Lua
                handle_usb_command(g_rx_buffer);
            }
        }
        
        // Clear buffer after processing
        g_rx_buffer_pos = 0;
        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
        return;
    }
}

// ============================================================================
// USB IRQ TIMERS - RX (TinyUSB task) and TX (flush) at 1kHz
// ============================================================================
static inline void usb_process_tx_bounded(void);

#define USB_SERVICE_INTERVAL_US 1000
#define USB_TX_INTERVAL_US 1000
static struct repeating_timer g_usb_service_timer;
static struct repeating_timer g_usb_tx_timer;
static constexpr uint32_t kUsbFlushIntervalUs = 2000; // Matches crow's 2ms cadence

// These are tick flags set from repeating-timer IRQs.
// TinyUSB itself is NOT called from IRQ context to avoid reentrancy/stack issues.
static volatile uint32_t g_usb_service_tick = 0;
static volatile uint32_t g_usb_tx_tick = 0;

static inline void usb_flush_if_due(void) {
    static uint32_t last_flush_us = 0;
    uint32_t now_us = time_us_32();
    if ((now_us - last_flush_us) >= kUsbFlushIntervalUs) {
        if (tud_cdc_connected()) {
            tud_cdc_write_flush();
        }
        last_flush_us = now_us;
    }
}

// USB service tick callback - runs at 1kHz (1ms intervals)
// IMPORTANT: Do not call TinyUSB from this IRQ.
static bool __isr __time_critical_func(usb_service_callback)(struct repeating_timer *t) {
    (void)t;
    g_usb_service_tick++;
    return true;
}

// USB TX tick callback - runs at 1kHz (1ms intervals)
// IMPORTANT: Do not call TinyUSB from this IRQ.
static bool __isr __time_critical_func(usb_tx_service_callback)(struct repeating_timer *t) {
    (void)t;
    g_usb_tx_tick++;
    return true;
}

static inline void usb_service_poll_from_main(void) {
    // Coalesce ticks to bounded work per loop iteration
    if (g_usb_service_tick) {
        g_usb_service_tick = 0;
        tud_task();

        // RX: Read available data and queue for main loop processing
        while (tud_cdc_available()) {
            uint8_t buf[64];
            uint32_t count = tud_cdc_read(buf, sizeof(buf));
            if (count == 0) break;
            usb_rx_lockfree_post((char*)buf, count);
        }
    }

    if (g_usb_tx_tick) {
        g_usb_tx_tick = 0;
        usb_process_tx_bounded();
    }
}

// Non-ISR USB TX processing
// Drains lock-free TX queue and performs conditional flushes.
// Bounded by message count to keep SOF handler short while avoiding drops.
static inline void usb_process_tx_bounded(void) {
    if (!tud_cdc_connected()) return;
    usb_tx_message_t msg;
    const int kMaxMsgsPerTick = 8;
    int sent_msgs = 0;
    while (sent_msgs < kMaxMsgsPerTick && usb_tx_lockfree_get(&msg)) {
        sent_msgs++;
        uint32_t written = tud_cdc_write(msg.data, msg.length);
        if (msg.needs_flush || tud_cdc_write_available() < 128) {
            tud_cdc_write_flush();
        }
        (void)written; // We currently rely on queue sizing; partial writes are dropped.
    }
    
    // Periodic flush to ensure data goes out even if queue is quiet
    usb_flush_if_due();
}


// ============================================================================
// OUTPUT BATCHING SYSTEM
// ============================================================================
// Queue output changes during Lua execution, flush all at once to avoid
// redundant calibration calculations and hardware writes

// Global flag to signal core1 to pause for flash operations
volatile bool g_flash_operation_pending = false;

typedef struct {
    bool pending;       // Has this output changed?
    float target_volts; // Target voltage to set
} pending_output_t;

static struct {
    pending_output_t outputs[4];
    bool batch_mode_active;
} g_output_batch = {
    {{false, 0.0f}, {false, 0.0f}, {false, 0.0f}, {false, 0.0f}},
    false
};

// Enable batching (call before Lua execution)
// Exported for use from C files
extern "C" void output_batch_begin() {
    g_output_batch.batch_mode_active = true;
    // Don't clear pending flags - allow accumulation across calls
}

// Queue an output change (called from lua_manager)
static void output_batch_queue(int channel, float volts) {
    if (channel < 1 || channel > 4) return;
    g_output_batch.outputs[channel - 1].pending = true;
    g_output_batch.outputs[channel - 1].target_volts = volts;
}

// Execute all queued changes (call after Lua execution)
// Exported for use from C files
extern "C" void output_batch_flush() {
    if (!g_output_batch.batch_mode_active) return;
    
    for (int i = 0; i < 4; i++) {
        if (g_output_batch.outputs[i].pending) {
            // Call actual hardware function once per changed output
            extern void hardware_output_set_voltage(int channel, float voltage);
            hardware_output_set_voltage(i + 1, g_output_batch.outputs[i].target_volts);
            g_output_batch.outputs[i].pending = false;
        }
    }
    
    g_output_batch.batch_mode_active = false;
}

// Check if in batch mode
static inline bool output_is_batching() {
    return g_output_batch.batch_mode_active;
}

// Try to extract a human-readable script title/name from the first line.
// Format (matches monome/bowery style):
//   --- My Script Title
// Notes:
// - Accepts either "--- Title" or "---Title" by stripping whitespace after "---".
static void extract_script_name(const char* script, uint32_t length) {
    g_new_script_name[0] = '\0';
    if (!script || length < 3) return;

    // Consider only the first line and only the first 200 chars.
    const char* end = script + (length < 200 ? length : 200);
    const char* p = script;

    // Skip whitespace at start
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    if (p >= end) return;

    const char* line_start = p;
    const char* line_end = line_start;
    while (line_end < end && *line_end != '\r' && *line_end != '\n') {
        line_end++;
    }

    // "--- <title>" (allow either "--- " or "---<title>" for robustness)
    if ((line_end - line_start) >= 3 && line_start[0] == '-' && line_start[1] == '-' && line_start[2] == '-') {
        const char* title_start = line_start + 3;
        // If there is whitespace after "---", skip it (supports the required "--- " format)
        while (title_start < line_end && (*title_start == ' ' || *title_start == '\t')) {
            title_start++;
        }

        const char* title_end = line_end;
        while (title_end > title_start && (title_end[-1] == ' ' || title_end[-1] == '\t')) {
            title_end--;
        }

        size_t title_len = (size_t)(title_end - title_start);
        if (title_len > 0) {
            if (title_len > sizeof(g_new_script_name) - 1) {
                title_len = sizeof(g_new_script_name) - 1;
            }
            memcpy(g_new_script_name, title_start, title_len);
            g_new_script_name[title_len] = '\0';
            return;
        }
    }
}

// ============================================================================
// BLACKBIRD CROW CLASS (Main Hardware Interface)
// ============================================================================

class BlackbirdCrow : public ComputerCard
{
    // Variables for communication between cores
    volatile uint32_t v1, v2;
    
    // Lua manager for REPL
    LuaManager* lua_manager;
    
public:
    // Cached unique ID for Lua access (since UniqueCardID() is protected)
    uint64_t cached_unique_id;

    uint16_t inputs[4];
    
    // Public wrappers for hardware knob and switch access (for Lua bindings)
    int32_t GetKnobValue(ComputerCard::Knob ind) { return KnobVal(ind); }
    ComputerCard::Switch GetSwitchValue() { return SwitchVal(); }
    bool GetSwitchChanged() { return SwitchChanged(); }
    
    // Public wrappers for pulse input access (for Lua bindings)
    bool GetPulseIn1() { return PulseIn1(); }
    bool GetPulseIn2() { return PulseIn2(); }
    
    // Public wrapper for normalization probe detection (for detection system)
    bool IsInputConnected(ComputerCard::Input i) { return Connected(i); }

    // Public wrappers for LED access (needed outside class scope)
    void IndicatorLedOn(uint32_t index) { this->LedOn(index); }
    void IndicatorLedOn(uint32_t index, bool state) { this->LedOn(index, state); }
    void IndicatorLedOff(uint32_t index) { this->LedOff(index); }
    void UpdateOutputLeds() {
        uint32_t now_us = time_us_32();
        // Only blink all LEDs when explicitly armed (e.g. after flash operations).
        // This avoids a confusing crash-like LED pattern if memory corruption ever
        // clobbers the timestamp variable.
        if (g_force_all_leds_armed && u32_time_before(now_us, g_force_all_leds_on_until_us)) {
            bool on = ((now_us / kResetIndicatorBlinkHalfPeriodUs) & 1u) == 0u;
            uint16_t level = on ? 4095 : 0;
            LedBrightness(0, level);
            LedBrightness(1, level);
            LedBrightness(2, level);
            LedBrightness(3, level);
            LedOn(4, on);
            LedOn(5, on);
            return;
        }

        // Auto-disarm once the hold period has elapsed.
        if (g_force_all_leds_armed && !u32_time_before(now_us, g_force_all_leds_on_until_us)) {
            g_force_all_leds_armed = false;
        }

        int32_t cv1_mv = g_output_state_mv[0];
        int32_t cv2_mv = g_output_state_mv[1];
        int32_t audio1_mv = g_output_state_mv[2];
        int32_t audio2_mv = g_output_state_mv[3];
        bool pulse1 = g_pulse_out_state[0];
        bool pulse2 = g_pulse_out_state[1];

        auto mv_to_brightness = [](int32_t mv) -> uint16_t {
            if (mv <= 0) {
                return 0;
            }
            if (mv >= 6000) {
                return 4095;
            }
            return (uint16_t)((mv * 682) >> 10);
        };

        LedBrightness(0, mv_to_brightness(audio1_mv));
        LedBrightness(1, mv_to_brightness(audio2_mv));
        LedBrightness(2, mv_to_brightness(cv1_mv));
        LedBrightness(3, mv_to_brightness(cv2_mv));
        LedOn(4, pulse1);
        LedOn(5, pulse2);
    }

    // Hardware abstraction functions for output
    // CRITICAL: Place in RAM - called from shaper_v() on every slope update
    __attribute__((section(".time_critical.hardware_set_output")))
    void hardware_set_output(int channel, float volts) {
        if (channel < 1 || channel > 4) return;
        
        // Clamp voltage to ±6V range
        if (volts > 6.0f) volts = 6.0f;
        if (volts < -6.0f) volts = -6.0f;
        
        // Convert to millivolts for calibrated output functions
        int32_t volts_mV = (int32_t)(volts * 1000.0f);
        
        // Store state for lua queries (in millivolts) - simplified
        set_output_state_simple(channel - 1, volts_mV);
        
        // Route to correct hardware output
        switch (channel) {
            case 1: // Output 3 → CVOut1 (use calibrated millivolts function)
                CVOut1Millivolts(volts_mV);
                break;
            case 2: // Output 4 → CVOut2 (use calibrated millivolts function)
                CVOut2Millivolts(volts_mV);
                break;
            case 3: // Output 1 → AudioOut1 (audio outputs use raw 12-bit values)
                {
                    int16_t dac_value = (int16_t)((volts_mV * 2048) / 6000);
                    AudioOut1(dac_value);
                }
                break;
            case 4: // Output 2 → AudioOut2 (audio outputs use raw 12-bit values)
                {
                    int16_t dac_value = (int16_t)((volts_mV * 2048) / 6000);
                    AudioOut2(dac_value);
                }
                break;
        }
    }
    
    // Q16-native hardware output - eliminates float conversion in hot path
    __attribute__((section(".time_critical.hardware_set_output_q16")))
    void hardware_set_output_q16(int channel, q16_t voltage_q16) {
        if (channel < 1 || channel > 4) return;
        
        // Convert Q16 to millivolts using integer math
        // But clamp to ±6V first: ±6.0 in Q16 = ±393216
        const q16_t Q16_6V = 393216;  // 6.0 * 65536
        if (voltage_q16 > Q16_6V) voltage_q16 = Q16_6V;
        if (voltage_q16 < -Q16_6V) voltage_q16 = -Q16_6V;
        
        // Convert Q16 to millivolts: (q16 * 1000) >> 16
        int32_t volts_mV = ((int64_t)voltage_q16 * 1000) >> 16;
        
        // Store state for lua queries
        set_output_state_simple(channel - 1, volts_mV);
        
        // Route to correct hardware output
        switch (channel) {
            case 1: // Output 3 → CVOut1
                CVOut1Millivolts(volts_mV);
                break;
            case 2: // Output 4 → CVOut2
                CVOut2Millivolts(volts_mV);
                break;
            case 3: // Output 1 → AudioOut1
                {
                    int16_t dac_value = (int16_t)((volts_mV * 2048) / 6000);
                    AudioOut1(dac_value);
                }
                break;
            case 4: // Output 2 → AudioOut2
                {
                    int16_t dac_value = (int16_t)((volts_mV * 2048) / 6000);
                    AudioOut2(dac_value);
                }
                break;
        }
    }
    
    // Hardware abstraction function for pulse outputs
    void hardware_set_pulse(int channel, bool state) {
        if (channel == 1) {
            PulseOut1(state);
            g_pulse_out_state[0] = state;
        } else if (channel == 2) {
            PulseOut2(state);
            g_pulse_out_state[1] = state;
        }
    }
    
    float hardware_get_output(int channel) {
        if (channel < 1 || channel > 4) return 0.0f;
        // Use AShaper_get_state to match crow's behavior exactly
        return AShaper_get_state(channel - 1);
    }
    
    // Hardware abstraction functions for input
    void hardware_get_input(int channel) {
        
        int16_t raw_value = 0;
        if (channel == 1) {
            raw_value = CVIn1();
        } else if (channel == 2) {
            raw_value = CVIn2();
        }

        set_input_state_simple(channel - 1, raw_value);

        return;
    }
    
    // LED control stubs (not implemented in current version)
    void debug_led_on(int index) {
        // Not implemented
    }
    
    void debug_led_off(int index) {
        // Not implemented
    }
    
    BlackbirdCrow()
    {
        // Initialize global USB buffer
        g_rx_buffer_pos = 0;
        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
        
        // Cache the unique ID for Lua access
        cached_unique_id = UniqueCardID();
        
        // Set global instance for Lua bindings
        g_blackbird_instance = this;
        
        // Initialize slopes system for crow-style output processing
        S_init(4); // Initialize 4 output channels
        ComputerCard::RegisterCore1BackgroundHook(blackbird_core1_background_service);
        
        // Initialize AShaper system for output quantization (pass-through mode)
        AShaper_init(4); // Initialize 4 output channels
        
        // Initialize detection system for 2 input channels
        Detect_init(2);
        
        // Initialize simple output state storage
        // (No special initialization needed for simple volatile array)
        
    // Initialize lock-free event queues for timing-critical events
        events_lockfree_init();
        
        // Initialize timer system for metro support (8 timers for full crow compatibility)
        Timer_Init(8);
        
        // Initialize metro system (depends on timer system)
        Metro_Init(8);
        
        // Initialize clock system for coroutine scheduling (16 max clock threads)
        clock_init(16);
        
        // Initialize flash storage system
        FlashStorage::init();
        
        lua_manager = new LuaManager();
    }
    
    // Load boot script from flash (called after hardware init)
    void load_boot_script()
    {
        // Load script from flash based on what's stored
        // Note: UF2 installation clears user scripts (erase blocks added during build)
        switch(FlashStorage::which_user_script()) {
            case USERSCRIPT_Default:
                // Load First.lua from compiled bytecode
                if (luaL_loadbuffer(lua_manager->L, (const char*)First, First_len, "First.lua") != LUA_OK 
                    || lua_pcall(lua_manager->L, 0, 0, 0) != LUA_OK) {
                    tud_cdc_write_str(" Failed to load First.lua\n\r");
                    tud_cdc_write_flush();
                } else {
                    tud_cdc_write_str(" Loaded: First.lua (default)\n\r");
                    tud_cdc_write_flush();
                    // Call init() like real crow does (no crow.reset() before init on startup)
                    lua_manager->evaluate_safe("if init then init() end");
                }
                break;
                
            case USERSCRIPT_User:
                {
                    uint16_t script_len = FlashStorage::get_user_script_length();
                    const char* script_addr = FlashStorage::get_user_script_addr();
                    const char* script_name = FlashStorage::get_script_name();
                    
                    // Execute directly from flash (XIP)
                    if (script_addr && luaL_loadbuffer(lua_manager->L, script_addr, script_len, "=userscript") == LUA_OK
                        && lua_pcall(lua_manager->L, 0, 0, 0) == LUA_OK) {
                        char msg[128];
                        if (script_name && script_name[0]) {
                            snprintf(msg, sizeof(msg), " Loaded: %s (%u bytes)\n\r", script_name, script_len);
                        } else {
                            snprintf(msg, sizeof(msg), " Loaded: Untitled User Script (%u bytes)\n\r", script_len);
                        }
                        tud_cdc_write_str(msg);
                        tud_cdc_write_flush();
                        // Call init() like real crow does (no crow.reset() before init on startup)
                        lua_manager->evaluate_safe("if init then init() end");
                    } else {
                        tud_cdc_write_str(" Failed to load user script from flash, loading First.lua\n\r");
                        tud_cdc_write_flush();
                        // Fallback to First.lua
                        if (luaL_loadbuffer(lua_manager->L, (const char*)First, First_len, "First.lua") == LUA_OK 
                            && lua_pcall(lua_manager->L, 0, 0, 0) == LUA_OK) {
                            tud_cdc_write_str(" Loaded First.lua fallback\n\r");
                            tud_cdc_write_flush();
                            // Call init() like real crow does (no crow.reset() before init on startup)
                            lua_manager->evaluate_safe("if init then init() end");
                        } else {
                            tud_cdc_write_str(" Failed to load First.lua fallback\n\r");
                            tud_cdc_write_flush();
                        }
                    }
                }
                break;
                
            case USERSCRIPT_Clear:
                printf("No user script loaded (cleared)\n");
                break;
        }
    }
    
    // Core0 main control loop - handles USB, events, Lua AND timer processing
    void MainControlLoop()
    {
        // Initialize USB lock-free queues
        usb_lockfree_init();
        
        // Initialize USB buffer
        g_rx_buffer_pos = 0;
        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
        
        // Start USB service timer (1kHz IRQ for stack + RX)
        if (!add_repeating_timer_us(-USB_SERVICE_INTERVAL_US, 
                                     usb_service_callback, 
                                     NULL, 
                                     &g_usb_service_timer)) {
            printf("Failed to start USB service timer!\n");
        }

        // Start USB TX timer (1kHz IRQ for deterministic flush cadence)
        if (!add_repeating_timer_us(-USB_TX_INTERVAL_US,
                                     usb_tx_service_callback,
                                     NULL,
                                     &g_usb_tx_timer)) {
            printf("Failed to start USB TX timer!\n");
        }
        
        // Welcome message timing - send 1.5s after startup
        bool welcome_sent = false;
        absolute_time_t welcome_time = make_timeout_time_ms(1500);
        
        // Zero out all outputs on startup
        for (int i = 1; i <= 4; i++) {
            char zero_cmd[32];
            snprintf(zero_cmd, sizeof(zero_cmd), "output[%d].volts = 0", i);
            lua_manager->evaluate_safe(zero_cmd);
        }
        
        // LED visualization cadence (~60Hz)
        static uint32_t last_led_update_us = 0;
        const uint32_t led_update_interval_us = 16667; // ~60Hz
        
        while (1) {
            // === PERFORMANCE MONITORING: Track loop iteration time ===
            uint32_t loop_start_time = time_us_32();

            // Service USB from main loop (timer IRQs only set tick flags)
            usb_service_poll_from_main();
            
            // Process USB RX messages from lock-free queue
            usb_rx_message_t rx_msg;
            while (usb_rx_lockfree_get(&rx_msg)) {
                // Process each byte from the RX message
                for (uint16_t i = 0; i < rx_msg.length; i++) {
                    process_usb_byte(rx_msg.data[i]);
                }
                
                // After processing each USB packet, check for commands without newlines
                // (druid sends ^^s, ^^e, ^^w as 3-byte packets without line endings)
                if (g_rx_buffer_pos >= 3 && g_rx_buffer_pos <= 10 && !g_multiline_mode) {
                    C_cmd_t cmd = parse_command(g_rx_buffer, g_rx_buffer_pos);
                    if (cmd != C_none) {
                        // Found a command without newline - handle it
                        handle_command_with_response(cmd);
                        // Clear buffer
                        g_rx_buffer_pos = 0;
                        memset(g_rx_buffer, 0, USB_RX_BUFFER_SIZE);
                    }
                }
            }

            uint32_t now_us = time_us_32();
            if ((now_us - last_led_update_us) >= led_update_interval_us) {
                last_led_update_us = now_us;
                UpdateOutputLeds();
            }
            
            // Send welcome message 1.5s after startup
            if (!welcome_sent && absolute_time_diff_us(get_absolute_time(), welcome_time) <= 0) {
                char card_id_str[48];
                
                tud_cdc_write_str("\n\r");
                tud_cdc_write_str(" Blackbird-v1.1\n\r");
                tud_cdc_write_str(" Music Thing Modular Workshop Computer\n\r");

                tud_cdc_write_flush(); // flush manually due to long welcome string
                
                snprintf(card_id_str, sizeof(card_id_str), " Program Card ID: 0x%08X%08X\n\r", 
                         (uint32_t)(cached_unique_id >> 32), (uint32_t)(cached_unique_id & 0xFFFFFFFF));
                
                tud_cdc_write_str(card_id_str);
                
                welcome_sent = true;
                
                // NOW load the boot script - all hardware is initialized
                load_boot_script();
            }
            
            // USB input now handled via lock-free queue (see above)
            // No need to call handle_usb_input() - it's replaced by queue processing
            
            // Check for performance warnings from ProcessSample
            if (g_performance_warning) {
                g_performance_warning = false;  // Clear flag
                uint32_t now_us = time_us_32();
                if (!u32_time_before(now_us, g_suppress_perf_warnings_until_us)) {
                    char perf_msg[256];
                    snprintf(perf_msg, sizeof(perf_msg),
                             "\n\r[PERF WARNING] ProcessSample exceeded budget: worst=%luus, overruns=%lu\n\r",
                             (unsigned long)g_worst_case_us, (unsigned long)g_overrun_count);
                    tud_cdc_write_str(perf_msg);
                }
                
                // Note: Don't reset g_worst_case_us here - let user query via perf_stats()
            }
            
            // Process queued messages from audio thread (bounded to avoid stalls)
            process_queued_messages_limited(kMaxLogMessagesPerLoop);
            
            // Process slope action callbacks from Core 1
            process_slope_action_callbacks();
            
            // *** OPTIMIZATION: Process detection events on Core 0 ***
            // This does the FP conversion and fires callbacks
            // Deferred from Core 1 ISR to eliminate FP math from audio thread
            extern void Detect_process_events_core0(void);
            Detect_process_events_core0();
            
            // Update stream-equivalent values for .volts queries (always maintained)
            update_input_stream_values();

            // Process lock-free clock resume events before anything else can stall them
            const int max_clock_events_per_loop = 32;  // allow higher rates, still bounded
            clock_event_lockfree_t clock_event;
            int clock_events_processed = 0;
            while (clock_events_processed < max_clock_events_per_loop && clock_lockfree_get(&clock_event)) {
                // Coalesce consecutive events for the same coro to prevent flooding
                clock_event_lockfree_t peek_event;
                while (clock_lockfree_peek(&peek_event) && peek_event.coro_id == clock_event.coro_id) {
                    // Discard older event in favor of latest resume
                    clock_lockfree_get(&clock_event);
                }
                L_handle_clock_resume_lockfree(&clock_event);
                clock_events_processed++;
                // Budget guard: break if over soft budget
                if ((int32_t)(time_us_32() - loop_start_time) > (int32_t)kMainLoopSoftBudgetUs) {
                    break;
                }
            }

            // Process lock-free metro events (high priority but bounded per loop)
            const int max_metro_events_per_loop = 32;
            metro_event_lockfree_t metro_event;
            int metro_events_processed = 0;
            while (metro_events_processed < max_metro_events_per_loop && metro_lockfree_get(&metro_event)) {
                // Coalesce consecutive events for the same metro ID to avoid flooding
                metro_event_lockfree_t peek_event;
                while (metro_lockfree_peek(&peek_event) && peek_event.metro_id == metro_event.metro_id) {
                    // Discard older event in favor of latest stage
                    metro_lockfree_get(&metro_event);
                }
                L_handle_metro_lockfree(&metro_event);
                metro_events_processed++;
                // Budget guard: break if over soft budget
                if ((int32_t)(time_us_32() - loop_start_time) > (int32_t)kMainLoopSoftBudgetUs) {
                    break;
                }
            }
            
            // Process lock-free input detection events (high priority)
            // IMPORTANT: Limit events per iteration to prevent starving clock system
            // Fast triggers can fill the queue, blocking clock_update() and coroutines
            input_event_lockfree_t input_event;
            const int max_input_events_per_loop = 8;  // Process up to 8 events per loop
            int input_events_processed = 0;
            while (input_lockfree_get(&input_event) && input_events_processed < max_input_events_per_loop) {
                L_handle_input_lockfree(&input_event);
                input_events_processed++;
                if ((int32_t)(time_us_32() - loop_start_time) > (int32_t)kMainLoopSoftBudgetUs) {
                    break;
                }
            }

            // Drain coalesced change events for inputs 1/2 (avoid backlog)
            for (int i = 0; i < 2; i++) {
                if (g_change_pending[i] > 0 && !g_change_callback_active[i]) {
                    g_change_callback_active[i] = true;
                    g_change_pending[i]--; // consume one
                    bool state = g_change_state[i];

                    // Avoid luaL_loadstring() here (can allocate heavily over time)
                    // Call input[i].change(state) directly.
                    if (lua_manager && lua_manager->L) {
                        lua_State* L = lua_manager->L;
                        output_batch_begin();
                        lua_getglobal(L, "input");
                        if (lua_istable(L, -1)) {
                            lua_rawgeti(L, -1, i + 1);
                            if (lua_istable(L, -1)) {
                                lua_getfield(L, -1, "change");
                                // Preserve historical semantics: skip only if nil/false.
                                // If user sets a truthy non-function, allow Lua to raise an error.
                                if (lua_toboolean(L, -1)) {
                                    lua_pushboolean(L, state);
                                    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                                        const char* err = lua_tostring(L, -1);
                                        tud_cdc_write_str("lua runtime error: ");
                                        tud_cdc_write_str(err ? err : "unknown error");
                                        tud_cdc_write_str("\n\r");
                                        tud_cdc_write_flush();
                                        lua_pop(L, 1);
                                    }
                                } else {
                                    lua_pop(L, 1); // pop nil/false
                                }
                            }
                            lua_pop(L, 1); // pop input[i]
                        }
                        lua_pop(L, 1); // pop input
                        output_batch_flush();
                    }

                    g_change_callback_active[i] = false;
                }
            }
            
            // Process lock-free ASL done events (medium priority, bounded)
            const int max_asl_done_events_per_loop = 32;
            asl_done_event_lockfree_t asl_done_event;
            int asl_events_processed = 0;
            while (asl_events_processed < max_asl_done_events_per_loop && asl_done_lockfree_get(&asl_done_event)) {
                L_handle_asl_done_lockfree(&asl_done_event);
                asl_events_processed++;
                if ((int32_t)(time_us_32() - loop_start_time) > (int32_t)kMainLoopSoftBudgetUs) {
                    break;
                }
            }
            
            // Check for switch changes and fire callback
            static ComputerCard::Switch last_switch = ComputerCard::Switch::Middle;
            static bool first_switch_check = true;
            static uint32_t last_switch_check_time = 0;
            uint32_t now = to_ms_since_boot(get_absolute_time());
            
            // Check switch every ~50ms to avoid excessive polling
            if (now - last_switch_check_time >= 50) {
                last_switch_check_time = now;
                ComputerCard::Switch current_switch = GetSwitchValue();
                
                // Only fire callback after first read (avoid firing on startup)
                if (!first_switch_check && current_switch != last_switch) {
                    const char* pos_str = (current_switch == ComputerCard::Switch::Down) ? "down" :
                                         (current_switch == ComputerCard::Switch::Middle) ? "middle" : "up";

                    // Call Lua switch.change callback without compiling a chunk.
                    if (lua_manager && lua_manager->L) {
                        lua_State* L = lua_manager->L;
                        lua_getglobal(L, "_switch_change_callback");
                        // Preserve historical semantics: skip only if nil/false.
                        if (lua_toboolean(L, -1)) {
                            lua_pushstring(L, pos_str);
                            if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                                const char* err = lua_tostring(L, -1);
                                tud_cdc_write_str("lua runtime error: ");
                                tud_cdc_write_str(err ? err : "unknown error");
                                tud_cdc_write_str("\n\r");
                                tud_cdc_write_flush();
                                lua_pop(L, 1);
                            }
                        } else {
                            lua_pop(L, 1); // pop nil/false
                        }
                    }
                }
                
                last_switch = current_switch;
                first_switch_check = false;
            }

            // Auto-select internal vs external clock based on normalization probe connectivity.
            // This prevents getting "stuck" on external clock after disconnect.
            auto_update_clock_source_from_connections();
            
            // Process clock edges deferred from ISR (avoids FP math in ISR)
            for (int i = 0; i < 2; i++) {
                while (g_pulsein_clock_edge_count[i] > 0) {
                    g_pulsein_clock_edge_count[i]--;
                    clock_crow_handle_clock();  // Safe on Core 0
                }
            }

            // === ASAP CALLBACK: User-defined high-frequency function (bb.asap) ===
            // Runs every MainControlLoop iteration. Intended for lightweight mapping logic
            // (e.g. reading knobs and updating outputs). Heavy work will degrade performance.
            // Safe: never called from ISR. If it errors, we print once and clear it.
            {
                static bool asap_error_reported = false;
                lua_getglobal(lua_manager->L, "bb");            // @1 = bb or nil
                if (!lua_isnil(lua_manager->L, -1)) {
                    lua_getfield(lua_manager->L, -1, "asap");   // @2 = bb.asap
                    if (lua_isfunction(lua_manager->L, -1)) {
                        if (lua_pcall(lua_manager->L, 0, 0, 0) != LUA_OK) {
                            const char* err = lua_tostring(lua_manager->L, -1);
                            if (!asap_error_reported) {
                                printf("bb.asap error: %s\n", err ? err : "(unknown)");
                                asap_error_reported = true;
                            }
                            lua_pop(lua_manager->L, 1); // pop error message
                            // Clear bb.asap to prevent repeated errors
                            lua_pushnil(lua_manager->L);           // @2
                            lua_setfield(lua_manager->L, -2, "asap"); // bb.asap = nil, pops nil
                        }
                    } else {
                        lua_pop(lua_manager->L, 1); // pop non-function asap
                    }
                }
                lua_settop(lua_manager->L, 0); // clean stack
            }
            
            // Check for pulse input changes and fire callbacks
            // Edge detection happens at the ProcessSample() rate, we just check flags here
            // REENTRANCY PROTECTION: Skip callback if one is already running (prevents crashes from fast clocks)
            for (int i = 0; i < 2; i++) {
                if (g_pulsein_edge_count[i] > 0 && !g_pulsein_callback_active[i]) {
                    // Mark callback as active FIRST to prevent reentrancy
                    g_pulsein_callback_active[i] = true;
                    
                    // Clear one edge AFTER marking active (prevents race)
                    g_pulsein_edge_count[i]--;
                    
                    // Cache the edge state before calling Lua (in case it changes)
                    bool edge_state = g_pulsein_edge_state[i];

                    // Edge was detected at audio rate - fire callback without compiling a chunk.
                    if (lua_manager && lua_manager->L) {
                        lua_State* L = lua_manager->L;
                        char cb_name[32];
                        snprintf(cb_name, sizeof(cb_name), "_pulsein%d_change_callback", i + 1);
                        lua_getglobal(L, cb_name);
                        // Preserve historical semantics: skip only if nil/false.
                        if (lua_toboolean(L, -1)) {
                            lua_pushboolean(L, edge_state);
                            if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                                const char* err = lua_tostring(L, -1);
                                tud_cdc_write_str("lua runtime error: ");
                                tud_cdc_write_str(err ? err : "unknown error");
                                tud_cdc_write_str("\n\r");
                                tud_cdc_write_flush();
                                lua_pop(L, 1);
                            }
                        } else {
                            lua_pop(L, 1); // pop nil/false
                        }
                    }
                    
                    // Mark callback as complete
                    g_pulsein_callback_active[i] = false;
                }
            }
            
            // Smooth Lua GC to avoid long pauses; tiny step each loop
            if (lua_manager && lua_manager->L) {
                lua_gc(lua_manager->L, LUA_GCSTEP, kLuaGcStepSize);
            }

            // Update public view monitoring (~15fps), but skip if we've blown budget
            static uint32_t last_pubview_time = 0;
            if (now - last_pubview_time >= 66) { // ~15fps (66ms = 1000/15)
                if ((time_us_32() - loop_start_time) < kMainLoopSoftBudgetUs) {
                    last_pubview_time = now;
                    public_update();
                }
            }
            
            // === PERFORMANCE MONITORING: Track loop worst-case time ===
            uint32_t loop_elapsed = time_us_32() - loop_start_time;
            if (loop_elapsed > g_loop_worst_case_us) {
                g_loop_worst_case_us = loop_elapsed;
            }
            g_loop_iteration_count++;
            

        }
    }

    // Accumulate script data during upload (REPL_reception mode)
    void receive_script_data(const char* buf, uint32_t len) {
        if (g_repl_mode != REPL_reception) {
            return;  // Safety check
        }
        
        if (g_new_script_len + len >= sizeof(g_new_script)) {
            tud_cdc_write_str("!ERROR! Script is too long.\n\r");
            
            g_repl_mode = REPL_discard;
            return;
        }
        
        // Accumulate script data
        memcpy(&g_new_script[g_new_script_len], buf, len);
        g_new_script_len += len;
        
        // Add newline if not present (preserves formatting)
        if (len > 0 && buf[len-1] != '\n') {
            g_new_script[g_new_script_len++] = '\n';
        }
    }
    
    // Handle USB commands received from Core1 via mailbox
    void handle_usb_command(const char* command)
    {
        // Parse and handle command
        C_cmd_t cmd = parse_command(command, strlen(command));
        
        // System commands are ALWAYS processed (even in reception mode)
        if (cmd != C_none) {
            handle_command_with_response(cmd);
            return;
        }
        
        // For non-command data:
        if (g_repl_mode == REPL_reception) {
            // We're in upload mode - this shouldn't normally happen
            // (script data should come through receive_script_data)
            // But handle it anyway for robustness
            receive_script_data(command, strlen(command));
        } else {
            // Normal REPL mode - execute Lua immediately
            if (lua_manager) {
                lua_manager->evaluate_safe(command);
            }
        }
    }
    
    // Handle commands and send responses directly (single-core)
    void handle_command_with_response(C_cmd_t cmd)
    {
        switch (cmd) {
            case C_version: {
                // Embed build date/time and debug format so a late serial connection can verify firmware
                tud_cdc_write_str("^^version('blackbird-1.1')\n\r");
                
                break; }
                
            case C_identity: {
                uint64_t unique_id = cached_unique_id;
                char response[80];
                snprintf(response, sizeof(response), "^^identity('0x%08X%08X')\n\r", 
                         (uint32_t)(unique_id >> 32), (uint32_t)(unique_id & 0xFFFFFFFF));
                tud_cdc_write_str(response);
                
                break;
            }
            
            case C_print: {
                // Check if there's a user script in flash and print its name
                USERSCRIPT_t script_type = FlashStorage::which_user_script();
                if (script_type == USERSCRIPT_User) {
                    const char* script_name = FlashStorage::get_script_name();
                    if (script_name && script_name[0] != '\0') {
                        char msg[128];
                        snprintf(msg, sizeof(msg), "Running: %s\n\r", script_name);
                        tud_cdc_write_str(msg);
                    } else {
                        tud_cdc_write_str("Running: user script (unnamed)\n\r");
                    }
                } else if (script_type == USERSCRIPT_Default) {
                    tud_cdc_write_str("Running: First.lua (default)\n\r");
                } else {
                    tud_cdc_write_str("No user script.\n\r");
                }
                
                sleep_ms(50);  // Give USB time to transmit before next command
                break;
            }
                
            case C_restart:
                tud_cdc_write_str("Press the RESET button to reset Workshop Computer.\n\r");
                
                // Could implement actual restart here
                break;
                
            case C_killlua:
                tud_cdc_write_str("killing lua...\n\r");
                
                if (lua_manager) {
                    // Soft reset: clear Lua state but don't reboot hardware
                    // This matches real Crow's Lua_Reset() behavior
                    
                    // 1. Stop all metros
                    Metro_stop_all();
                    
                    // 2. Clear input detectors
                    for (int i = 0; i < 2; i++) {
                        Detect_none(Detect_ix_to_p(i));
                    }

                    // 3a. Stop all noise modes
                    for (int i = 0; i < 4; i++) {
                        g_noise_active[i] = false;
                        g_noise_gain[i] = 0;
                        g_noise_lock_counter[i] = 0;
                    }
                    g_noise_active_mask = 0;  // Clear all bits in mask
                    
                    // 3b. Stop all output slopes
                    for (int i = 0; i < 4; i++) {
                        S_toward(i, 0.0, 0.0, SHAPE_Linear, NULL);
                    }
                    
                    // 4. Cancel all clock coroutines
                    clock_cancel_coro_all();
                    
                    // 6. Reset crow modules to defaults (calls crow.reset() in Lua)
                    lua_manager->evaluate_safe("if crow and crow.reset then crow.reset() end");
                    
                    // 7. Clear user globals using Crow's _user table approach
                    lua_manager->evaluate_safe(
                        "if _user then "
                        "  for k,_ in pairs(_user) do "
                        "    _G[k] = nil "
                        "  end "
                        "end "
                        "_G._user = {}"
                    );

                    // 8. Reset init() to nop_fn to prevent errors if new script doesn't define it
                    lua_manager->evaluate_safe("init = nop_fn");
                    
                    // 8. Reset init() to empty function
                    lua_manager->evaluate_safe("_G.init = function() end");
                    
                    // 9. Garbage collect twice for thorough cleanup
                    lua_gc(lua_manager->L, LUA_GCCOLLECT, 1);
                    lua_gc(lua_manager->L, LUA_GCCOLLECT, 1);
                }
                
                break;
                
            case C_boot:
                tud_cdc_write_str("Workshop Computer does not support bootloader command sorry.\n\r");
                
                break;
                
            case C_startupload:
                // Clear and prepare for script reception
                g_new_script_len = 0;
                memset(g_new_script, 0, sizeof(g_new_script));
                g_new_script_name[0] = '\0';  // Clear script name
                lua_manager->evaluate_safe("crow.reset()"); // Reset crow state before upload
                g_repl_mode = REPL_reception;
                tud_cdc_write_str("script upload started\n\r");
                
                break;
                
            case C_endupload:
                if (g_repl_mode == REPL_discard) {
                    tud_cdc_write_str("upload failed, returning to normal mode\n\r");
                } else if (g_new_script_len > 0 && lua_manager) {
                    // Perform aggressive reset before loading new script (matches ^^k behavior)
                    // This ensures a clean slate with no lingering state from previous script
                    
                    // 1. Stop all metros
                    Metro_stop_all();
                    
                    // 2. Clear input detectors
                    for (int i = 0; i < 2; i++) {
                        Detect_none(Detect_ix_to_p(i));
                    }
                    
                    // 3. Stop all output slopes
                    for (int i = 0; i < 4; i++) {
                        S_toward(i, 0.0, 0.0, SHAPE_Linear, NULL);
                    }
                    
                    // 3b. Stop all noise modes
                    for (int i = 0; i < 4; i++) {
                        g_noise_active[i] = false;
                        g_noise_gain[i] = 0;
                        g_noise_lock_counter[i] = 0;
                    }
                    g_noise_active_mask = 0;  // Clear all bits in mask
                    
                    // 4. Cancel all clock coroutines
                    // 3b. Stop all noise modes
                    for (int i = 0; i < 4; i++) {
                        g_noise_active[i] = false;
                        g_noise_gain[i] = 0;
                        g_noise_lock_counter[i] = 0;
                    }
                    g_noise_active_mask = 0;  // Clear all bits in mask
                    
                    // 4. Cancel all clock coroutines
                    clock_cancel_coro_all();
                    
                    // Run script in RAM (temporary) - matches crow's REPL_upload(0)
                    if (lua_manager->evaluate_safe(g_new_script)) {
                        // Garbage collect for cleanup
                        lua_gc(lua_manager->L, LUA_GCCOLLECT, 1);
                        
                        // Script loaded successfully - now call init() to start it (like Lua_crowbegin)
                        // Real crow does NOT call crow.reset() before init() on script upload
                        lua_manager->evaluate_safe("if init then init() end");
                        tud_cdc_write_str("^^ready()\n\r"); // Inform host that script is running
                        // Silent success - script is now running
                    } else {
                        tud_cdc_write_str("\\script evaluation failed\n\r");
                    }
                } else {
                    tud_cdc_write_str("\\no script data received\n\r");
                }
                g_repl_mode = REPL_normal;
                
                break;
                
            case C_flashupload: {
                // Suppress perf warnings caused by upload/flash write stalls.
                // Druid flow instructs user to reset after completion.
                g_suppress_perf_warnings_until_us = time_us_32() + kResetIndicatorHoldUs;
                g_performance_warning = false;

                if (g_repl_mode == REPL_discard) {
                    tud_cdc_write_str("upload failed, discard mode\n\r");
                    
                } else if (g_new_script_len > 0 && lua_manager) {
                    // Try to extract script name from first comment line
                    extract_script_name(g_new_script, g_new_script_len);
                    
                    // Run script AND save to flash - matches crow's REPL_upload(1)
                    // Tell user to prepare for manual reset BEFORE we write to flash
                    
                    tud_cdc_write_str("\n\r");
                    tud_cdc_write_str("========================================\n\r");
                    
                    if (g_new_script_name[0]) {
                        char msg[64];
                        snprintf(msg, sizeof(msg), "Writing %s to flash...\n\r", g_new_script_name);
                        tud_cdc_write_str(msg);
                    } else {
                        tud_cdc_write_str("Writing script to flash...\n\r");
                    }
                    
                    
                    // Write to flash (this will reset core1, do the flash write, then restart core1)
                    if (FlashStorage::write_user_script_with_name(g_new_script, g_new_script_len, g_new_script_name)) {
                        // Give USB time to stabilize after core1 restart
                        
                        
                        tud_cdc_write_str("User script saved to flash!\n\r");
                        tud_cdc_write_str("\n\r");
                        tud_cdc_write_str("Press the RESET button (next to card slot)\n\r");
                        tud_cdc_write_str("on your Workshop Computer to load your script.\n\r");
                        tud_cdc_write_str("========================================\n\r");
                        tud_cdc_write_str("\n\r");

                        // Light up all LEDs (and keep them lit) to indicate upload complete
                        g_force_all_leds_on_until_us = time_us_32() + kResetIndicatorHoldUs;
                        g_force_all_leds_armed = true;
                        UpdateOutputLeds();
                    } else {
                        tud_cdc_write_str("flash write failed\n\r");
                        
                    }
                } else {
                    char debug_buf[128];
                    snprintf(debug_buf, sizeof(debug_buf), "no script data (len=%lu, lua_manager=%p)\n\r", 
                             (unsigned long)g_new_script_len, (void*)lua_manager);
                    tud_cdc_write_str(debug_buf);
                }
                g_repl_mode = REPL_normal;
                
                break;
            }
                
            case C_flashclear:
                // Suppress perf warnings caused by flash erase/program stalls.
                // Clear flow instructs user to reset after completion.
                g_suppress_perf_warnings_until_us = time_us_32() + kResetIndicatorHoldUs;
                g_performance_warning = false;

                // Output status BEFORE flash operation (which disables interrupts)
                
                tud_cdc_write_str("\n\r");
                tud_cdc_write_str("========================================\n\r");
                tud_cdc_write_str("Clearing user script...\n\r");
                
                // Write to flash (this will disable interrupts briefly)
                if (FlashStorage::set_default_script_mode()) {
                    
                    tud_cdc_write_str("User script cleared!\n\r");
                    
                    
                    // Reset Lua environment immediately (matches real crow behavior)
                    // This will call crow.reset() which calls public.clear()
                    // which sends ^^pub("_clear")
                    if (lua_manager) {
                        // Call crow.reset() to clear state and trigger public.clear()
                        lua_manager->evaluate_safe("if crow and crow.reset then crow.reset() end");
                        
                        // Call init() if it exists (but there's no user script now)
                        lua_manager->evaluate_safe("if init then init() end");
                    }
                    
                    tud_cdc_write_str("========================================\n\r");
                    tud_cdc_write_str("Press the RESET button (next to card slot)\n\r");
                    tud_cdc_write_str("on your Workshop Computer to finalize.\n\r");
                    tud_cdc_write_str("\n\r");

                    // Light up all LEDs (and keep them lit) to indicate operation complete
                    g_force_all_leds_on_until_us = time_us_32() + kResetIndicatorHoldUs;
                    g_force_all_leds_armed = true;
                    UpdateOutputLeds();

                } else {
                    tud_cdc_write_str("flash write failed\n\r");
                    
                }
                break;
                
            case C_loadFirst:
                // Load First.lua from compiled bytecode
                lua_manager->evaluate_safe("if crow and crow.reset then crow.reset() end");
                
                if (luaL_loadbuffer(lua_manager->L, (const char*)First, First_len, "First.lua") != LUA_OK 
                    || lua_pcall(lua_manager->L, 0, 0, 0) != LUA_OK) {
                    tud_cdc_write_str(" Failed to load First.lua\n\r");
                } else {
                    tud_cdc_write_str(" Loaded: First.lua (default)\n\r");
                    // Call init() like real crow does (no crow.reset() before init on startup)
                    lua_manager->evaluate_safe("if init then init() end");
                }
                break;
                
            default:
                // For unimplemented commands, send a simple acknowledgment
                printf("ok\n\r");
                break;
        }
        fflush(stdout);
    }
    
    ~BlackbirdCrow() {
        if (lua_manager) {
            delete lua_manager;
        }
    }

    // Parse command from buffer
    C_cmd_t parse_command(const char* buffer, int length)
    {
        for (int i = 0; i < length - 2; i++) {
            if (buffer[i] == '^' && buffer[i + 1] == '^') {
                char cmd_char = buffer[i + 2];
                switch (cmd_char) {
                    case 'v': return C_version;
                    case 'i': return C_identity;
                    case 'p': return C_print;
                    case 'r': return C_restart;
                    case 'b': return C_boot;
                    case 's': return C_startupload;
                    case 'e': return C_endupload;
                    case 'w': return C_flashupload;
                    case 'c': return C_flashclear;
                    case 'k': return C_killlua;
                    case 'f': return C_loadFirst;
                    case 'F': return C_loadFirst;
                }
            }
        }
        return C_none;
    }
    

    
    // ULTRA-LIGHTWEIGHT audio callback - ONLY READ INPUTS!
    // NO output processing in ISR - prevents multiplexer misalignment
    virtual void ProcessSample() override
    {
        // Performance monitoring: capture start time (low-cost)
        uint32_t start_time = time_us_32();
        
        // Increment sample counter for timer system (lightweight)
        extern volatile uint64_t global_sample_counter;
        global_sample_counter++;
        
        // Keep clock system synchronized (lightweight)
        clock_increment_sample_counter();

        // Derive service ticks from ProcessSample() rate with deterministic spacing.
        // This avoids hard-coded assumptions (e.g. 9.6kHz) when changing PROCESS_SAMPLE_RATE_HZ.
        static uint32_t clock_tick_phase = 0;
        clock_tick_phase += kClockServiceRateHz;
        if (clock_tick_phase >= PROCESS_SAMPLE_RATE_HZ_INT) {
            clock_tick_phase -= PROCESS_SAMPLE_RATE_HZ_INT;
            g_clock_ticks_pending++;
        }

        static uint32_t timer_tick_phase = 0;
        timer_tick_phase += kTimerServiceRateHz;
        if (timer_tick_phase >= PROCESS_SAMPLE_RATE_HZ_INT) {
            timer_tick_phase -= PROCESS_SAMPLE_RATE_HZ_INT;
            g_timer_ticks_pending++;
        }
        
        // === SLOPE PROCESSING: Sample-accurate output generation ===
        // Moved from Core 0 Timer_Process for zero-jitter, sample-accurate output
        // Process one sample per channel per ISR call (~115-210 cycles per active channel)
        extern q16_t S_consume_buffered_sample_q16(int index);
        extern bool S_slope_buffer_needs_fill(int index);
        extern void S_request_slope_buffer_fill(int index);
        
        for (int ch = 0; ch < SLOPE_CHANNELS; ch++) {
            q16_t output_q16 = S_consume_buffered_sample_q16(ch);
            hardware_output_set_voltage_q16(ch + 1, output_q16);
        }
        static int slope_refill_channel = 0;
        if (S_slope_buffer_needs_fill(slope_refill_channel)) {
            S_request_slope_buffer_fill(slope_refill_channel);
        }
        slope_refill_channel = (slope_refill_channel + 1) % SLOPE_CHANNELS;
        
        // Read CV inputs directly - clean and simple
        int16_t cv1 = CVIn1();
        int16_t cv2 = CVIn2();
        
        // Update input state for .volts queries
        set_input_state_simple(0, cv1);
        set_input_state_simple(1, cv2);
        
        // Read audio inputs and store raw values (NO floating point in ISR)
        set_audioin_raw(0, AudioIn1());
        set_audioin_raw(1, AudioIn2());
        
        // Process detection sample-by-sample for edge accuracy
        Detect_process_sample(0, cv1);
        Detect_process_sample(1, cv2);
        
        // Pulse input edge detection at ProcessSample rate - catches even very short pulses
        // Only process edges for physical jacks that the normalization probe reports as connected
        bool pulse1_connected = Connected(ComputerCard::Input::Pulse1);
        bool pulse2_connected = Connected(ComputerCard::Input::Pulse2);

        if (pulse1_connected) {
            if (g_pulsein_mode[0] == 1) {
                bool rising = PulseIn1RisingEdge();
                bool falling = PulseIn1FallingEdge();
                // direction: 0=both, 1=rising only, -1=falling only
                if ((rising && g_pulsein_direction[0] != -1) || (falling && g_pulsein_direction[0] != 1)) {
                    if (g_pulsein_edge_count[0] == 0) {
                        g_pulsein_edge_count[0] = 1;
                    } else {
                        g_pulsein_edge_overflow_count[0]++;
                    }
                    g_pulsein_edge_state[0] = PulseIn1();
                }
            } else if (g_pulsein_mode[0] == 2) {
                // Clock mode - only rising edges
                if (PulseIn1RisingEdge()) {
                    if (g_pulsein_clock_edge_count[0] == 0) {
                        g_pulsein_clock_edge_count[0] = 1;
                    } else {
                        g_pulsein_clock_overflow_count[0]++;
                    }
                }
            }
            g_pulsein_state[0] = PulseIn1();
        } else {
            // Clear any stale edge flags while disconnected
            g_pulsein_edge_count[0] = 0;
            g_pulsein_clock_edge_count[0] = 0;
            g_pulsein_state[0] = false;
        }

        if (pulse2_connected) {
            if (g_pulsein_mode[1] == 1) {
                bool rising = PulseIn2RisingEdge();
                bool falling = PulseIn2FallingEdge();
                // direction: 0=both, 1=rising only, -1=falling only
                if ((rising && g_pulsein_direction[1] != -1) || (falling && g_pulsein_direction[1] != 1)) {
                    if (g_pulsein_edge_count[1] == 0) {
                        g_pulsein_edge_count[1] = 1;
                    } else {
                        g_pulsein_edge_overflow_count[1]++;
                    }
                    g_pulsein_edge_state[1] = PulseIn2();
                }
            } else if (g_pulsein_mode[1] == 2) {
                // Clock mode - only rising edges
                if (PulseIn2RisingEdge()) {
                    if (g_pulsein_clock_edge_count[1] == 0) {
                        g_pulsein_clock_edge_count[1] = 1;
                    } else {
                        g_pulsein_clock_overflow_count[1]++;
                    }
                }
            }
            g_pulsein_state[1] = PulseIn2();
        } else {
            g_pulsein_edge_count[1] = 0;
            g_pulsein_clock_edge_count[1] = 0;
            g_pulsein_state[1] = false;
        }
        
        // === AUDIO-RATE NOISE GENERATION (ProcessSample rate) - INTEGER MATH ONLY ===
        // Generate and output noise for any active channels
        // OPTIMIZATION: Fast check if ANY noise is active before iterating channels
        if (g_noise_active_mask) {
            // Generate one shared noise sample and scale per-channel by gain (OK if identical spectra)
            int32_t base_noise_mv = generate_audio_noise_mv(6000);
            for (int ch = 0; ch < 4; ch++) {
                if (g_noise_active_mask & (1 << ch)) {
                    int32_t scaled_noise_mv = (base_noise_mv * g_noise_gain[ch]) / 6000;
                    g_output_state_mv[ch] = scaled_noise_mv;
                    switch (ch) {
                        case 0: CVOut1Millivolts(scaled_noise_mv); break;
                        case 1: CVOut2Millivolts(scaled_noise_mv); break;
                        case 2: {
                            int16_t dac_value = (int16_t)((scaled_noise_mv * 349) >> 10);
                            AudioOut1(dac_value);
                            break;
                        }
                        case 3: {
                            int16_t dac_value = (int16_t)((scaled_noise_mv * 349) >> 10);
                            AudioOut2(dac_value);
                            break;
                        }
                    }
                }
            }
          }
        
        // === PULSE OUTPUT 2: Controlled by Lua (no default behavior) ===
        // Users can control by calling bb.pulseout[2]:clock() or setting bb.pulseout[2].action
        
        // === PERFORMANCE MONITORING (low-cost) ===
        // Check if ProcessSample exceeded time budget
        // Each sample has a budget of kProcessSampleBudgetUs (≈104us at 9.6kHz); only >=100% counts as an overrun
        uint32_t elapsed = time_us_32() - start_time;
        
        // Always track worst-case execution time (available via perf_stats())
        if (elapsed > g_worst_case_us) {
            g_worst_case_us = elapsed;
        }
        
        // Only flag overruns when we actually exceed the entire per-sample budget
        if (elapsed >= kProcessSampleOverrunThresholdUs) {
            g_overrun_count++;
            
            static uint32_t last_report_time = 0;
            uint32_t now = time_us_32();
            
            // Report at most once per second to avoid flooding
            if ((now - last_report_time) > 1000000) {
                g_performance_warning = true;
                last_report_time = now;
            }
        }
    }
};

// Static wrapper function implementations (after BlackbirdCrow class is fully defined)
static void handle_usb_command(const char* command) {
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->handle_usb_command(command);
    }
}

static C_cmd_t parse_command(const char* buffer, int length) {
    if (g_blackbird_instance) {
        return ((BlackbirdCrow*)g_blackbird_instance)->parse_command(buffer, length);
    }
    return C_none;
}

static void handle_command_with_response(C_cmd_t cmd) {
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->handle_command_with_response(cmd);
    }
}

// Helper function to check if pulse input is connected (for normalization probe)
static bool is_pulse_input_connected(int pulse_idx) {
    BlackbirdCrow* card = (BlackbirdCrow*)g_blackbird_instance;
    if (!card) return true; // If no card instance, assume connected (safe default)
    
    // Map pulse index (0 or 1) to ComputerCard Input enum
    ComputerCard::Input pulse_input = (pulse_idx == 0) ? ComputerCard::Input::Pulse1 : ComputerCard::Input::Pulse2;
    
    return card->IsInputConnected(pulse_input);
}

// Automatically choose clock source based on whether a clock-mode jack is physically connected.
// If any clock-mode input is enabled *and* the normalization probe reports it connected, use external (CROW) clock.
// Otherwise, fall back to internal clock so clock.tempo / internal timing works again.
static void auto_update_clock_source_from_connections(void) {
    static bool using_external = false;

    BlackbirdCrow* card = (BlackbirdCrow*)g_blackbird_instance;
    if (!card) {
        return;
    }

    bool want_external = false;
    float want_div = 1.0f;

    // Prefer CV clock inputs first (input[1]/input[2])
    if (g_input_clock_mode[0] && card->IsInputConnected(ComputerCard::Input::CV1)) {
        want_external = true;
        want_div = g_input_clock_div[0];
    } else if (g_input_clock_mode[1] && card->IsInputConnected(ComputerCard::Input::CV2)) {
        want_external = true;
        want_div = g_input_clock_div[1];
    } else if (g_pulsein_mode[0] == 2 && card->IsInputConnected(ComputerCard::Input::Pulse1)) {
        want_external = true;
        want_div = g_pulsein_clock_div[0];
    } else if (g_pulsein_mode[1] == 2 && card->IsInputConnected(ComputerCard::Input::Pulse2)) {
        want_external = true;
        want_div = g_pulsein_clock_div[1];
    }

    if (want_external) {
        clock_crow_in_div(want_div);
        if (!using_external) {
            clock_set_source(CLOCK_SOURCE_CROW);
            using_external = true;
        }
    } else {
        if (using_external) {
            clock_set_source(CLOCK_SOURCE_INTERNAL);
            // Rebase immediately so scripts see internal tempo right away.
            // Otherwise, reference tempo won't update until the next internal beat tick.
            clock_internal_start(clock_get_time_beats(), false);
            using_external = false;
        }
    }
}

// __index metamethod for bb.connected table
static int bb_connected_index(lua_State* L) {
    const char* key = lua_tostring(L, 2);
    if (!key) {
        lua_pushboolean(L, false);
        return 1;
    }
    
    BlackbirdCrow* card = (BlackbirdCrow*)g_blackbird_instance;
    if (!card) {
        lua_pushboolean(L, true);  // Assume connected if no card
        return 1;
    }
    
    // Map string keys to ComputerCard Input enum
    ComputerCard::Input input;
    if (strcmp(key, "cv1") == 0) {
        input = ComputerCard::Input::CV1;
    } else if (strcmp(key, "cv2") == 0) {
        input = ComputerCard::Input::CV2;
    } else if (strcmp(key, "pulse1") == 0) {
        input = ComputerCard::Input::Pulse1;
    } else if (strcmp(key, "pulse2") == 0) {
        input = ComputerCard::Input::Pulse2;
    } else if (strcmp(key, "audio1") == 0) {
        input = ComputerCard::Input::Audio1;
    } else if (strcmp(key, "audio2") == 0) {
        input = ComputerCard::Input::Audio2;
    } else {
        lua_pushboolean(L, false);  // Unknown input
        return 1;
    }
    
    lua_pushboolean(L, card->IsInputConnected(input));
    return 1;
}

// CASL bridge functions
int LuaManager::lua_casl_describe(lua_State* L) {
    int raw = luaL_checkinteger(L, 1);
    int internal = raw - 1;
    
    // DON'T clear noise here - it needs to be cleared when the action actually RUNS
    // The noise() action will set it, and other actions should clear it when they start
    
    casl_describe(internal, L); // C is zero-based
    lua_pop(L, 2);
    return 0;
}

int LuaManager::lua_casl_action(lua_State* L) {
    int raw = luaL_checkinteger(L, 1);
    int act = luaL_checkinteger(L, 2);
    int internal = raw - 1;
    
    // When an action is explicitly triggered (act == 1), clear any existing noise
    // UNLESS noise was just set in the describe phase (lock counter == 10).
    // This handles output[n].volts = x, output[n](lfo()), etc. clearing old noise,
    // while allowing output[n](bb.noise()) to work correctly.
    if (internal >= 0 && internal < 4 && act == 1 && g_noise_active[internal]) {
        // If lock counter is 10, noise was just set in describe phase, don't clear
        if (g_noise_lock_counter[internal] != 10) {
            g_noise_active[internal] = false;
            g_noise_active_mask &= ~(1 << internal);  // Clear bit in mask
            g_noise_gain[internal] = 0;
            g_noise_lock_counter[internal] = 0;
        }
    }
    
    casl_action(internal, act); // C is zero-based
    
    // After action completes, if noise is active and lock counter is still 10,
    // reduce it to 2 so that the next user action will clear the noise.
    // This allows noise to persist through immediate automatic calls but be
    // cleared by the next explicit user action.
    if (internal >= 0 && internal < 4 && g_noise_active[internal] && g_noise_lock_counter[internal] == 10) {
        g_noise_lock_counter[internal] = 2;
    }
    
    lua_pop(L, 2);
    return 0;
}

int LuaManager::lua_casl_defdynamic(lua_State* L) {
    int c_ix = luaL_checkinteger(L, 1) - 1; // lua is 1-based
    lua_pop(L, 1);
    lua_pushinteger(L, casl_defdynamic(c_ix));
    return 1;
}

int LuaManager::lua_casl_cleardynamics(lua_State* L) {
    casl_cleardynamics(luaL_checkinteger(L, 1) - 1); // lua is 1-based
    lua_pop(L, 1);
    return 0;
}

int LuaManager::lua_casl_setdynamic(lua_State* L) {
    casl_setdynamic(luaL_checkinteger(L, 1) - 1, // lua is 1-based
                    luaL_checkinteger(L, 2),
                    luaL_checknumber(L, 3));
    lua_pop(L, 3);
    return 0;
}

int LuaManager::lua_casl_getdynamic(lua_State* L) {
    float d = casl_getdynamic(luaL_checkinteger(L, 1) - 1, // lua is 1-based
                             luaL_checkinteger(L, 2));
    lua_pop(L, 2);
    lua_pushnumber(L, d);
    return 1;
}

// Crow backend functions for Output.lua compatibility

// LL_get_state(channel) - Get current voltage state from slopes system
int LuaManager::lua_LL_get_state(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    float volts = S_get_state(channel - 1);  // Convert to 0-based for C
    lua_pushnumber(L, volts);
    return 1;
}

// LL_set_volts(channel, volts, [slew_seconds], [shape]) - Fast path for output[n].volts
// Uses CASL directly to preserve slew/shape behavior and output[n].done() callbacks.
int LuaManager::lua_LL_set_volts(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    float volts = (float)luaL_checknumber(L, 2);
    float slew_seconds = (float)luaL_optnumber(L, 3, 0.0);
    const char* shape_str = luaL_optstring(L, 4, "linear");

    if (channel < 1 || channel > 4) {
        return luaL_error(L, "Invalid channel: %d (must be 1-4)", channel);
    }
    if (slew_seconds < 0.0f) {
        slew_seconds = 0.0f;
    }

    const int idx = channel - 1;

    // Match Asl:describe behavior for output.volts (clears dynamics on that channel)
    casl_cleardynamics(idx);

    Shape_t shape = S_str_to_shape(shape_str);
    casl_describe_to_literal_q16(idx,
                                FLOAT_TO_Q16(volts),
                                FLOAT_TO_Q16(slew_seconds),
                                shape);

    // Match lua_casl_action noise-clearing behavior (LL_set_volts bypasses lua_casl_action).
    if (idx >= 0 && idx < 4 && g_noise_active[idx]) {
        // If lock counter is 10, noise was just set in describe phase, don't clear.
        // (Not expected for LL_set_volts, but keep behavior identical.)
        if (g_noise_lock_counter[idx] != 10) {
            g_noise_active[idx] = false;
            g_noise_active_mask &= ~(1 << idx);
            g_noise_gain[idx] = 0;
            g_noise_lock_counter[idx] = 0;
        }
    }

    casl_action(idx, 1);

    // Mirror lua_casl_action post-action lock-counter adjustment.
    if (idx >= 0 && idx < 4 && g_noise_active[idx] && g_noise_lock_counter[idx] == 10) {
        g_noise_lock_counter[idx] = 2;
    }
    return 0;
}

// LL_set_noise(channel, gain) - Enable audio-rate noise output
int LuaManager::lua_LL_set_noise(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    float gain = luaL_checknumber(L, 2);    // Gain multiplier (0.0-1.0)
    
    // Validate channel
    if (channel < 1 || channel > 4) {
        return luaL_error(L, "Invalid channel: %d (must be 1-4)", channel);
    }
    
    // Clamp gain to valid range
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    
    int ch_idx = channel - 1;
    
    // Stop any ongoing slopes/actions on this channel before enabling noise
    // This prevents LFOs or other actions from interfering with noise
    S_toward(ch_idx, 0.0, 0.0, SHAPE_Linear, NULL);
    
    g_noise_active[ch_idx] = true;
    g_noise_active_mask |= (1 << ch_idx);  // Set bit in mask for fast checking
    // Convert gain to millivolts (0-6000 mV) as integer
    g_noise_gain[ch_idx] = (int32_t)(gain * 6000.0f);
    // Set lock counter to prevent immediate clearing (ignore next few hardware_output_set_voltage calls)
    g_noise_lock_counter[ch_idx] = 10;  // Ignore next 10 calls
    
    return 0;
}

// LL_clear_noise(channel) - Disable noise output
int LuaManager::lua_LL_clear_noise(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    
    // Validate channel
    if (channel < 1 || channel > 4) {
        return luaL_error(L, "Invalid channel: %d (must be 1-4)", channel);
    }
    
    int ch_idx = channel - 1;
    g_noise_active[ch_idx] = false;
    g_noise_active_mask &= ~(1 << ch_idx);  // Clear bit in mask
    g_noise_gain[ch_idx] = 0;  // Clear integer gain
    g_noise_lock_counter[ch_idx] = 0;  // Clear lock
    
    return 0;
}

// LL_set_oscillator(channel, freq_hz, [level], [shape]) - Enable audio-rate oscillator
int LuaManager::lua_LL_set_oscillator(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    float freq = luaL_checknumber(L, 2);    // Frequency in Hz
    float level = (float)luaL_optnumber(L, 3, 5.0); // Peak volts (default 5V)
    const char* shape_str = luaL_optstring(L, 4, "sine");

    // Validate channel
    if (channel < 1 || channel > 4) {
        return luaL_error(L, "Invalid channel: %d (must be 1-4)", channel);
    }
    if (freq <= 0.0f) {
        return luaL_error(L, "freq must be > 0");
    }

    Shape_t shape = S_str_to_shape(shape_str);

    if (!S_set_oscillator(channel - 1, freq, level, shape)) {
        return luaL_error(L, "Failed to set oscillator on channel %d", channel);
    }
    return 0;
}

// LL_clear_oscillator(channel) - Disable oscillator on a channel
int LuaManager::lua_LL_clear_oscillator(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    if (channel < 1 || channel > 4) {
        return luaL_error(L, "Invalid channel: %d (must be 1-4)", channel);
    }
    S_clear_oscillator(channel - 1);
    return 0;
}

// set_output_scale(channel, scale_table, [modulo], [scaling]) - Set output quantization
int LuaManager::lua_set_output_scale(lua_State* L) {
    // Default values - shared between calls (matches crow behavior)
    static float mod = 12.0;     // default to 12TET
    static float scaling = 1.0;  // default to 1V/octave
    
    int nargs = lua_gettop(L);
    int channel = luaL_checkinteger(L, 1) - 1;  // Convert to 0-based
    
    // Validate channel
    if (channel < 0 || channel >= 4) {
        lua_pop(L, nargs);
        return luaL_error(L, "Invalid channel: %d (must be 1-4)", channel + 1);
    }
    
    // Case 1: No scale argument OR empty table -> chromatic quantization (semitones)
    if (nargs == 1 || (lua_istable(L, 2) && lua_rawlen(L, 2) == 0)) {
        float divs[12] = {0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 11.0};
        AShaper_set_scale(channel, divs, 12, 12.0, 1.0);
        lua_pop(L, nargs);
        return 0;
    }
    
    // Case 2: String argument 'none' -> disable quantization
    if (lua_isstring(L, 2)) {
        const char* str = lua_tostring(L, 2);
        if (strcmp(str, "none") == 0) {
            AShaper_unset_scale(channel);
            lua_pop(L, nargs);
            return 0;
        }
    }
    
    // Case 3: Table of scale degrees (semitones or ratios)
    if (!lua_istable(L, 2)) {
        lua_pop(L, nargs);
        return luaL_error(L, "Second argument must be table or 'none'");
    }
    
    int tlen = lua_rawlen(L, 2);
    if (tlen > MAX_DIV_LIST_LEN) {
        lua_pop(L, nargs);
        return luaL_error(L, "Scale table length must be 1-%d", MAX_DIV_LIST_LEN);
    }
    
    float divs[MAX_DIV_LIST_LEN];
    for (int i = 0; i < tlen; i++) {
        lua_pushnumber(L, i + 1);  // Lua is 1-based
        lua_gettable(L, 2);
        divs[i] = luaL_checknumber(L, -1);
        lua_pop(L, 1);
    }
    
    // Optional modulo parameter (arg 3)
    if (nargs >= 3) {
        mod = luaL_checknumber(L, 3);
    }
    
    // Optional scaling parameter (arg 4)
    if (nargs >= 4) {
        scaling = luaL_checknumber(L, 4);
    }
    
    AShaper_set_scale(channel, divs, tlen, mod, scaling);
    
    lua_pop(L, nargs);
    return 0;
}

// Helper function for justvolts/just12 implementation
static int lua_justvolts_impl(lua_State* L, float mul) {
    // Apply optional offset
    float offset = 0.0f;
    int nargs = lua_gettop(L);
    
    switch(nargs) {
        case 1: 
            break;
        case 2: 
            offset = log2f(luaL_checknumber(L, 2)) * mul;
            break;
        default:
            return luaL_error(L, "justvolts: need 1 or 2 args");
    }
    
    // Handle single number or table
    int lua_type_1 = lua_type(L, 1);
    
    if (lua_type_1 == LUA_TNUMBER) {
        // Single ratio
        float result = log2f(lua_tonumber(L, 1)) * mul + offset;
        lua_settop(L, 0);
        lua_pushnumber(L, result);
        return 1;
    }
    else if (lua_type_1 == LUA_TTABLE) {
        // Table of ratios
        int telems = lua_rawlen(L, 1);
        
        // Create result table
        lua_createtable(L, telems, 0);
        
        for(int i = 1; i <= telems; i++) {
            lua_rawgeti(L, 1, i);
            float ratio = luaL_checknumber(L, -1);
            float result = log2f(ratio) * mul + offset;
            lua_pop(L, 1);
            
            lua_pushnumber(L, result);
            lua_rawseti(L, 2, i); // Store in result table
        }
        
        // Remove original table, leave result on stack
        lua_remove(L, 1);
        return 1;
    }
    else {
        return luaL_error(L, "justvolts: argument must be number or table");
    }
}

// justvolts(ratio_or_table, [offset]) - Convert JI ratios to 1V/oct voltages
// Examples:
//   justvolts(2) -> 1.0 (one octave)
//   justvolts({1/1, 2/1, 4/1}) -> {0, 1, 2}
int LuaManager::lua_justvolts(lua_State* L) {
    return lua_justvolts_impl(L, 1.0f);
}

// just12(ratio_or_table, [offset]) - Convert JI ratios to 12TET semitone voltages
// Examples:
//   just12(3/2) -> 7.02 (perfect fifth in semitones)
//   just12({1/1, 5/4, 3/2}) -> {0, 3.86, 7.02}
//   just12({1/1, 5/4, 3/2}, 2) -> {12, 15.86, 19.02} (offset by 1 octave)
int LuaManager::lua_just12(lua_State* L) {
    return lua_justvolts_impl(L, 12.0f);
}

// hztovolts(freq, [reference]) - Convert frequency to voltage
// Examples:
//   hztovolts(440) -> 0 (A4 at 0V by default)
//   hztovolts(880) -> 1.0 (A5, one octave up)
//   hztovolts(440, 261.626) -> 0.585 (A4 referenced to C4)
int LuaManager::lua_hztovolts(lua_State* L) {
    const float MIDDLE_C_INV = 1.0f / 261.626f; // 1/C4 frequency
    
    float retval = 0.0f;
    int nargs = lua_gettop(L);
    
    switch(nargs) {
        case 1: // use default middle C reference
            retval = log2f(luaL_checknumber(L, 1) * MIDDLE_C_INV);
            break;
        case 2: // use provided reference
            retval = log2f(luaL_checknumber(L, 1) / luaL_checknumber(L, 2));
            break;
        default:
            return luaL_error(L, "hztovolts: need 1 or 2 args");
    }
    
    lua_settop(L, 0);
    lua_pushnumber(L, retval);
    return 1;
}

// _c.tell('output', channel, value) - Send output to slopes system (proper crow behavior)
int LuaManager::lua_c_tell(lua_State* L) {
    // Add safety check for argument count
    int argc = lua_gettop(L);
    if (argc < 3) {
        printf("_c.tell: insufficient arguments (%d)\n\r", argc);
        return 0;
    }
    
    const char* module = luaL_checkstring(L, 1);
    int channel = luaL_checkinteger(L, 2);
    
        if (strcmp(module, "output") == 0) {
            // Check if this is a pulse output (channels 3 & 4 are pulse outputs in logical mapping)
            if (channel == 3 || channel == 4) {
                // Pulse outputs - special handling for 1-bit digital outputs
                int pulse_idx = channel - 3;  // 0-based pulse output index (0 = pulse1, 1 = pulse2)
                
                // Check if arg 3 is a table (ASL action like pulse())
                if (lua_istable(L, 3)) {
                    // For pulse outputs, we need to extract timing from ASL action
                    // pulse(time) creates: {asl._if(polarity, {to(level,0,'now'), to(level,time), to(0,0,'now')}), ...}
                    // We need to extract the 'time' parameter and create a clock-based pulse
                    
                    float pulse_time = extract_pulse_time_from_action(L, 3);
                    
                    // If pulse_time is 0 or very small, just set low immediately without coroutine
                    if (pulse_time < 0.001f) {  // Less than 1ms
                        g_pulse_out_state[pulse_idx] = false;
                        hardware_pulse_output_set(pulse_idx + 1, false);
                        g_pulse_out_coro_id[pulse_idx] = 0;  // Clear any pending coroutine ID
                        lua_settop(L, argc);  // Clean up and return
                        return 0;
                    }
                    
                    // If pulse_time is very long (>100s), treat as constant high
                    if (pulse_time > 100.0f) {
                        g_pulse_out_state[pulse_idx] = true;
                        hardware_pulse_output_set(pulse_idx + 1, true);
                        g_pulse_out_coro_id[pulse_idx] = 0;  // Clear any pending coroutine ID
                        lua_settop(L, argc);  // Clean up and return
                        return 0;
                    }
                    
                    // Now generate the pulse using clock.run
                    // CRITICAL FIX: Cancel any pending pulse coroutine to prevent overlap issues
                    if (g_pulse_out_coro_id[pulse_idx] != 0) {
                        lua_getglobal(L, "clock");
                        if (lua_istable(L, -1)) {
                            lua_getfield(L, -1, "cancel");
                            if (lua_isfunction(L, -1)) {
                                lua_pushinteger(L, g_pulse_out_coro_id[pulse_idx]);
                                lua_pcall(L, 1, 0, 0);  // Ignore errors
                            } else {
                                lua_pop(L, 1);  // pop non-function
                            }
                            lua_pop(L, 1);  // pop clock table
                        } else {
                            lua_pop(L, 1);  // pop non-table
                        }
                        g_pulse_out_coro_id[pulse_idx] = 0;  // Clear tracking
                    }
                    
                    // Set output high immediately
                    g_pulse_out_state[pulse_idx] = true;
                    hardware_pulse_output_set(pulse_idx + 1, true);
                    
                    // Schedule the pulse to go low after pulse_time
                    // Create a unique coroutine ID for this pulse
                    uint32_t coro_id = g_pulse_coro_counter++;
                    g_pulse_out_coro_id[pulse_idx] = coro_id;
                    
                    // Create a Lua coroutine to handle timing
                    // Use _pulse_sleep_and_clear helper instead of luaL_dostring
                    lua_getglobal(L, "clock");
                    if (lua_istable(L, -1)) {
                        lua_getfield(L, -1, "run");
                        if (lua_isfunction(L, -1)) {
                            // Get the helper function
                            lua_getglobal(L, "_pulse_sleep_and_clear");
                            if (lua_isfunction(L, -1)) {
                                // Call: _pulse_sleep_and_clear(channel, coro_id, pulse_time)
                                lua_pushinteger(L, pulse_idx + 1);  // channel
                                lua_pushinteger(L, coro_id);         // coro_id
                                lua_pushnumber(L, pulse_time);       // sleep duration
                                
                                // This creates a closure that clock.run will execute
                                if (lua_pcall(L, 3, 1, 0) == LUA_OK) {
                                    // Stack: clock, run_func, closure
                                    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
                                        lua_pop(L, 1);  // pop error (silent fail)
                                        g_pulse_out_coro_id[pulse_idx] = 0;  // Clear on error
                                    }
                                } else {
                                    lua_pop(L, 1);  // pop error
                                    g_pulse_out_coro_id[pulse_idx] = 0;  // Clear on error
                                }
                            } else {
                                lua_pop(L, 1);  // pop non-function
                                g_pulse_out_coro_id[pulse_idx] = 0;  // Clear on error
                            }
                        } else {
                            lua_pop(L, 1);  // pop non-function
                            g_pulse_out_coro_id[pulse_idx] = 0;  // Clear on error
                        }
                        lua_pop(L, 1);  // pop clock table
                    } else {
                        lua_pop(L, 1);  // pop non-table
                        g_pulse_out_coro_id[pulse_idx] = 0;  // Clear on error
                    }
                } else {
                    // Not a table - could be a direct voltage value
                    float value = (float)luaL_checknumber(L, 3);
                    // Set pulse output based on voltage threshold
                    bool state = (value > 2.5f);  // Consider >2.5V as "high"
                    g_pulse_out_state[pulse_idx] = state;
                    hardware_pulse_output_set(pulse_idx + 1, state);
                }
            } else {
                // Regular CV outputs (channels 1-2 are CV, 3-4 are audio)
                // Must route through LL_set_volts / slope system so that
                // ProcessSample() sees the updated slope state instead of
                // overwriting it with the idle (0V) slope value every sample.
                float value = (float)luaL_checknumber(L, 3);

                // User explicitly setting output should always disable noise
                int ch_idx = channel - 1;
                if (ch_idx >= 0 && ch_idx < 4 && g_noise_active[ch_idx]) {
                    g_noise_active[ch_idx] = false;
                    g_noise_active_mask &= ~(1 << ch_idx);  // Clear bit in mask
                    g_noise_gain[ch_idx] = 0;
                    g_noise_lock_counter[ch_idx] = 0;
                }

                // Route through slope system (like LL_set_volts does) so
                // ProcessSample's slope loop drives the hardware output.
                // hardware_output_set_voltage() alone gets clobbered by the
                // slope loop writing the stale idle value every ~104µs.
                casl_cleardynamics(ch_idx);
                casl_describe_to_literal_q16(ch_idx,
                                             FLOAT_TO_Q16(value),
                                             FLOAT_TO_Q16(0.0f),  // instant slew
                                             SHAPE_Linear);
                casl_action(ch_idx, 1);
            }
    } else {
        // Only 'output' is handled here - all other _c.tell messages (stream, change, window, etc)
        // are handled by l_bootstrap_c_tell and sent as ^^ protocol messages
        printf("_c.tell: unexpected module '%s' (ch=%d) - should be handled by l_bootstrap_c_tell\n\r", module, channel);
    }
    
    return 0;
}

// C-callable wrapper for lua_c_tell (hardware output commands)
// Called from l_bootstrap_c_tell to handle output/stream/change messages
extern "C" int LuaManager_lua_c_tell(lua_State* L) {
    return LuaManager::lua_c_tell(L);
}

// soutput_handler(channel, voltage) - Bridge from C to Lua output callbacks
int LuaManager::lua_soutput_handler(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from C
    float voltage = (float)luaL_checknumber(L, 2);
    
    // Call the Lua soutput_handler function which will invoke output[channel].receive()
    lua_getglobal(L, "soutput_handler");
    if (lua_isfunction(L, -1)) {
        lua_pushinteger(L, channel);
        lua_pushnumber(L, voltage);
        lua_call(L, 2, 0);
    } else {
        // soutput_handler not defined, just log it
        printf("soutput_handler: ch%d=%.3f (no handler)\n\r", channel, voltage);
        lua_pop(L, 1);
    }
    
    return 0;
}

// Crow backend functions for Input.lua compatibility

// io_get_input(channel) - Read input voltage using AudioIn1/AudioIn2
int LuaManager::lua_io_get_input(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);  // 1-based channel from Lua
    float volts = 0.0f;
    
    if (g_blackbird_instance) {
        volts = get_input_state_simple(channel - 1); // Convert to 0-based
    }
    
    lua_pushnumber(L, volts);
    return 1;
}

// Mode-specific detection callbacks - OPTIMIZED for direct execution
static constexpr bool kDetectionDebug = false;

// Lock-free stream callback - posts sample-accurate values
static void stream_callback(int channel, float value) {
    // Keep .volts queries aligned with the last streamed sample
    if (channel >= 0 && channel < 2) {
        g_input_stream_volts[channel] = value;
    }

    // Post to lock-free queue with the provided value
    if (!input_lockfree_post(channel, value, 1)) {  // type=1 for stream
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Stream lock-free queue full, dropped %lu events", drop_count);
        }
    }
}

// Shared state for change callback duplicate suppression - MUST BE SHARED!
static int8_t g_change_last_reported_state[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };

// Reset function for change callback state - called when input modes change
static void reset_change_callback_state(int channel) {
    if (channel >= 0 && channel < 8) {
        g_change_last_reported_state[channel] = -1;
    }
}

// Lock-free change callback - posts to queue without blocking ISR
static void change_callback(int channel, float value) {
    bool state = (value > 0.5f);
    
    // Coalesce for CV inputs 1/2 (channels 0/1) to avoid backlog when slammed at audio rate
    if (channel >= 0 && channel < 2) {
        if (g_change_pending[channel] == 0) {
            g_change_pending[channel] = 1;
        } else {
            g_change_overflow_count[channel]++;
        }
        g_change_state[channel] = state;
        g_change_last_reported_state[channel] = (int8_t)state;
        return; // Don't post to lock-free queue for coalesced channels
    }

    // Fallback for any future channels: post to lock-free queue
    if (!input_lockfree_post(channel, value, 0)) {  // type=0 for change
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Change lock-free queue full, dropped %lu events", drop_count);
        }
    }
}

// Window callback - called when input crosses window boundaries
static void window_callback(int channel, float value) {
    // Value contains signed window index (positive=up, negative=down)
    if (!input_lockfree_post(channel, value, 2)) {  // type=2 for window
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Window lock-free queue full, dropped %lu events", drop_count);
        }
    }
}

static void scale_callback(int channel, float value) {
    Detect_t* detector = Detect_ix_to_p(channel);
    if (!detector) return;
    
    input_event_lockfree_t event;
    event.channel = channel;
    event.value = value;
    event.detection_type = 3;  // type=3 for scale
    event.timestamp_us = time_us_32();
    event.extra.scale.index = detector->scale.lastIndex;
    event.extra.scale.octave = detector->scale.lastOct;
    event.extra.scale.note = detector->scale.lastNote;
    event.extra.scale.volts = detector->scale.lastVolts;
    
    // Post the full event with scale data
    extern bool input_lockfree_post_extended(const input_event_lockfree_t* event);
    if (!input_lockfree_post_extended(&event)) {
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Scale lock-free queue full, dropped %lu events", drop_count);
        }
    }
}

// Volume callback - called periodically with RMS level
static void volume_callback(int channel, float level) {
    // OPTIMIZATION: Time-based batching for volume mode
    static float last_value[8] = {0};
    static uint32_t last_post_time[8] = {0};
    
    uint32_t now = time_us_32();
    float delta = fabsf(level - last_value[channel]);
    uint32_t time_since_post = now - last_post_time[channel];
    
    // Post if significant change (>5mV) OR timeout (5ms)
    bool significant_change = (delta > 0.005f);  // 5mV threshold
    bool timeout = (time_since_post > 5000);     // 5ms timeout
    
    if (significant_change || timeout) {
        if (input_lockfree_post(channel, level, 4)) {  // type=4 for volume
            last_value[channel] = level;
            last_post_time[channel] = now;
        } else {
            static uint32_t drop_count = 0;
            if (++drop_count % 100 == 0) {
                queue_debug_message("Volume lock-free queue full, dropped %lu events", drop_count);
            }
        }
    }
}

// Peak callback - called when envelope exceeds threshold
static void peak_callback(int channel, float value) {
    if (!input_lockfree_post(channel, 0.0f, 5)) {  // type=5 for peak, no value needed
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Peak lock-free queue full, dropped %lu events", drop_count);
        }
    }
}

// Freq callback - called periodically with frequency estimate
static void freq_callback(int channel, float freq) {
    if (!input_lockfree_post(channel, freq, 6)) {  // type=6 for freq
        static uint32_t drop_count = 0;
        if (++drop_count % 100 == 0) {
            queue_debug_message("Freq lock-free queue full, dropped %lu events", drop_count);
        }
    }
}

// ============================================================================
// EVENT HANDLERS (Core-Safe Callbacks)
// ============================================================================

// Lock-free input event handler - processes detection events from lock-free queue
extern "C" void L_handle_input_lockfree(input_event_lockfree_t* event) {
    LuaManager* lua_mgr = LuaManager::getInstance();
    if (!lua_mgr) return;
    if (!lua_mgr->L) return;
    
    // ===============================================
    // OPTIMIZATION 2: Enable batching for input callbacks
    // ===============================================
    output_batch_begin();
    
    int channel = event->channel + 1;  // Convert to 1-based for Lua
    float value = event->value;
    int detection_type = event->detection_type;
    
    // LED indicator for lock-free input processing
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_on(0);
    }
    
    // Call appropriate Lua handler based on detection type
    // IMPORTANT: Call input[n].stream() / input[n].change() / input[n].window() etc. methods
    // which internally call _c.tell() to send ^^stream, ^^change, ^^window messages to druid/norns
    // NOTE: Avoid luaL_loadstring() here (it allocates heavily and can lead to long-run OOM).
    lua_State* L = lua_mgr->L;
    lua_getglobal(L, "input");
    if (lua_istable(L, -1)) {
        lua_rawgeti(L, -1, channel);
        if (lua_istable(L, -1)) {
            const char* method = nullptr;
            int nargs = 0;
            bool pushed_method_value = false;

            if (detection_type == 1) {
                method = "stream";
                lua_getfield(L, -1, method);
                if (lua_toboolean(L, -1)) {
                    pushed_method_value = true;
                    lua_pushnumber(L, value);
                    nargs = 1;
                } else {
                    lua_pop(L, 1);
                }
            } else if (detection_type == 0) {
                method = "change";
                bool state = (value > 0.5f);
                lua_getfield(L, -1, method);
                if (lua_toboolean(L, -1)) {
                    pushed_method_value = true;
                    lua_pushboolean(L, state);
                    nargs = 1;
                } else {
                    lua_pop(L, 1);
                }
            } else if (detection_type == 2) {
                method = "window";
                int win = (int)fabsf(value);
                bool dir = (value > 0);
                lua_getfield(L, -1, method);
                if (lua_toboolean(L, -1)) {
                    pushed_method_value = true;
                    lua_pushinteger(L, win);
                    lua_pushboolean(L, dir);
                    nargs = 2;
                } else {
                    lua_pop(L, 1);
                }
            } else if (detection_type == 3) {
                method = "scale";
                lua_getfield(L, -1, method);
                if (lua_toboolean(L, -1)) {
                    pushed_method_value = true;
                    lua_createtable(L, 0, 4);
                    lua_pushinteger(L, event->extra.scale.index + 1); // Convert to 1-based
                    lua_setfield(L, -2, "index");
                    lua_pushinteger(L, event->extra.scale.octave);
                    lua_setfield(L, -2, "octave");
                    lua_pushnumber(L, event->extra.scale.note);
                    lua_setfield(L, -2, "note");
                    lua_pushnumber(L, event->extra.scale.volts);
                    lua_setfield(L, -2, "volts");
                    nargs = 1;
                } else {
                    lua_pop(L, 1);
                }
            } else if (detection_type == 4) {
                method = "volume";
                lua_getfield(L, -1, method);
                if (lua_toboolean(L, -1)) {
                    pushed_method_value = true;
                    lua_pushnumber(L, value);
                    nargs = 1;
                } else {
                    lua_pop(L, 1);
                }
            } else if (detection_type == 5) {
                method = "peak";
                lua_getfield(L, -1, method);
                if (lua_toboolean(L, -1)) {
                    pushed_method_value = true;
                    nargs = 0;
                } else {
                    lua_pop(L, 1);
                }
            } else if (detection_type == 6) {
                method = "freq";
                lua_getfield(L, -1, method);
                if (lua_toboolean(L, -1)) {
                    pushed_method_value = true;
                    lua_pushnumber(L, value);
                    nargs = 1;
                } else {
                    lua_pop(L, 1);
                }
            }

            if (pushed_method_value) {
                if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
                    const char* err = lua_tostring(L, -1);
                    tud_cdc_write_str("lua runtime error: ");
                    tud_cdc_write_str(err ? err : "unknown error");
                    tud_cdc_write_str("\n\r");
                    tud_cdc_write_flush();
                    lua_pop(L, 1);
                }
            }
        }
        lua_pop(L, 1); // pop input[channel]
    }
    lua_pop(L, 1); // pop input
    
    if (kDetectionDebug) {
        printf("LOCKFREE INPUT: ch%d type=%d value=%.3f\n\r",
               channel, detection_type, value);
    }
    
    // Flush batched outputs after Lua callback execution
    output_batch_flush();
    
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->debug_led_off(0);
    }
}

// L_queue_asl_done function - queues ASL completion event using lock-free queue
extern "C" void L_queue_asl_done(int channel) {
    // Use lock-free queue for Core 1 -> Core 0 communication (called from slope callback)
    if (!asl_done_lockfree_post(channel)) {
        printf("Warning: ASL done lock-free queue full for channel %d\n\r", channel + 1);
    }
}

// ============================================================================
// INPUT MODE FUNCTIONS (Lua Bindings for Detection System)
// ============================================================================

// Helper function to check if an input is connected (for normalization probe)
// Only applies to CV inputs (channels 1-2) - pulse inputs handled separately
static bool is_input_connected(int channel) {
    BlackbirdCrow* card = (BlackbirdCrow*)g_blackbird_instance;
    if (!card) return true; // If no card instance, assume connected (safe default)
    
    // Map input[] channels to ComputerCard Input enum
    // input[1] → CV1, input[2] → CV2
    ComputerCard::Input input;
    if (channel == 1) {
        input = ComputerCard::Input::CV1;
    } else if (channel == 2) {
        input = ComputerCard::Input::CV2;
    } else {
        // Only channels 1-2 use the detection system for CV inputs
        return true; // Unknown channel, assume connected
    }
    
    return card->IsInputConnected(input);
}

// Input mode functions - connect to detection system with mode-specific callbacks
int LuaManager::lua_set_input_stream(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);

    if (channel >= 1 && channel <= 2) {
        g_input_clock_mode[channel - 1] = false;
    }
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_stream(detector, stream_callback, time);
    }
    return 0;
}

int LuaManager::lua_set_input_change(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float threshold = (float)luaL_checknumber(L, 2);
    float hysteresis = (float)luaL_checknumber(L, 3);
    const char* direction = luaL_checkstring(L, 4);

    if (channel >= 1 && channel <= 2) {
        g_input_clock_mode[channel - 1] = false;
    }
    
    // CRITICAL FIX: Reset callback state when mode changes to allow new callbacks to fire
    reset_change_callback_state(channel - 1);
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        // Always configure detection; normalization probe already zeroes disconnected inputs
        int8_t dir = Detect_str_to_dir(direction);
        Detect_change(detector, change_callback, threshold, hysteresis, dir);
    }
    return 0;
}

int LuaManager::lua_set_input_window(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);

    if (channel >= 1 && channel <= 2) {
        g_input_clock_mode[channel - 1] = false;
    }
    
    // Extract windows array from Lua table
    if (!lua_istable(L, 2)) {
        printf("set_input_window: windows must be a table\n\r");
        return 0;
    }
    
    float hysteresis = (float)luaL_checknumber(L, 3);
    
    // Get table length
    int wLen = lua_rawlen(L, 2);
    if (wLen > WINDOW_MAX_COUNT) wLen = WINDOW_MAX_COUNT;
    
    float windows[WINDOW_MAX_COUNT];
    for (int i = 1; i <= wLen; i++) {
        lua_rawgeti(L, 2, i);
        windows[i-1] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        // Always configure detection; normalization probe already zeroes disconnected inputs
        Detect_window(detector, window_callback, windows, wLen, hysteresis);
    }
    return 0;
}

int LuaManager::lua_set_input_scale(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);

    if (channel >= 1 && channel <= 2) {
        g_input_clock_mode[channel - 1] = false;
    }
    
    // Extract scale array from Lua table
    float scale[SCALE_MAX_COUNT];
    int sLen = 0;
    
    if (lua_istable(L, 2)) {
        sLen = lua_rawlen(L, 2);
        if (sLen > SCALE_MAX_COUNT) sLen = SCALE_MAX_COUNT;
        
        for (int i = 1; i <= sLen; i++) {
            lua_rawgeti(L, 2, i);
            scale[i-1] = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        }
    }
    
    float temp = (float)luaL_checknumber(L, 3);    // Temperament (divisions)
    float scaling = (float)luaL_checknumber(L, 4); // Voltage scaling
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        // Always configure detection; normalization probe already zeroes disconnected inputs
        Detect_scale(detector, scale_callback, scale, sLen, temp, scaling);
    }
    return 0;
}

int LuaManager::lua_set_input_volume(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);

    if (channel >= 1 && channel <= 2) {
        g_input_clock_mode[channel - 1] = false;
    }
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_volume(detector, volume_callback, time);
    }
    return 0;
}

int LuaManager::lua_set_input_peak(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float threshold = (float)luaL_checknumber(L, 2);
    float hysteresis = (float)luaL_checknumber(L, 3);

    if (channel >= 1 && channel <= 2) {
        g_input_clock_mode[channel - 1] = false;
    }
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_peak(detector, peak_callback, threshold, hysteresis);
    }
    return 0;
}

int LuaManager::lua_set_input_freq(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);

    if (channel >= 1 && channel <= 2) {
        g_input_clock_mode[channel - 1] = false;
    }
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        Detect_freq(detector, freq_callback, time);
    }
    return 0;
}

int LuaManager::lua_set_input_clock(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    float div = (float)luaL_checknumber(L, 2);
    float threshold = (float)luaL_checknumber(L, 3);
    float hysteresis = (float)luaL_checknumber(L, 4);

    if (channel >= 1 && channel <= 2) {
        g_input_clock_mode[channel - 1] = true;
        g_input_clock_div[channel - 1] = (div > 0.0f) ? div : 1.0f;
    }
    
    // Clock mode is a specialized change detection
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        // Always configure detection; normalization probe already zeroes disconnected inputs
        // Set clock to external input source and configure division
        clock_set_source(CLOCK_SOURCE_CROW);
        clock_crow_in_div(div);
        
        // Use clock_input_handler instead of generic change_callback
        Detect_change(detector, clock_input_handler, threshold, hysteresis, 1); // Rising edge
    }
    return 0;
}

int LuaManager::lua_set_input_none(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);

    if (channel >= 1 && channel <= 2) {
        g_input_clock_mode[channel - 1] = false;
    }
    
    Detect_t* detector = Detect_ix_to_p(channel - 1); // Convert to 0-based
    if (detector) {
        // ATOMIC MODE SWITCHING: Set flag to prevent callback corruption
        detector->mode_switching = true;
        
        // Clear detection mode safely
        Detect_none(detector);
        
        // Clear flag after mode change is complete
        detector->mode_switching = false;
    }
    return 0;
}

// ============================================================================
// METRO & CLOCK SYSTEM (Lua Bindings)
// ============================================================================

// Metro system Lua bindings implementation

// metro_start(id, time) - Start a metro with specified interval
int LuaManager::lua_metro_start(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    
    // Set time first, then start (crow-style)
    Metro_set_time(id, time);
    Metro_start(id);
    return 0;
}

// metro_stop(id) - Stop a metro
int LuaManager::lua_metro_stop(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    
    // Call C metro backend
    Metro_stop(id);
    return 0;
}

// metro_set_time(id, time) - Change metro interval
int LuaManager::lua_metro_set_time(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    float time = (float)luaL_checknumber(L, 2);
    
    // Call C metro backend
    Metro_set_time(id, time);
    return 0;
}

// metro_set_count(id, count) - Set metro repeat count
int LuaManager::lua_metro_set_count(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    int count = luaL_checkinteger(L, 2);
    
    // Call C metro backend
    Metro_set_count(id, count);
    return 0;
}

// Clock system Lua bindings implementation

// clock_cancel(coro_id) - Cancel a running clock coroutine
int LuaManager::lua_clock_cancel(lua_State* L) {
    int coro_id = (int)luaL_checkinteger(L, 1);
    clock_cancel_coro(coro_id);
    lua_pop(L, 1);
    return 0;
}

// clock_schedule_sleep(coro_id, seconds) - Schedule coroutine resume after seconds
int LuaManager::lua_clock_schedule_sleep(lua_State* L) {
    int coro_id = (int)luaL_checkinteger(L, 1);
    float seconds = (float)luaL_checknumber(L, 2);
    
    if (seconds <= 0) {
        L_queue_clock_resume(coro_id); // immediate callback
    } else {
        clock_schedule_resume_sleep(coro_id, seconds);
    }
    lua_pop(L, 2);
    return 0;
}

// clock_schedule_sync(coro_id, beats) - Schedule coroutine resume at beat sync point
int LuaManager::lua_clock_schedule_sync(lua_State* L) {
    int coro_id = (int)luaL_checkinteger(L, 1);
    float beats = (float)luaL_checknumber(L, 2);
    
    if (beats <= 0) {
        L_queue_clock_resume(coro_id); // immediate callback
    } else {
        clock_schedule_resume_sync(coro_id, beats);
    }
    lua_pop(L, 2);
    return 0;
}

// clock_schedule_beat(coro_id, beats) - Schedule coroutine resume after beats (not synced)
int LuaManager::lua_clock_schedule_beat(lua_State* L) {
    int coro_id = (int)luaL_checkinteger(L, 1);
    float beats = (float)luaL_checknumber(L, 2);
    
    if (beats <= 0) {
        L_queue_clock_resume(coro_id); // immediate callback
    } else {
        clock_schedule_resume_beatsync(coro_id, beats);
    }
    lua_pop(L, 2);
    return 0;
}

// clock_get_time_beats() - Get current time in beats
int LuaManager::lua_clock_get_time_beats(lua_State* L) {
    lua_pushnumber(L, clock_get_time_beats());
    return 1;
}

// clock_get_tempo() - Get current tempo in BPM
int LuaManager::lua_clock_get_tempo(lua_State* L) {
    lua_pushnumber(L, clock_get_tempo());
    return 1;
}

// clock_set_source(source) - Set clock source (1-based in Lua)
int LuaManager::lua_clock_set_source(lua_State* L) {
    int source = (int)luaL_checkinteger(L, 1);
    clock_set_source((clock_source_t)(source - 1)); // Convert to 0-based
    lua_pop(L, 1);
    return 0;
}

// clock_internal_set_tempo(bpm) - Set internal clock tempo
int LuaManager::lua_clock_internal_set_tempo(lua_State* L) {
    float bpm = (float)luaL_checknumber(L, 1);
    clock_internal_set_tempo(bpm);
    lua_pop(L, 1);
    return 0;
}

// clock_internal_start(new_beat) - Start internal clock at specified beat
int LuaManager::lua_clock_internal_start(lua_State* L) {
    float new_beat = (float)luaL_checknumber(L, 1);
    clock_set_source(CLOCK_SOURCE_INTERNAL);
    clock_internal_start(new_beat, true);
    lua_pop(L, 1);
    return 0;
}

// clock_internal_stop() - Stop internal clock
int LuaManager::lua_clock_internal_stop(lua_State* L) {
    clock_set_source(CLOCK_SOURCE_INTERNAL);
    clock_internal_stop();
    return 0;
}

// Implementation of lua_unique_card_id function (after BlackbirdCrow class is fully defined)
int LuaManager::lua_unique_card_id(lua_State* L) {
    // Get the cached ID from the global instance
    if (g_blackbird_instance) {
        lua_pushinteger(L, (lua_Integer)((BlackbirdCrow*)g_blackbird_instance)->cached_unique_id);
        return 1;
    }
    
    lua_pushinteger(L, 0);
    return 1;
}

// Implementation of lua_unique_id function - crow-compatible version
// Returns 3 integers like crow's unique_id() which returns getUID_Word(0), getUID_Word(4), getUID_Word(8)
int LuaManager::lua_unique_id(lua_State* L) {
    // Get the cached 64-bit ID from the global instance
    if (g_blackbird_instance) {
        uint64_t id = ((BlackbirdCrow*)g_blackbird_instance)->cached_unique_id;
        
        // Split the 64-bit ID into three parts to mimic crow's behavior
        // Crow returns 3 32-bit words from different offsets of the STM32 UID
        // We'll split our 64-bit value into 3 parts with some bit mixing
        uint32_t word0 = (uint32_t)(id & 0xFFFFFFFF);           // Lower 32 bits
        uint32_t word1 = (uint32_t)((id >> 32) & 0xFFFFFFFF);   // Upper 32 bits
        uint32_t word2 = (uint32_t)(word0 ^ word1);             // XOR for third word
        
        lua_pushinteger(L, (lua_Integer)word0);
        lua_pushinteger(L, (lua_Integer)word1);
        lua_pushinteger(L, (lua_Integer)word2);
        return 3;
    }
    
    // Return zeros if no instance available
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    lua_pushinteger(L, 0);
    return 3;
}

// Public view tracking - global state for monitoring channels
static bool g_view_chans[6] = {false, false, false, false, false, false};
static float g_last_view_values[6] = {-6.0f, -6.0f, -6.0f, -6.0f, -6.0f, -6.0f};

// pub_view_in(channel, state) - Enable/disable viewing of input channel
int LuaManager::lua_pub_view_in(lua_State* L) {
    int chan = luaL_checkinteger(L, 1) - 1; // lua is 1-based
    bool state;
    if (lua_isboolean(L, 2)) { 
        state = lua_toboolean(L, 2); 
    } else { 
        state = (bool)lua_tointeger(L, 2); 
    }
    
    // Inputs are channels 4-5 in the view_chans array (outputs are 0-3)
    int view_idx = chan + 4;
    if (view_idx >= 0 && view_idx < 6) {
        g_view_chans[view_idx] = state;
        if (state) {
            g_last_view_values[view_idx] = -6.0f; // reset to force update
        }
    }
    
    lua_pop(L, 2);
    return 0;
}

// pub_view_out(channel, state) - Enable/disable viewing of output channel
int LuaManager::lua_pub_view_out(lua_State* L) {
    int chan = luaL_checkinteger(L, 1) - 1; // lua is 1-based
    bool state;
    if (lua_isboolean(L, 2)) { 
        state = lua_toboolean(L, 2); 
    } else { 
        state = (bool)lua_tointeger(L, 2); 
    }
    
    // Outputs are channels 0-3 in the view_chans array
    if (chan >= 0 && chan < 4) {
        g_view_chans[chan] = state;
        if (state) {
            g_last_view_values[chan] = -6.0f; // reset to force update
        }
    }
    
    lua_pop(L, 2);
    return 0;
}

// public_update() - Send ^^pubview messages to norns/druid for monitored channels
// Called at ~15fps from main loop
static void public_update() {
    const float VDIFF = 0.1f; // 100mV hysteresis
    static int chan = 0; // rotate through channels 0-5
    
    if (g_view_chans[chan]) {
        float new_val;
        char msg_buf[64]; // Buffer for the message
        
        if (chan < 4) { // outputs
            new_val = S_get_state(chan);
            if (new_val + VDIFF < g_last_view_values[chan] ||
                new_val - VDIFF > g_last_view_values[chan]) {
                g_last_view_values[chan] = new_val;
                
                // Format the pubview message
                int len = snprintf(msg_buf, sizeof(msg_buf), 
                                  "^^pubview('output',%d,%g)\n\r", 
                                  chan + 1, new_val);
                
                // Write to buffer - batched flush happens every 2ms in main loop
                if (len > 0 && tud_cdc_connected()) {
                    tud_cdc_write(msg_buf, len);
                }
            }
        } else { // inputs (4-5 -> 0-1)
            int input_chan = chan - 4;
            new_val = get_input_state_simple(input_chan);
            if (new_val + VDIFF < g_last_view_values[chan] ||
                new_val - VDIFF > g_last_view_values[chan]) {
                g_last_view_values[chan] = new_val;
                
                // Format the pubview message
                int len = snprintf(msg_buf, sizeof(msg_buf), 
                                  "^^pubview('input',%d,%g)\n\r", 
                                  input_chan + 1, new_val);
                
                // Write to buffer - batched flush happens every 2ms in main loop
                if (len > 0 && tud_cdc_connected()) {
                    tud_cdc_write(msg_buf, len);
                }
            }
        }
    }
    
    chan = (chan + 1) % 6; // round-robin through all 6 channels
}

// tell(event, ...) - Send ^^event messages to druid/norns
// This is the core function that input.stream() and input.change() use
int LuaManager::lua_tell(lua_State* L) {
    int nargs = lua_gettop(L);
    
    // Need at least the event name
    if (nargs == 0) {
        return luaL_error(L, "tell: no event name provided");
    }
    
    switch(nargs) {
        case 1:
            Caw_printf("^^%s()", luaL_checkstring(L, 1));
            break;
        case 2:
            Caw_printf("^^%s(%s)", luaL_checkstring(L, 1),
                                   luaL_checkstring(L, 2));
            break;
        case 3:
            Caw_printf("^^%s(%s,%s)", luaL_checkstring(L, 1),
                                      luaL_checkstring(L, 2),
                                      luaL_checkstring(L, 3));
            break;
        case 4:
            Caw_printf("^^%s(%s,%s,%s)", luaL_checkstring(L, 1),
                                         luaL_checkstring(L, 2),
                                         luaL_checkstring(L, 3),
                                         luaL_checkstring(L, 4));
            break;
        case 5:
            Caw_printf("^^%s(%s,%s,%s,%s)", luaL_checkstring(L, 1),
                                            luaL_checkstring(L, 2),
                                            luaL_checkstring(L, 3),
                                            luaL_checkstring(L, 4),
                                            luaL_checkstring(L, 5));
            break;
        default:
            return luaL_error(L, "tell: too many arguments (max 5)");
    }
    
    lua_pop(L, nargs);
    lua_settop(L, 0);
    return 0;
}

// get_out(channel) - Emit a crow-style ^^output(channel,value) message
// Used by some hosts to query current output state at boot.
int LuaManager::lua_get_out(lua_State* L) {
    int chan = luaL_checkinteger(L, 1);
    if (chan < 1 || chan > 4) {
        lua_settop(L, 0);
        return 0;
    }
    // In this firmware outputs are driven by the slopes system; use its state.
    Caw_printf("^^output(%i,%f)", chan, (float)S_get_state(chan - 1));
    lua_settop(L, 0);
    return 0;
}

// get_cv(channel) - Emit a crow-style ^^stream(channel,value) message
// Used by some hosts to query current input (stream-equivalent) state at boot.
int LuaManager::lua_get_cv(lua_State* L) {
    int chan = luaL_checkinteger(L, 1);
    if (chan < 1 || chan > 2) {
        lua_settop(L, 0);
        return 0;
    }
    Caw_printf("^^stream(%i,%f)", chan, get_input_state_simple(chan - 1));
    lua_settop(L, 0);
    return 0;
}

// hardware_pulse(channel, state) - Set pulse output state
// channel: 1 or 2
// state: boolean (true = high, false = low)
int LuaManager::lua_hardware_pulse(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    bool state = lua_toboolean(L, 2);
    
    if (channel < 1 || channel > 2) {
        return luaL_error(L, "hardware_pulse: channel must be 1 or 2");
    }
    
    hardware_pulse_output_set(channel, state);
    return 0;
}

// _pulse_coro_check(channel, coro_id) - Verify if coroutine is still valid
// Returns true if this coroutine should still control the pulse output
// Returns false if a newer pulse has replaced this one (prevents overlap issues)
int LuaManager::lua_pulse_coro_check(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    uint32_t coro_id = (uint32_t)luaL_checkinteger(L, 2);
    
    if (channel < 1 || channel > 2) {
        lua_pushboolean(L, false);
        return 1;
    }
    
    int idx = channel - 1;
    bool is_current = (g_pulse_out_coro_id[idx] == coro_id);
    
    // Clear the tracking if this coroutine is finishing
    if (is_current) {
        g_pulse_out_coro_id[idx] = 0;
    }
    
    lua_pushboolean(L, is_current);
    return 1;
}

// Hardware knob access functions - return normalized 0.0-1.0 values
// (Integer to float conversion happens here, outside ISR)
int LuaManager::lua_get_knob_main(lua_State* L) {
    if (g_blackbird_instance) {
        BlackbirdCrow* crow = (BlackbirdCrow*)g_blackbird_instance;
        int value = crow->GetKnobValue(ComputerCard::Knob::Main);
        float value_f = value / 4095.0; // Convert to 0.0-1.0
       if (value_f < 0.01) {
            value_f = 0.0;
        } else if (value_f > 0.99) {
            value_f = 1.0; 
        }
        lua_pushnumber(L, value_f);  
        return 1;
    }
    lua_pushnumber(L, 0.0);
    return 1;
}

int LuaManager::lua_get_knob_x(lua_State* L) {
    if (g_blackbird_instance) {
        BlackbirdCrow* crow = (BlackbirdCrow*)g_blackbird_instance;
        int value = crow->GetKnobValue(ComputerCard::Knob::X);
        float value_f = value / 4095.0; // Convert to 0.0-1.0
       if (value_f < 0.01) {
            value_f = 0.0;
        } else if (value_f > 0.99) {
            value_f = 1.0; 
        }
        lua_pushnumber(L, value_f);  
        return 1;
    }
    lua_pushnumber(L, 0.0);
    return 1;
}

int LuaManager::lua_get_knob_y(lua_State* L) {
    if (g_blackbird_instance) {
        BlackbirdCrow* crow = (BlackbirdCrow*)g_blackbird_instance;
        int value = crow->GetKnobValue(ComputerCard::Knob::Y);
       float value_f = value / 4095.0; // Convert to 0.0-1.0
       if (value_f < 0.01) {
            value_f = 0.0;
        } else if (value_f > 0.99) {
            value_f = 1.0; 
        }
        lua_pushnumber(L, value_f);  
        return 1;
    }
    lua_pushnumber(L, 0.0);
    return 1;
}

// Hardware switch position query - returns 'down', 'middle', or 'up'
int LuaManager::lua_get_switch_position(lua_State* L) {
    if (g_blackbird_instance) {
        BlackbirdCrow* crow = (BlackbirdCrow*)g_blackbird_instance;
        ComputerCard::Switch pos = crow->GetSwitchValue();
        const char* pos_str = (pos == ComputerCard::Switch::Down) ? "down" :
                              (pos == ComputerCard::Switch::Middle) ? "middle" : "up";
        lua_pushstring(L, pos_str);
        return 1;
    }
    lua_pushstring(L, "middle");
    return 1;
}

// Implementation of lua_memstats (memory statistics and debugging)
int LuaManager::lua_memstats(lua_State* L) {
    if (!tud_cdc_connected()) return 0;
    
    int kb_used = lua_gc(L, LUA_GCCOUNT, 0);
    int bytes = lua_gc(L, LUA_GCCOUNTB, 0);
    float total_kb = kb_used + (bytes / 1024.0f);
    
    // Use TinyUSB CDC functions for output (printf doesn't work with TinyUSB)
    char buffer[128];
    
    tud_cdc_write_str("Lua Memory Usage:\n\r");
    
    
    snprintf(buffer, sizeof(buffer), "  Current: %.2f KB (%d KB + %d bytes)\n\r", 
             total_kb, kb_used, bytes);
    tud_cdc_write_str(buffer);
    
    
    // Trigger a GC cycle and report change
    lua_gc(L, LUA_GCCOLLECT, 0);
    int kb_after = lua_gc(L, LUA_GCCOUNT, 0);
    int bytes_after = lua_gc(L, LUA_GCCOUNTB, 0);
    float total_after = kb_after + (bytes_after / 1024.0f);
    float freed = total_kb - total_after;
    
    snprintf(buffer, sizeof(buffer), "  After GC: %.2f KB (freed %.2f KB)\n\r", 
             total_after, freed);
    tud_cdc_write_str(buffer);
    
    
    return 0;
}

// ============================================================================
// UTILITY & DIAGNOSTIC FUNCTIONS (Lua Bindings)
// ============================================================================

// Implementation of lua_perf_stats (performance monitoring statistics)
// Usage: perf_stats()              -- prints formatted stats, resets counters
//        perf_stats(false)         -- prints formatted stats, doesn't reset
//        val = perf_stats("raw")   -- returns ProcessSample raw value (microseconds), resets
//        val = perf_stats("raw", false) -- returns ProcessSample raw value, doesn't reset
int LuaManager::lua_perf_stats(lua_State* L) {
    int nargs = lua_gettop(L);
    bool reset = true;
    bool raw_mode = false;
    
    // Parse arguments
    if (nargs >= 1) {
        if (lua_isstring(L, 1)) {
            const char* mode = lua_tostring(L, 1);
            if (strcmp(mode, "raw") == 0) {
                raw_mode = true;
            }
            // Check for second argument (reset flag)
            if (nargs >= 2) {
                reset = lua_toboolean(L, 2);
            }
        } else {
            // First arg is boolean (reset flag)
            reset = lua_toboolean(L, 1);
        }
    }
    
    // Get current worst-case values
    uint32_t worst = g_worst_case_us;
    uint32_t overruns = g_overrun_count;
    uint32_t loop_worst = g_loop_worst_case_us;
    uint32_t loop_count = g_loop_iteration_count;
    
    if (raw_mode) {
        // Raw mode: just return ProcessSample value for backwards compatibility
        lua_pushinteger(L, worst);
        if (reset) {
            g_worst_case_us = 0;
            g_loop_worst_case_us = 0;
            g_loop_iteration_count = 0;
        }
        return 1;
    } else {
        // Default mode: print formatted output via TinyUSB CDC
        if (tud_cdc_connected()) {
            const float utilization = (kProcessSampleBudgetUs > 0.0f)
                ? (static_cast<float>(worst) / kProcessSampleBudgetUs) * 100.0f
                : 0.0;
            char msg[512];
            snprintf(msg, sizeof(msg), 
                     "ProcessSample Performance (Core 1):\n\r"
                     "  Worst case: %lu microseconds\n\r"
                     "  Budget: %.1fus (%.0fHz sample rate)\n\r"
                     "  Utilization: %.1f%%\n\r"
                     "  Overruns (>=%.1fus): %lu\n\r"
                     "\n\r"
                     "MainControlLoop Performance (Core 0):\n\r"
                     "  Worst case: %lu microseconds\n\r"
                     "  Total iterations: %lu\n\r",
                     (unsigned long)worst,
                     kProcessSampleBudgetUs,
                     kProcessSampleRateHz,
                     utilization,
                     kProcessSampleBudgetUs,
                     (unsigned long)overruns,
                     (unsigned long)loop_worst,
                     (unsigned long)loop_count);
            tud_cdc_write_str(msg);

            // Event queue stats
            char qmsg[512];
            snprintf(qmsg, sizeof(qmsg),
                     "Queues:\n\r"
                     "  Metro: depth=%lu posted=%lu processed=%lu dropped=%lu coalesced=%lu\n\r"
                     "  Clock: depth=%lu posted=%lu processed=%lu dropped=%lu coalesced=%lu\n\r"
                     "  Input: depth=%lu posted=%lu processed=%lu dropped=%lu\n\r"
                     "  ASL  : depth=%lu posted=%lu processed=%lu dropped=%lu\n\r",
                     (unsigned long)metro_lockfree_queue_depth(),
                     (unsigned long)metro_events_posted_count(),
                     (unsigned long)metro_events_processed_count(),
                     (unsigned long)metro_events_dropped_count(),
                     (unsigned long)metro_events_coalesced_count(),
                     (unsigned long)clock_lockfree_queue_depth(),
                     (unsigned long)clock_events_posted_count(),
                     (unsigned long)clock_events_processed_count(),
                     (unsigned long)clock_events_dropped_count(),
                     (unsigned long)clock_events_coalesced_count(),
                     (unsigned long)input_lockfree_queue_depth(),
                     (unsigned long)input_events_posted_count(),
                     (unsigned long)input_events_processed_count(),
                     (unsigned long)input_events_dropped_count(),
                     (unsigned long)asl_done_lockfree_queue_depth(),
                     (unsigned long)asl_done_events_posted_count(),
                     (unsigned long)asl_done_events_processed_count(),
                     (unsigned long)asl_done_events_dropped_count());
            tud_cdc_write_str(qmsg);

            // Callback timing stats (metros & clock resumes)
            char cbmsg[256];
            snprintf(cbmsg, sizeof(cbmsg),
                     "Callbacks:\n\r"
                     "  Metro: last=%luus worst=%luus overruns=%lu\n\r"
                     "  Clock: last=%luus worst=%luus\n\r",
                     (unsigned long)metro_cb_last_us(),
                     (unsigned long)metro_cb_worst_us(),
                     (unsigned long)metro_cb_overrun_count(),
                     (unsigned long)clock_resume_cb_last_us(),
                     (unsigned long)clock_resume_cb_worst_us());
            tud_cdc_write_str(cbmsg);

            // Ensure all stats are transmitted promptly
            tud_cdc_write_flush();
            
        }
        
        // Optionally reset the trackers
        if (reset) {
            g_worst_case_us = 0;
            g_loop_worst_case_us = 0;
            g_loop_iteration_count = 0;
            events_lockfree_reset_stats();
            metro_cb_reset_stats();
            clock_resume_cb_reset_stats();
        }
        
        return 0;  // No return value in print mode
    }
}

// Implementation of lua_clock_stats
// Usage: stats = clock_stats()       -- returns table of counters and does not reset by default
//        stats = clock_stats(true)   -- returns table and resets counters
int LuaManager::lua_clock_stats(lua_State* L) {
    bool reset = false;
    int nargs = lua_gettop(L);
    if (nargs >= 1) {
        reset = lua_toboolean(L, 1);
    }

    // Gather stats
    uint32_t sched_fail = clock_get_schedule_failures();
    uint32_t sched_succ = clock_get_schedule_successes();
    uint32_t max_active = clock_get_max_active_threads();
    uint32_t pool_cap   = clock_get_pool_capacity();
    uint32_t posted     = clock_events_posted_count();
    uint32_t processed  = clock_events_processed_count();
    uint32_t dropped    = clock_events_dropped_count();
    uint32_t depth      = clock_lockfree_queue_depth();
    extern int sleep_count, sync_count;
    uint32_t active_now = (uint32_t)(sleep_count + sync_count);

    lua_newtable(L);
    lua_pushstring(L, "schedule_failures"); lua_pushinteger(L, sched_fail); lua_settable(L, -3);
    lua_pushstring(L, "schedule_successes"); lua_pushinteger(L, sched_succ); lua_settable(L, -3);
    lua_pushstring(L, "max_active_threads"); lua_pushinteger(L, max_active); lua_settable(L, -3);
    lua_pushstring(L, "pool_capacity"); lua_pushinteger(L, pool_cap); lua_settable(L, -3);
    lua_pushstring(L, "active_now"); lua_pushinteger(L, active_now); lua_settable(L, -3);
    lua_pushstring(L, "lockfree_posted"); lua_pushinteger(L, posted); lua_settable(L, -3);
    lua_pushstring(L, "lockfree_processed"); lua_pushinteger(L, processed); lua_settable(L, -3);
    lua_pushstring(L, "lockfree_dropped"); lua_pushinteger(L, dropped); lua_settable(L, -3);
    lua_pushstring(L, "lockfree_depth"); lua_pushinteger(L, depth); lua_settable(L, -3);

    if (reset) {
        clock_reset_stats();
    }
    return 1;
}

// ============================================================================
// C INTERFACE FUNCTIONS (Called from crow C libraries)
// ============================================================================

// Implementation of C interface function (after BlackbirdCrow class is fully defined)
// CRITICAL: Place in RAM - called from shaper_v() via slopes.c
extern "C" {
__attribute__((section(".time_critical.hardware_output_set_voltage")))
void hardware_output_set_voltage(int channel, float voltage) {
    int ch_idx = channel - 1;  // channel is 1-based
    
    // If noise is currently active and locked, ignore this call completely
    // The lock counter protects noise from automatic hardware_output_set_voltage calls
    // (from slopes, LFOs, etc.) but explicit user actions via casl_action will clear noise
    if (ch_idx >= 0 && ch_idx < 4) {
        if (g_noise_active[ch_idx] && g_noise_lock_counter[ch_idx] > 0) {
            // Noise is active and protected - ignore this automatic call
            // Do NOT decrement counter - keep noise protected indefinitely
            return;
        }
        
        // If noise is active but not locked, a new action is trying to output
        // Clear noise and let the new action take control
        if (g_noise_active[ch_idx]) {
            g_noise_active[ch_idx] = false;
            g_noise_active_mask &= ~(1 << ch_idx);  // Clear bit in mask
            g_noise_gain[ch_idx] = 0;
            g_noise_lock_counter[ch_idx] = 0;
        }
    }
    
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->hardware_set_output(channel, voltage);
    }
}

// Q16-native version - eliminates float conversion in hot path
// Called from slopes.c shaper_v() and S_toward_q16()
__attribute__((section(".time_critical.hardware_output_set_voltage_q16")))
void hardware_output_set_voltage_q16(int channel, q16_t voltage_q16) {
    int ch_idx = channel - 1;  // channel is 1-based
    
    // Same noise handling as float version
    if (ch_idx >= 0 && ch_idx < 4) {
        if (g_noise_active[ch_idx] && g_noise_lock_counter[ch_idx] > 0) {
            return;
        }
        
        if (g_noise_active[ch_idx]) {
            g_noise_active[ch_idx] = false;
            g_noise_active_mask &= ~(1 << ch_idx);
            g_noise_gain[ch_idx] = 0;
            g_noise_lock_counter[ch_idx] = 0;
        }
    }
    
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->hardware_set_output_q16(channel, voltage_q16);
    }
}
}

// C interface functions for pulse outputs (called from Lua)
extern "C" void hardware_pulse_output_set(int channel, bool state) {
    if (g_blackbird_instance) {
        ((BlackbirdCrow*)g_blackbird_instance)->hardware_set_pulse(channel, state);
    }
}

// NOTE: pulseout_has_custom_action() and pulseout_execute_action() are no longer needed
// Pulse outputs are now controlled entirely via Lua :clock() coroutines

// Bridge function called from slopes.c to trigger Lua soutput_handler
extern "C" void trigger_soutput_handler(int channel, float voltage) {
    LuaManager* lua_mgr = LuaManager::getInstance();
    if (lua_mgr && lua_mgr->L) {
        // Call the C binding for soutput_handler which will call the Lua function
        lua_pushcfunction(lua_mgr->L, LuaManager::lua_soutput_handler);
        lua_pushinteger(lua_mgr->L, channel + 1); // Convert to 1-based
        lua_pushnumber(lua_mgr->L, voltage);
        if (lua_pcall(lua_mgr->L, 2, 0, 0) != LUA_OK) {
            const char* error = lua_tostring(lua_mgr->L, -1);
            printf("soutput_handler error: %s\n\r", error ? error : "unknown error");
            lua_pop(lua_mgr->L, 1);
        }
    }
}

// Provide Lua state access for l_crowlib metro handler
extern "C" lua_State* get_lua_state(void) {
    LuaManager* lua_mgr = LuaManager::getInstance();
    return lua_mgr ? lua_mgr->L : nullptr;
}

// ============================================================================
// MAIN ENTRY POINTS & STDIO REDIRECTION
// ============================================================================

BlackbirdCrow crow;

// Simple MIDI-style LED animation while waiting for USB host connection
static void usb_wait_led_step() {
    static const uint8_t led_path[] = {0, 1, 3, 5, 4, 2};
    static size_t path_index = 0;
    static int last_led = -1;
    static uint64_t last_step_us = 0;

    uint64_t now = time_us_64();
    if (now - last_step_us < 50000) {
        return;  // ~20 Hz animation rate
    }
    last_step_us = now;

    if (last_led >= 0) {
        crow.IndicatorLedOff(last_led);
    }

    int current_led = led_path[path_index];
    crow.IndicatorLedOn(current_led);
    last_led = current_led;

    path_index = (path_index + 1) % (sizeof(led_path) / sizeof(led_path[0]));
}

static void usb_wait_led_clear() {
    for (int i = 0; i < 6; i++) {
        crow.IndicatorLedOff(i);
    }
}

// Expose card unique ID for USB descriptors
// This ensures the USB serial number follows the card, not the board
extern "C" uint64_t get_card_unique_id(void) {
    // Access through the blackbird instance if available
    if (g_blackbird_instance) {
        return ((BlackbirdCrow*)g_blackbird_instance)->cached_unique_id;
    }
    // Fallback to 0 during early initialization
    return 0;
}

void core1_entry() {
        printf("[boot] core1 audio engine starting\n\r");
        crow.EnableNormalisationProbe();
        crow.Run(); 
}

// Redirect stdio functions to TinyUSB CDC
extern "C" {
    // Override putchar for printf support
    int putchar(int c) {
        if (tud_cdc_connected()) {
            uint8_t ch = (uint8_t)c;
            tud_cdc_write(&ch, 1);
            if (c == '\n' || c == '\r') {
                
            }
        }
        return c;
    }
    
    // Override puts for string output
    int puts(const char* s) {
        if (tud_cdc_connected()) {
            tud_cdc_write_str(s);
            tud_cdc_write_char('\n');
            
        }
        return 1;
    }
    
    // Newlib write function
    int _write(int handle, char *data, int size) {
        if (handle == 1 || handle == 2) { // stdout or stderr
            if (tud_cdc_connected()) {
                tud_cdc_write(data, size);
                
            }
            return size;
        }
        return -1;
    }

    // Override _exit to ensure message flushing on panic/abort
    void __attribute__((noreturn)) _exit(int status) {
        (void)status;

        // If we can, flash all LEDs so the user sees the fault state.
        // (Main loop is no longer running, so we blink from here.)
        BlackbirdCrow* card = g_blackbird_instance ? (BlackbirdCrow*)g_blackbird_instance : nullptr;
        bool on = true;
        uint32_t last_toggle_us = time_us_32();
        if (card) {
            for (uint32_t i = 0; i < 6; i++) {
                card->IndicatorLedOn(i, on);
            }
        }

        tud_cdc_write_str("\n\r[BLACKBIRD CRASH] RIP. Please reset.\n\r");
        tud_cdc_write_flush();
        
        // Force flush loop - keep servicing USB tasks to push data out
        uint32_t timeout = 0;
        while (tud_cdc_write_available() < 256 && timeout < 100000) {
            tud_task();
            sleep_us(100);
            timeout++;
        }
        
        while (1) {
            // Hang, but keep blinking LEDs at 0.5 Hz.
            uint32_t now_us = time_us_32();
            if ((now_us - last_toggle_us) >= kResetIndicatorBlinkHalfPeriodUs) {
                last_toggle_us = now_us;
                on = !on;
                if (card) {
                    for (uint32_t i = 0; i < 6; i++) {
                        card->IndicatorLedOn(i, on);
                    }
                }
            }
            sleep_ms(10);
        }
    }
}

int main()
{
    set_sys_clock_khz(200000, true);

    // Initialize TinyUSB (replaces stdio_init_all)
    tusb_init();
    
    // Disable stdio buffering for immediate console output
    setvbuf(stdout, NULL, _IONBF, 0);
    
    // Wait briefly (up to 1500ms) for a USB serial host to connect so the boot banner is visible
    {
        absolute_time_t until = make_timeout_time_ms(1500);
        while (!tud_cdc_connected() && absolute_time_diff_us(get_absolute_time(), until) > 0) {
            tud_task();  // Must service TinyUSB while waiting
            usb_wait_led_step();
            tight_loop_contents();
        }
    }

    usb_wait_led_clear();

    multicore_launch_core1(core1_entry);
    
    // Allow core1 to start before entering control loop
    sleep_ms(500);
    
    // Start Core0 main control loop (handles commands and events)
    crow.MainControlLoop();
}
