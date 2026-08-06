#define COMPUTERCARD_SAMPLE_RATE_DIV 2 // Run at 24kHz sample rate to avoid CPU overruns
#define SAMPLE_RATE (48000 / COMPUTERCARD_SAMPLE_RATE_DIV)
#define SAMPLES_PER_MINUTE (SAMPLE_RATE * 60)

#include "ComputerCard.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "hardware/structs/sio.h"
#include "tusb.h"
#include "usb_midi_host.h"
#include "hardware/divider.h"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>

// ============================================================================
// Data Structs & Constants
// ============================================================================

enum WaveShape {
    WAVE_GATE = 0,
    WAVE_RATCHET,
    WAVE_SINE,
    WAVE_TRIANGLE,
    WAVE_SAW_UP,
    WAVE_SAW_DOWN,
    WAVE_TRAPEZOID,
    WAVE_HUMP,
    WAVE_ENVELOPE,
    WAVE_LOG_ENVELOPE,
    WAVE_RANDOM_SH,
    WAVE_RANDOM_SM,
    WAVE_CV_DELAY,
    WAVE_MATH,
    WAVE_NUM_SHAPES
};

enum Scale {
    SCALE_OFF = 0,
    SCALE_CHROMATIC,
    SCALE_MAJOR,
    SCALE_MINOR_PENTATONIC,
    SCALE_NATURAL_MINOR,
    SCALE_MAJOR_PENTATONIC,
    SCALE_DORIAN,
    SCALE_MIXOLYDIAN,
    SCALE_LYDIAN,
    SCALE_PHRYGIAN,
    SCALE_HARMONIC_MINOR,
    SCALE_NUM_SCALES
};

// 34 clock modifiers: divisions (/2048 to /2), 1x, multiplications (x2 to x128)
// Pinned to RAM so Core 1 never stalls on a Flash rodata access.
uint32_t clock_multipliers_q16[] __attribute__((section(".data"))) = {
    32, 64, 85, 128, 171, 256, 341,                                        // /2048, /1024, /768, /512, /384, /256, /192
    512, 683, 1024, 1365, 2048, 2731, 4096, 5461, 8192, 10922, 16384, 21845, 32768,  // Divisions: /128, /96, /64, /48, /32, /24, /16, /12, /8, /6, /4, /3, /2
    65536,                                                                  // x1
    131072, 196608, 262144, 393216, 524288, 786432, 1048576, 1572864, 2097152, 3145728, 4194304, 6291456, 8388608 // Multiplications: x2 to x128
};

const char* clock_modifier_names[] = {
    "/2048", "/1024", "/768", "/512", "/384", "/256", "/192",
    "/128", "/96", "/64", "/48", "/32", "/24", "/16", "/12", "/8", "/6", "/4", "/3", "/2",
    "x1",
    "x2", "x3", "x4", "x6", "x8", "x12", "x16", "x24", "x32", "x48", "x64", "x96", "x128"
};

const char* wave_shape_names[] = {
    "GATE", "RTCH", "SINE", "TRI", "SAW+", "SAW-", "TRAP", "HUMP", "ENV", "LOGE", "RSH", "RSM", "DELAY"
};

const char* scale_names[] = {
    "OFF", "CHRO", "MAJ", "MPEN", "MIN", "MINP", "DOR", "MIXO", "LYD", "PHRY", "HMIN"
};

// Pinned to RAM so Core 1 never stalls on Flash rodata access inside ProcessSample.
int scale_notes_major[] __attribute__((section(".data"))) = {0, 2, 4, 5, 7, 9, 11};
int scale_notes_minor_pentatonic[] __attribute__((section(".data"))) = {0, 3, 5, 7, 10};
int scale_notes_natural_minor[] __attribute__((section(".data"))) = {0, 2, 3, 5, 7, 8, 10};
int scale_notes_major_pentatonic[] __attribute__((section(".data"))) = {0, 2, 4, 7, 9};
int scale_notes_dorian[] __attribute__((section(".data"))) = {0, 2, 3, 5, 7, 9, 10};
int scale_notes_mixolydian[] __attribute__((section(".data"))) = {0, 2, 4, 5, 7, 9, 10};
int scale_notes_lydian[] __attribute__((section(".data"))) = {0, 2, 4, 6, 7, 9, 11};
int scale_notes_phrygian[] __attribute__((section(".data"))) = {0, 1, 3, 5, 7, 8, 10};
int scale_notes_harmonic_minor[] __attribute__((section(".data"))) = {0, 2, 3, 5, 7, 8, 11};

uint32_t segment_widths_ram[] = {
    4294967295U,
    2147483647U,
    1431655765U,
    1073741823U,
    858993459U,
    715827882U,
    613566756U,
    536870911U
};

struct ChannelConfig {
    uint8_t clock_modifier = 20;     // x1 default (index 20 in new 34-modifier scale)
    uint8_t euclidean_steps = 0;    // 0 = straight clock
    uint8_t euclidean_fills = 0;
    uint8_t wave_shape = WAVE_GATE;
    uint8_t wave_param = 128;       // default 50% / mid-way
    uint8_t probability = 100;      // 100% trigger probability
    uint8_t quantizer_scale_and_offset = 0; // bits 0-3: quantizer_scale, bits 4-7: euclidean_offset
    uint8_t loop_length = 0;        // 0 = Infinite
    uint8_t level = 0;            // default level (0 for analog, overridden for digital)
    uint8_t glide = 0;              // default 0 (instant change, no portamento)

    uint8_t get_quantizer_scale() const {
        return quantizer_scale_and_offset & 0x0F;
    }
    void set_quantizer_scale(uint8_t scale) {
        quantizer_scale_and_offset = (quantizer_scale_and_offset & 0xF0) | (scale & 0x0F);
    }
    uint8_t get_euclidean_offset() const {
        return (quantizer_scale_and_offset >> 4) & 0x0F;
    }
    void set_euclidean_offset(uint8_t offset) {
        quantizer_scale_and_offset = (quantizer_scale_and_offset & 0x0F) | ((offset & 0x0F) << 4);
    }
    uint8_t get_clock_modifier() const {
        if (clock_modifier <= 33) return clock_modifier;
        return 20; // x1 default (index 20)
    }
    void set_clock_modifier(uint8_t mod) {
        if (mod <= 33) clock_modifier = mod;
    }
    uint8_t get_trigger_source() const {
        if (clock_modifier >= 34 && clock_modifier <= 39) {
            return clock_modifier - 33;
        }
        if (clock_modifier >= 40 && clock_modifier <= 55) {
            return 7; // MIDI Mode
        }
        return 0; // standard clock source
    }
    void set_trigger_source(uint8_t src) {
        if (src >= 1 && src <= 6) {
            clock_modifier = 33 + src;
        } else if (src == 7) {
            if (clock_modifier < 40 || clock_modifier > 55) {
                clock_modifier = 40; // MIDI Ch 1 default
            }
        } else {
            clock_modifier = 20; // x1 default
        }
    }
};

struct SavedSettings {
    uint32_t magic = 0x434c4b5b;    // 'CLKZ'
    uint16_t bpm = 120;
    uint8_t sync_source = 0;        // 0 = Internal, 1 = External Pulse 1
    uint8_t reset_mode = 0;         // 0 = Reset on Pulse 2, 1 = Run Gate on Pulse 2
    uint8_t ext_ppqn = 4;           // External Clock PPQN (1, 4, 24, 48)
    uint8_t global_swing = 0;       // 0 to 50 (%)
    uint8_t global_humanize = 0;    // 0 to 100 (%)
    uint8_t global_seed = 0;        // 0 to 63 (64 seeding options)
    ChannelConfig channels[6];
};

// Internal pre-calculated data owned exclusively by Core 1
struct ChannelState {
    uint32_t phase = 0;
    uint32_t phase_increment = 0;
    uint8_t step_index = 0;
    uint8_t loop_step_count = 0;
    bool step_active = true;
    // volatile: written by Core 1, read by Core 0 (LED + telemetry). 16-bit
    // aligned stores are atomic on Cortex-M0+, but volatile prevents the
    // compiler from caching the value across the Core 0 read boundary.
    volatile int16_t last_value = -2048;
    volatile int16_t telemetry_max = -2048;
    volatile int16_t telemetry_min = 2047;

    // Slew/Triangle pre-calculations
    uint64_t inv_skew = 0;
    uint64_t inv_rem = 0;
    uint64_t inv_slew = 0;
    uint8_t ratchet_count = 2;

    // Random walk and S&H state
    int16_t rand_current = 0;
    int16_t rand_next = 0;
    int32_t rand_smoothed = 0;
    int16_t random_history[64] = {0};

    // External sync timing pre-calculations (division-free on Core 1)
    uint32_t ext_sync_pulses = 4;
    uint32_t ext_pulse_ch_counter = 0;

    // Quantizer input cache — skip re-quantizing if note and scale are unchanged
    int16_t last_q_input_note = INT16_MIN; // raw semitone before snap
    uint8_t last_q_scale = 0xFF;           // scale when last_q_input_note was computed
    int16_t last_q_output_note = 60;       // snapped semitone result
    int32_t jitter_phase = 0;              // Global Humanize phase offset for current step
    uint32_t delay_write_ptr = 0;          // Circular buffer write index for CV/Audio delay mode
    bool step_triggered = false;           // Flag set when a new clock step starts
    uint32_t bounce_decay = 0;             // Bouncing ball envelope decay value
    uint32_t bounce_decay_coeff = 65536;   // Q16 fixed-point decay coefficient (65536 = neutral/full-speed)
    uint8_t delay_impulse_counter = 0;     // Remaining samples to inject impulse trigger
    // S&H humanize alignment: new voltage is held in pending until jitter delay expires
    // so the CV latch and the trigger/gate output change on the exact same sample.
    bool     rand_pending     = false;
    int16_t  rand_pending_val = 0;
    bool     last_gate_high   = false;

    volatile uint8_t midi_note_num = 60;
    volatile uint8_t midi_velocity = 0;
    volatile bool midi_note_active = false;
    volatile bool midi_trigger_pending = false;
    float current_pitch = 60.0f;
};

// Symmetrical circular delay buffers in SRAM (24 KB total)
int16_t delay_buffer_0[4096] __attribute__((section(".data")));
int16_t delay_buffer_1[4096] __attribute__((section(".data")));
int16_t delay_buffer_2[2048] __attribute__((section(".data")));
int16_t delay_buffer_3[2048] __attribute__((section(".data")));

int16_t* delay_buffers[4] = { delay_buffer_0, delay_buffer_1, delay_buffer_2, delay_buffer_3 };
uint32_t delay_buffer_masks[4] = { 4095, 4095, 2047, 2047 };

// Knob pick-up ("wiggle unlock") latch
struct KnobLock {
    bool locked = true;
    int32_t ref = 0;

    void engage(int32_t current_physical_value) {
        locked = true;
        ref = current_physical_value;
    }

    bool update(int32_t current_physical_value, int32_t stored_software_value, int32_t& output_value) {
        if (locked) {
            int32_t diff = current_physical_value - ref;
            if (diff < 0) diff = -diff;
            if (diff > 200) { // ~5% wiggle threshold to unlock manually
                locked = false;
            }
        }
        if (!locked) {
            output_value = current_physical_value;
            return true;
        } else {
            output_value = stored_software_value;
            return false;
        }
    }
};

struct NoteEntry {
    uint8_t note = 0;
    uint8_t velocity = 0;
};

struct NoteStack {
    NoteEntry notes[8];
    uint8_t count = 0;

    void push(uint8_t note, uint8_t velocity) {
        remove(note);
        if (count < 8) {
            notes[count++] = {note, velocity};
        } else {
            for (int i = 1; i < 8; i++) {
                notes[i - 1] = notes[i];
            }
            notes[7] = {note, velocity};
        }
    }

    void remove(uint8_t note) {
        for (int i = 0; i < count; i++) {
            if (notes[i].note == note) {
                for (int j = i + 1; j < count; j++) {
                    notes[j - 1] = notes[j];
                }
                count--;
                break;
            }
        }
    }

    bool empty() const {
        return count == 0;
    }

    NoteEntry top() const {
        if (count > 0) {
            return notes[count - 1];
        }
        return {0, 0};
    }

    void clear() {
        count = 0;
    }
};

// LED feedback display modes for parameter preview
enum FeedbackMode : uint8_t {
    FEEDBACK_BAR_UNIPOLAR = 0, // 6 LEDs left-to-right fill with fractional brightness (val 0-63)
    FEEDBACK_BAR_BIPOLAR  = 1, // LEDs 0-2 = negative side, 3-5 = positive side (signed_val -32..+32)
    FEEDBACK_BINARY       = 2, // 6-bit binary number on LEDs 0-5
    FEEDBACK_BAR_UNIPOLAR_BLINK = 3, // 6 LEDs left-to-right, blinking at 2.5Hz (val 0-63)
};

// Pending deferred-commit types for stepped parameters
enum PendingType : uint8_t {
    PENDING_NONE       = 0,
    PENDING_MODIFIER   = 1, // clock modifier - committed via division_feedback_timer_
    PENDING_SHAPE      = 2,
    PENDING_LOOP       = 3,
    PENDING_STEPS      = 4,
    PENDING_FILLS      = 5,
    PENDING_OFFSET     = 6,
    PENDING_SCALE      = 7,
    PENDING_SEED       = 8,
    PENDING_SYNC_RESET = 9,
};

struct PendingCommit {
    bool    active  = false;
    uint8_t channel = 0;            // 0-5 channel index, 6 = global
    uint8_t type    = PENDING_NONE;
    uint8_t value   = 0;
};

// USB MIDI & CDC State
extern volatile bool g_usb_midi_host;

// ============================================================================
// Flash / Persistence Configuration
// ============================================================================

#define FLASH_TARGET_OFFSET (2 * 1024 * 1024 - 4096) // Last sector of 2MB flash
static const uint8_t* kFlashPtr = reinterpret_cast<const uint8_t*>(XIP_BASE + FLASH_TARGET_OFFSET);
static uint8_t flash_sector_buf[4096] __attribute__((aligned(4)));

// Flash layout within the 4KB sector:
//   Offset 0:   SavedSettings active_settings  (~65 bytes)
//   Offset 256: SavedSettings preset_slots[6]  (6 x ~65 bytes = ~390 bytes)
//   Total: ~650 bytes — well within 4KB
#define FLASH_PRESET_BASE_OFFSET 256  // Byte offset of first preset slot in sector



// ============================================================================
// Core Parameter Double-Buffer Struct (Core 0 → Core 1 Param Sharing)
// ============================================================================

struct SharedParams {
    uint16_t bpm;
    uint8_t sync_source;
    uint8_t reset_mode;
    uint8_t ext_ppqn;
    uint8_t global_swing;
    uint8_t global_humanize;
    uint8_t global_seed;
    bool global_paused;
    uint32_t manual_reset_counter;
    uint32_t base_increment; // Pre-calculated x1 step increment per sample
    uint32_t swing_mult_even; // Pre-calculated swing Q16 multiplier for even steps
    uint32_t swing_mult_odd;  // Pre-calculated swing Q16 multiplier for odd steps
    ChannelConfig channels[6];
    // NOTE: ChannelState is intentionally NOT included here.
    // Core 1 owns its own states_ array exclusively, so Core 0 can never
    // corrupt the live phase/step/rand values by atomically swapping the
    // parameter buffer mid-step.
};

volatile SharedParams g_shared_params[2];
volatile uint32_t g_shared_params_idx = 0;

// ============================================================================
// Global Lookup Tables
// ============================================================================

int16_t sine_lut[256];
uint16_t exp_lut[256];
uint32_t inv_skew_lut[256];
uint32_t inv_rem_lut[256];
uint32_t inv_slew_lut[256];
uint32_t env_multiplier_lut[256];

void init_luts() {
    for (int i = 0; i < 256; i++) {
        // Sine wave ranging from -2048 to 2047
        sine_lut[i] = (int16_t)(sinf(i * 2.0f * 3.14159265f / 256.0f) * 2047.0f);
        // Exponential decay envelope mapping 4095 (i=0) to 0 (i=255)
        float val = expf(-4.0f * i / 255.0f);
        exp_lut[i] = (uint16_t)(4095.0f * (val - 0.0183156f) / 0.9816843f);

        // Precalculate division-free inverses for triangle symmetry and random slew
        uint32_t clamped = i;
        if (clamped < 2) clamped = 2;
        if (clamped > 253) clamped = 253;
        
        uint32_t skew_point = clamped << 24;
        uint32_t rem_width = 0xFFFFFFFF - skew_point;
        
        inv_skew_lut[i] = ((uint64_t)4095 << 32) / skew_point;
        inv_rem_lut[i] = ((uint64_t)4095 << 32) / rem_width;

        clamped = i;
        if (clamped < 2) clamped = 2;
        uint32_t slew_duration = clamped << 24;
        inv_slew_lut[i] = ((uint64_t)65536 << 32) / slew_duration;

        // Precalculate envelope speed scaling factors
        clamped = i;
        if (clamped < 2) clamped = 2;
        // Width 100% (255) -> 1.0x speed multiplier (256 in Q8)
        // Width 50% (128) -> 2.0x speed multiplier (512 in Q8)
        env_multiplier_lut[i] = (255 * 256) / clamped;
    }
}

// Fast division-free LUT interpolation
inline int16_t __not_in_flash_func(interpolate_sine)(uint32_t phase) {
    uint32_t idx = phase >> 24;
    uint32_t frac = (phase >> 8) & 0xFFFF;
    int16_t y0 = sine_lut[idx];
    int16_t y1 = sine_lut[(idx + 1) & 0xFF];
    return y0 + (((y1 - y0) * (int32_t)frac) >> 16);
}

inline int16_t __not_in_flash_func(interpolate_exp)(uint32_t phase) {
    uint32_t idx = phase >> 24;
    uint32_t frac = (phase >> 8) & 0xFFFF;
    uint16_t y0 = exp_lut[idx];
    uint16_t y1 = (idx < 255) ? exp_lut[idx + 1] : 0;
    return (int16_t)(y0 + (((int32_t)(y1 - y0) * (int32_t)frac) >> 16));
}

// Fast LCG random number generator running from RAM
static uint32_t rand_seed = 0x12345678;
inline uint32_t __not_in_flash_func(fast_rand)() {
    rand_seed = rand_seed * 1103515245 + 12345;
    return rand_seed;
}

inline int __not_in_flash_func(safe_abs)(int x) {
    return x < 0 ? -x : x;
}

inline uint32_t __not_in_flash_func(safe_div_u32)(uint32_t dividend, uint32_t divisor) {
    // RP2040 SIO divider takes exactly 8 cycles to produce a result after
    // the divisor is written. hw_divider_u32_quotient_wait() burns those
    // cycles via compiler barriers, then reads the settled quotient.
    sio_hw->div_udividend = dividend;
    sio_hw->div_udivisor  = divisor;
    return hw_divider_u32_quotient_wait();
}

inline uint32_t __not_in_flash_func(mul16_16)(uint32_t a, uint32_t b) {
    uint32_t res;
    asm ("mul %0, %1" : "=l" (res) : "l" (b), "0" (a) : "cc");
    return res;
}

inline uint32_t __not_in_flash_func(mul_u32_u32_high)(uint32_t a, uint32_t b) {
    uint32_t a_lo = a & 0xFFFF;
    uint32_t a_hi = a >> 16;
    uint32_t b_lo = b & 0xFFFF;
    uint32_t b_hi = b >> 16;
    
    uint32_t p0 = mul16_16(a_lo, b_lo);
    uint32_t p1 = mul16_16(a_hi, b_lo);
    uint32_t p2 = mul16_16(a_lo, b_hi);
    uint32_t p3 = mul16_16(a_hi, b_hi);
    
    uint32_t carry = (p0 >> 16) + (p1 & 0xFFFF) + (p2 & 0xFFFF);
    return p3 + (p1 >> 16) + (p2 >> 16) + (carry >> 16);
}

inline uint32_t __not_in_flash_func(mul_u32_u32_shift16)(uint32_t a, uint32_t b) {
    uint32_t a_lo = a & 0xFFFF;
    uint32_t a_hi = a >> 16;
    uint32_t b_lo = b & 0xFFFF;
    uint32_t b_hi = b >> 16;
    
    uint32_t p0 = mul16_16(a_lo, b_lo);
    uint32_t p1 = mul16_16(a_hi, b_lo);
    uint32_t p2 = mul16_16(a_lo, b_hi);
    uint32_t p3 = mul16_16(a_hi, b_hi);
    
    return (p3 << 16) + p1 + p2 + (p0 >> 16);
}

inline uint32_t __not_in_flash_func(mul_u32_u32_shift8)(uint32_t a, uint32_t b) {
    uint32_t a_lo = a & 0xFFFF;
    uint32_t a_hi = a >> 16;
    uint32_t b_lo = b & 0xFFFF;
    uint32_t b_hi = b >> 16;
    
    uint32_t p0 = mul16_16(a_lo, b_lo);
    uint32_t p1 = mul16_16(a_hi, b_lo);
    uint32_t p2 = mul16_16(a_lo, b_hi);
    uint32_t p3 = mul16_16(a_hi, b_hi);
    
    return (p3 << 24) + ((p1 + p2) << 8) + (p0 >> 8);
}

inline uint32_t __not_in_flash_func(mul_u32_u16_shift8_clamp)(uint32_t a, uint32_t b) {
    uint32_t a_lo = a & 0xFFFF;
    uint32_t a_hi = a >> 16;
    
    uint32_t p0 = mul16_16(a_lo, b);
    uint32_t p1 = mul16_16(a_hi, b);
    
    if (p1 >= 0x01000000) {
        return 0xFFFFFFFF;
    }
    uint32_t part1 = p1 << 8;
    uint32_t part0 = p0 >> 8;
    if (part1 > 0xFFFFFFFF - part0) {
        return 0xFFFFFFFF;
    }
    return part1 + part0;
}

// Scale Quantization snap function
int __not_in_flash_func(quantize_to_scale)(int note, int scale) {
    if (scale == SCALE_OFF || scale == SCALE_CHROMATIC) return note;

    // Guaranteed positive floor division-free calculation for note >= -240
    int positive_note = note + 240;
    int octave = ((positive_note * 1366) >> 14) - 20;
    int note_in_octave = positive_note - (octave + 20) * 12;

    const int* scale_notes = nullptr;
    int scale_size = 0;

    if (scale == SCALE_MAJOR) {
        scale_notes = scale_notes_major;
        scale_size = 7;
    } else if (scale == SCALE_MINOR_PENTATONIC) {
        scale_notes = scale_notes_minor_pentatonic;
        scale_size = 5;
    } else if (scale == SCALE_NATURAL_MINOR) {
        scale_notes = scale_notes_natural_minor;
        scale_size = 7;
    } else if (scale == SCALE_MAJOR_PENTATONIC) {
        scale_notes = scale_notes_major_pentatonic;
        scale_size = 5;
    } else if (scale == SCALE_DORIAN) {
        scale_notes = scale_notes_dorian;
        scale_size = 7;
    } else if (scale == SCALE_MIXOLYDIAN) {
        scale_notes = scale_notes_mixolydian;
        scale_size = 7;
    } else if (scale == SCALE_LYDIAN) {
        scale_notes = scale_notes_lydian;
        scale_size = 7;
    } else if (scale == SCALE_PHRYGIAN) {
        scale_notes = scale_notes_phrygian;
        scale_size = 7;
    } else if (scale == SCALE_HARMONIC_MINOR) {
        scale_notes = scale_notes_harmonic_minor;
        scale_size = 7;
    }

    if (scale_notes == nullptr || scale_size <= 0) {
        return note;
    }

    int best_note = scale_notes[0];
    int min_diff = 999;
    for (int i = 0; i < scale_size; ++i) {
        int diff = safe_abs(note_in_octave - scale_notes[i]);
        if (diff < min_diff) {
            min_diff = diff;
            best_note = scale_notes[i];
        }
    }

    return octave * 12 + best_note;
}

// ============================================================================
// ClockworksCard Definition
// ============================================================================

class ClockworksCard : public ComputerCard {
public:
    ClockworksCard();
    void ProcessSample() override;
    void run_core0_ui_loop();
    void tick_ui_once();

    void load_settings();
    void save_settings_to_flash();
    void sync_params_to_core1();
    void process_incoming_midi_packet(uint8_t const packet[4]);
    void process_incoming_sysex(uint8_t const *buf, int len);
    void apply_parameter_change(uint8_t ch, uint8_t param_id, uint8_t value);
    void send_telemetry_packet_sysex();
    void send_sync_packet_sysex();

private:
    void reset_channels_core1(uint8_t global_seed);
    void commit_pending(PendingCommit& pc);
    void clear_midi_stacks_core0();
    void migrate_settings_format();
    void process_usb_midi_device();
    void process_midi_cc(uint8_t midi_ch, uint8_t cc, uint8_t val);
    void render_leds_feedback(uint8_t mode, uint8_t val, int8_t signed_val, uint32_t now_ms);
    SavedSettings settings_;
    ChannelState states_[6];
    NoteStack note_stacks_[6];
    uint8_t last_midi_channel_[6];

    // UI State variables
    uint8_t active_page_ = 0;       // 0-5 = Channels, 6 = Global
    bool is_down_held_ = false;
    bool manual_reset_triggered_ = false;
    uint32_t manual_reset_counter_ = 0;
    uint32_t edit_feedback_timer_ = 0; // ms remaining of LED display

    // Page-change blink
    uint32_t page_blink_timer_ = 0;   // ms remaining of double-blink animation

    // Play/pause state (toggled by Switch UP on global page)
    bool paused_ = false;

    // Preset menu state (entered by hold-DOWN on global page)
    bool preset_menu_active_ = false;
    uint8_t selected_preset_ = 0;
    uint8_t armed_preset_action_ = 0;   // 0 = None, 1 = Load, 2 = Save
    volatile uint32_t division_feedback_timer_ = 0; // ms remaining of division binary display on LEDs
    uint8_t division_feedback_value_ = 0;
    volatile uint32_t parameter_feedback_timer_ = 0; // ms remaining of generic binary display on LEDs
    uint8_t parameter_feedback_value_ = 0;
    uint8_t  parameter_feedback_mode_       = FEEDBACK_BINARY;
    int8_t   parameter_feedback_signed_val_ = 0; // used by FEEDBACK_BAR_BIPOLAR

    // Knob pick-up locks
    KnobLock lock_main_;
    KnobLock lock_x_;
    KnobLock lock_y_;

    // Per-knob deferred commit slots for stepped parameters
    PendingCommit pending_main_;
    PendingCommit pending_x_;
    PendingCommit pending_y_;

    volatile bool ui_led_override_ = false;

    // Autosave triggers
    bool config_dirty_ = false;
    uint32_t last_change_time_ms_ = 0;

    // Telemetry timing
    uint32_t last_telemetry_time_ms_ = 0;
    bool telemetry_enabled_ = false;
    uint32_t telemetry_watchdog_ms_ = 0;

    // External sync engine variables on Core 1
    volatile uint32_t ext_period_samples_ = 0;
    volatile uint32_t sync_sample_counter_ = 0;
    volatile uint32_t samples_since_last_pulse_ = 0;
    volatile bool external_clock_active_ = false;
    volatile uint32_t ext_pulse_counter_ = 0;

    // MIDI sync variables shared between Core 0 and Core 1
    volatile uint32_t midi_clock_tick_count_ = 0;
    volatile uint32_t midi_reset_counter_ = 0;
    volatile uint32_t last_midi_clock_time_ms_ = 0;
    volatile uint32_t midi_period_samples_ = 0;
    volatile uint32_t samples_since_last_midi_clock_ = 999999;
    volatile uint32_t midi_pulse_counter_ = 0;
    volatile uint32_t master_phase_ = 0;

    void calculate_state_inverses(int ch, uint8_t shape, uint8_t param);
    void process_incoming_serial();
    void lock_knobs();
    void send_sync_packet();
    void send_param_update_sysex(uint8_t ch, uint8_t param_id, uint8_t value);
    void send_global_update_sysex(uint8_t param_id, uint8_t value);
    void apply_default_settings();
    void load_preset(uint8_t slot);
    void save_preset(uint8_t slot);
    bool is_valid_settings(const SavedSettings& s);
};

void (*ComputerCard::audio_callback_ptr)(void*) = nullptr;
void *ComputerCard::audio_callback_inst = nullptr;

void __not_in_flash_func(audio_callback_ram_wrapper)(void* inst) {
    static_cast<ClockworksCard*>(inst)->ClockworksCard::ProcessSample();
}

ClockworksCard::ClockworksCard() {
    midi_clock_tick_count_ = 0;
    midi_reset_counter_ = 0;
    last_midi_clock_time_ms_ = 0;
    midi_period_samples_ = 0;
    samples_since_last_midi_clock_ = 999999;
    init_luts();
    load_settings();
    EnableNormalisationProbe(); // Enable the hardware normalization probe to detect plugged in jacks

    // Set RAM-resident non-virtual audio callback
    ComputerCard::audio_callback_inst = this;
    ComputerCard::audio_callback_ptr = audio_callback_ram_wrapper;

    for (int i = 0; i < 6; i++) {
        calculate_state_inverses(i, settings_.channels[i].wave_shape, settings_.channels[i].wave_param);
        states_[i].ext_sync_pulses = 4;
        states_[i].ext_pulse_ch_counter = 0;
        for (int j = 0; j < 64; j++) {
            if (i >= 4) {
                states_[i].random_history[j] = (int16_t)(rand() % 256);
            } else {
                states_[i].random_history[j] = (int16_t)((rand() % 4096) - 2048);
            }
        }
        states_[i].rand_current = states_[i].random_history[0];
        states_[i].rand_next = states_[i].random_history[1];
    }

    sync_params_to_core1();
}

void ClockworksCard::calculate_state_inverses(int ch, uint8_t shape, uint8_t param) {
    if (ch >= 4) {
        return; // No pre-calculations needed for digital pulse outputs
    }
    // Clamp parameters slightly to avoid division by 0
    uint32_t clamped_param = param;
    if (clamped_param < 2) clamped_param = 2;
    if (clamped_param > 253) clamped_param = 253;

    if (shape == WAVE_TRIANGLE || shape == WAVE_HUMP) {
        uint32_t skew_point = clamped_param << 24;
        uint32_t rem_width = 0xFFFFFFFF - skew_point;
        states_[ch].inv_skew = ((uint64_t)4095 << 32) / skew_point;
        states_[ch].inv_rem = ((uint64_t)4095 << 32) / rem_width;
    } else if (shape == WAVE_TRAPEZOID) {
        uint32_t F = (clamped_param * 0x40000000ULL) >> 8;
        uint32_t R = 0x80000000ULL - F;
        states_[ch].inv_skew = ((uint64_t)4095 << 32) / R;
    } else if (shape == WAVE_RANDOM_SH) {
        uint32_t slew_duration = clamped_param << 24;
        states_[ch].inv_slew = ((uint64_t)65536 << 32) / slew_duration;
    } else if (shape == WAVE_RATCHET) {
        // Map 0-255 parameter to ratchet subdivisions: 2, 3, 4, 6, 8, 12, 16
        uint8_t ratchet_table[] = {2, 3, 4, 6, 8, 12, 16};
        int idx = (param * 7) >> 8;
        if (idx < 0) idx = 0;
        if (idx > 6) idx = 6;
        states_[ch].ratchet_count = ratchet_table[idx];
    }
}

void ClockworksCard::sync_params_to_core1() {
    uint32_t next_idx = 1 - g_shared_params_idx;
    volatile SharedParams &p = g_shared_params[next_idx];

    p.bpm = settings_.bpm;
    p.sync_source = Connected(Input::Pulse1) ? 1 : settings_.sync_source;
    p.reset_mode = settings_.reset_mode;
    p.ext_ppqn = settings_.ext_ppqn;
    p.global_swing = settings_.global_swing;
    p.global_humanize = settings_.global_humanize;
    p.global_seed = settings_.global_seed;
    p.global_paused = paused_;
    p.manual_reset_counter = manual_reset_counter_;
    
    // Pre-calculate baseline x1 step increment: (bpm * 2^32) / (60 * SAMPLE_RATE)
    p.base_increment = ((uint64_t)settings_.bpm << 32) / SAMPLES_PER_MINUTE;

    // Pre-calculate global swing Q16 multipliers (runs on Core 0)
    uint32_t s_q8 = ((uint32_t)settings_.global_swing * 256) / 100;
    if (s_q8 > 128) s_q8 = 128; // Cap swing offset at 50% to prevent extreme/unstable clocks
    if (s_q8 > 0) {
        p.swing_mult_even = (256 * 65536) / (256 + s_q8);
        p.swing_mult_odd = (256 * 65536) / (256 - s_q8);
    } else {
        p.swing_mult_even = 65536;
        p.swing_mult_odd = 65536;
    }

    // Update external sync pulses counts per channel division-free on Core 0
    for (int i = 0; i < 6; i++) {
        uint8_t mod = settings_.channels[i].get_clock_modifier();
        if (mod >= 33) mod = 19;
        uint32_t pulses = ((uint32_t)settings_.ext_ppqn * 65536) / clock_multipliers_q16[mod];
        if (pulses == 0) pulses = 1;
        states_[i].ext_sync_pulses = pulses;
    }

    std::memcpy((void*)p.channels, settings_.channels, sizeof(settings_.channels));
    // Do NOT copy states_ here. Core 1 owns states_ exclusively.
    // Copying would silently teleport phase/step/rand values mid-audio-sample.

    // Flip index atomically
    g_shared_params_idx = next_idx;
}

void ClockworksCard::clear_midi_stacks_core0() {
    for (int i = 0; i < 6; i++) {
        note_stacks_[i].clear();
        states_[i].midi_note_num = 60;
        states_[i].midi_velocity = 0;
        states_[i].midi_note_active = false;
        states_[i].midi_trigger_pending = false;

        uint8_t current_mod = settings_.channels[i].clock_modifier;
        if (current_mod >= 40 && current_mod <= 55) {
            last_midi_channel_[i] = current_mod;
        } else {
            last_midi_channel_[i] = 40 + i;
        }
    }
}

void ClockworksCard::migrate_settings_format() {
    // Obsolete migration code made a no-op to prevent settings corruption
    return;
}

void ClockworksCard::apply_default_settings() {
    settings_ = SavedSettings{};
    settings_.magic = 0x434c4b5b; // 'CLKZ'
    settings_.bpm = 120;
    settings_.sync_source = 0;
    settings_.reset_mode = 0;
    settings_.ext_ppqn = 4;
    settings_.global_swing = 0;
    settings_.global_humanize = 0;
    settings_.global_seed = 0;
    for (int i = 0; i < 6; i++) {
        settings_.channels[i].clock_modifier = 20; // x1
        settings_.channels[i].euclidean_steps = 0;
        settings_.channels[i].euclidean_fills = 0;
        settings_.channels[i].wave_shape = WAVE_GATE;
        settings_.channels[i].wave_param = 128;
        settings_.channels[i].probability = 100;
        settings_.channels[i].set_quantizer_scale(SCALE_OFF);
        settings_.channels[i].set_euclidean_offset(0);
        settings_.channels[i].loop_length = 0;
        settings_.channels[i].level = (i < 4) ? 200 : 0; // Default level 200 (+100% level) for analog, 0 (0% delay) for digital
        settings_.channels[i].glide = 0;                  // default 0 (instant)
        calculate_state_inverses(i, WAVE_GATE, 128);
    }
    clear_midi_stacks_core0();
}

bool ClockworksCard::is_valid_settings(const SavedSettings& s) {
    if (s.magic != 0x434c4b5b) return false;
    if (s.bpm < 40 || s.bpm > 250) return false;
    if (s.sync_source > 1) return false;
    if (s.reset_mode > 1) return false;
    if (s.ext_ppqn != 1 && s.ext_ppqn != 4 && s.ext_ppqn != 24 && s.ext_ppqn != 48) return false;
    if (s.global_swing > 50) return false;
    if (s.global_humanize > 100) return false;
    if (s.global_seed > 63) return false;
    for (int i = 0; i < 6; i++) {
        if (s.channels[i].clock_modifier >= 56) return false;
        if (s.channels[i].get_trigger_source() > 7) return false;
        if (s.channels[i].euclidean_steps > 16) return false;
        if (s.channels[i].euclidean_steps > 0) {
            if (s.channels[i].euclidean_fills > 16) return false;
            if (s.channels[i].euclidean_fills > s.channels[i].euclidean_steps) return false;
        } else {
            if (i >= 4) {
                if (s.channels[i].euclidean_fills > 16) return false;
            }
        }
        if (s.channels[i].probability > 100) return false;
        if (i >= 4) {
            if (s.channels[i].wave_shape >= 6) return false;
        } else {
            if (s.channels[i].wave_shape >= WAVE_NUM_SHAPES) return false;
        }
        if (s.channels[i].get_quantizer_scale() >= SCALE_NUM_SCALES) return false;
        if (s.channels[i].euclidean_steps > 0 && s.channels[i].get_euclidean_offset() >= s.channels[i].euclidean_steps) return false;
        if (s.channels[i].loop_length > 64) return false;
        if (s.channels[i].level > 200) return false;
    }
    return true;
}

void ClockworksCard::load_settings() {
    std::memcpy(&settings_, kFlashPtr, sizeof(SavedSettings));
    migrate_settings_format();
    if (!is_valid_settings(settings_)) {
        apply_default_settings();
        config_dirty_ = true;
        last_change_time_ms_ = 0;
    } else {
        clear_midi_stacks_core0();
    }
}


void ClockworksCard::load_preset(uint8_t slot) {
    if (slot >= 6) return;
    size_t offset = FLASH_PRESET_BASE_OFFSET + slot * sizeof(SavedSettings);
    const SavedSettings* src = reinterpret_cast<const SavedSettings*>(kFlashPtr + offset);
    std::memcpy(&settings_, src, sizeof(SavedSettings));
    migrate_settings_format();
    if (!is_valid_settings(settings_)) {
        std::memcpy(&settings_, kFlashPtr, sizeof(SavedSettings));
        migrate_settings_format();
        return;
    }
    for (int i = 0; i < 6; i++) {
        calculate_state_inverses(i, settings_.channels[i].wave_shape, settings_.channels[i].wave_param);
    }
    clear_midi_stacks_core0();
    save_settings_to_flash(); // Persist the loaded preset as the active settings
    sync_params_to_core1();
    lock_knobs();
    manual_reset_counter_++; // Reset phases on load
    send_sync_packet();
}

void ClockworksCard::save_preset(uint8_t slot) {
    if (slot >= 6) return;
    // Read full sector, write our preset into the correct slot, re-flash the sector
    std::memcpy(flash_sector_buf, kFlashPtr, 4096);
    size_t offset = FLASH_PRESET_BASE_OFFSET + slot * sizeof(SavedSettings);
    std::memcpy(flash_sector_buf + offset, &settings_, sizeof(SavedSettings));
    // Also keep active settings current at offset 0
    std::memcpy(flash_sector_buf, &settings_, sizeof(SavedSettings));

    // Core 0 runs the audio engine entirely from RAM (copy_to_ram binary type).
    // Flash erase/program only touches the XIP address window which Core 0 never
    // accesses, so no multicore lockout is needed. Just disable Core 1 interrupts.
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, flash_sector_buf, 4096);
    restore_interrupts(ints);
}

void __not_in_flash_func(ClockworksCard::save_settings_to_flash)() {
    std::memcpy(flash_sector_buf, kFlashPtr, 4096);
    if (std::memcmp(&settings_, flash_sector_buf, sizeof(SavedSettings)) == 0) {
        return; // Unchanged — skip flash erase/write
    }
    // Only overwrite the active-settings slot (offset 0); preserve preset slots
    std::memcpy(flash_sector_buf, &settings_, sizeof(SavedSettings));

    // Core 0 runs the audio engine entirely from RAM (copy_to_ram binary type).
    // Flash erase/program only touches the XIP address window which Core 0 never
    // accesses, so no multicore lockout is needed. Just disable Core 1 interrupts.
    const uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_TARGET_OFFSET, flash_sector_buf, 4096);
    restore_interrupts(ints);
}

inline int16_t __attribute__((always_inline)) scale_and_quantize_val(int16_t raw_val, const ChannelConfig& ch_config, uint8_t current_scale) {
    int32_t val = raw_val;
    // 1. Level Attenuation
    int32_t signed_level = (int32_t)ch_config.level - 100;
    int32_t abs_scale = signed_level >= 0 ? signed_level : -signed_level;
    bool is_bipolar = (ch_config.wave_shape == WAVE_SINE || 
                       ch_config.wave_shape == WAVE_TRIANGLE || 
                       ch_config.wave_shape == WAVE_SAW_UP || 
                       ch_config.wave_shape == WAVE_SAW_DOWN || 
                       ch_config.wave_shape == WAVE_RANDOM_SH || 
                       ch_config.wave_shape == WAVE_RANDOM_SM || 
                       ch_config.wave_shape == WAVE_CV_DELAY || 
                       ch_config.wave_shape == WAVE_TRAPEZOID);
    if (is_bipolar) {
        if (signed_level < 0) {
            // Negative level (left side): bipolar mode
            int32_t temp_level = val * abs_scale;
            int32_t abs_level = temp_level < 0 ? -temp_level : temp_level;
            int32_t quotient_level = (abs_level * 5243) >> 19;
            val = (int16_t)(temp_level < 0 ? -quotient_level : quotient_level);
        } else {
            // Positive level (right side): unipolar scaling (0V to +6V) scaling up from 0V
            int32_t uni_val = (val + 2048) >> 1; // 0..2047
            val = (int16_t)((uni_val * abs_scale * 5243) >> 19);
        }
    } else {
        int32_t temp_level = val * abs_scale;
        int16_t scaled = (int16_t)((temp_level * 5243) >> 19);
        val = (signed_level >= 0) ? scaled : -scaled;
    }
    // 2. Scale Quantization
    if (current_scale != SCALE_OFF) {
        int note = 60 + ((val * 9 + 128) >> 8);
        int q_note = quantize_to_scale(note, current_scale);
        int32_t temp_note = (q_note - 60) * 256;
        int32_t abs_note = temp_note < 0 ? -temp_note : temp_note;
        int32_t quotient_note = (abs_note * 58255) >> 19;
        val = (int16_t)(temp_note < 0 ? -quotient_note : quotient_note);
        if (val > 2047) val = 2047;
        if (val < -2048) val = -2048;
    }
    return val;
}

void ClockworksCard::commit_pending(PendingCommit& pc) {
    if (!pc.active) return;
    pc.active = false;
    uint8_t ch  = pc.channel;
    uint8_t val = pc.value;
    switch (pc.type) {
        case PENDING_MODIFIER:
            if (ch < 6) {
                settings_.channels[ch].clock_modifier = val;
            }
            break;
        case PENDING_SHAPE:
            if (ch < 6) {
                settings_.channels[ch].wave_shape = val;
                calculate_state_inverses(ch, val, settings_.channels[ch].wave_param);
            }
            break;
        case PENDING_LOOP:
            if (ch < 6) settings_.channels[ch].loop_length = val;
            break;
        case PENDING_STEPS:
            if (ch < 6) {
                settings_.channels[ch].euclidean_steps = val;
                if (val > 0 && settings_.channels[ch].euclidean_fills > val)
                    settings_.channels[ch].euclidean_fills = val;
                if (val > 0 && settings_.channels[ch].get_euclidean_offset() >= val)
                    settings_.channels[ch].set_euclidean_offset(val - 1);
                if (val == 0) settings_.channels[ch].set_euclidean_offset(0);
            }
            break;
        case PENDING_FILLS:
            if (ch < 6) {
                uint8_t max_f = settings_.channels[ch].euclidean_steps;
                settings_.channels[ch].euclidean_fills =
                    (max_f > 0 && val > max_f) ? max_f : val;
            }
            break;
        case PENDING_OFFSET:
            if (ch < 6) settings_.channels[ch].set_euclidean_offset(val);
            break;
        case PENDING_SCALE:
            if (ch < 6) settings_.channels[ch].set_quantizer_scale(val);
            break;
        case PENDING_SEED:
            settings_.global_seed = val;
            break;
        case PENDING_SYNC_RESET:
            settings_.sync_source = (val >> 1) & 1;
            settings_.reset_mode  =  val       & 1;
            break;
        default: break;
    }
    config_dirty_ = true;
    last_change_time_ms_ = to_ms_since_boot(get_absolute_time());
    sync_params_to_core1();
}

void ClockworksCard::process_incoming_midi_packet(uint8_t const packet[4]) {
    static uint32_t last_midi_stop_time_ms = 0;
    uint8_t cin = packet[0] & 0x0F;
    uint8_t len = 0;
    switch (cin) {
        case 0x05: case 0x0F:
            len = 1;
            break;
        case 0x02: case 0x06: case 0x0C: case 0x0D:
            len = 2;
            break;
        case 0x03: case 0x04: case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0E:
            len = 3;
            break;
        default:
            len = 0;
            break;
    }
    
    static uint8_t sysex_buf[256];
    static int sysex_len = 0;
    
    // Check for real-time messages in the valid bytes
    for (int i = 0; i < len; i++) {
        uint8_t b = packet[1 + i];
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (b >= 0xF8) {
            if (b == 0xF8) {
                // MIDI Clock tick
                midi_clock_tick_count_++;
                last_midi_clock_time_ms_ = now;
            } else if (b == 0xFA) {
                // MIDI Start: Reset phase and step counts only if not a DAW loop wrap
                if (now - last_midi_stop_time_ms > 100) {
                    midi_reset_counter_++;
                }
                paused_ = false;
                sync_params_to_core1();
                send_sync_packet();
            } else if (b == 0xFB) {
                // MIDI Continue: Unpause and resume without resetting phase/steps
                paused_ = false;
                sync_params_to_core1();
                send_sync_packet();
            } else if (b == 0xFC) {
                // MIDI Stop: Pause execution and record timestamp
                paused_ = true;
                last_midi_stop_time_ms = now;
                sync_params_to_core1();
                send_sync_packet();
            }
            continue;
        }
        
        if (b == 0xF0) {
            sysex_len = 0;
            sysex_buf[sysex_len++] = b;
        } else if (b == 0xF7) {
            if (sysex_len > 0 && sysex_len < 256) {
                sysex_buf[sysex_len++] = b;
                process_incoming_sysex(sysex_buf, sysex_len);
            }
            sysex_len = 0;
        } else {
            if (sysex_len > 0 && sysex_len < 255) {
                sysex_buf[sysex_len++] = b;
            } else {
                sysex_len = 0;
            }
        }
    }
    
    if (cin == 0x0B && len == 3) {
        uint8_t channel = packet[1] & 0x0F;
        uint8_t cc = packet[2];
        uint8_t val = packet[3];
        process_midi_cc(channel, cc, val);
    } else if ((cin == 0x09 || cin == 0x08) && len == 3) {
        uint8_t channel = packet[1] & 0x0F;
        uint8_t note = packet[2] & 0x7F;
        uint8_t velocity = packet[3] & 0x7F;
        bool is_note_on = (cin == 0x09) && (velocity > 0);

        for (int i = 0; i < 6; i++) {
            if (settings_.channels[i].get_trigger_source() == 7) {
                uint8_t target_ch = settings_.channels[i].clock_modifier - 40;
                if (target_ch == channel) {
                    if (is_note_on) {
                        note_stacks_[i].push(note, velocity);
                        states_[i].midi_note_num = note;
                        states_[i].midi_velocity = velocity;
                        states_[i].midi_note_active = true;
                        states_[i].midi_trigger_pending = true;
                    } else {
                        note_stacks_[i].remove(note);
                        if (note_stacks_[i].empty()) {
                            states_[i].midi_note_active = false;
                        } else {
                            NoteEntry top = note_stacks_[i].top();
                            states_[i].midi_note_num = top.note;
                            states_[i].midi_velocity = top.velocity;
                            states_[i].midi_note_active = true;
                            states_[i].midi_trigger_pending = true;
                        }
                    }
                }
            }
        }
    }
}

void ClockworksCard::process_usb_midi_device() {
    if (tud_midi_available()) {
        uint8_t packet[4];
        while (tud_midi_packet_read(packet)) {
            process_incoming_midi_packet(packet);
        }
    }
}

void ClockworksCard::process_midi_cc(uint8_t midi_ch, uint8_t cc, uint8_t val) {
    (void)midi_ch; // Ignore MIDI channel (Omni CC)
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    
    if (cc >= 10 && cc <= 69) {
        uint8_t ch = (cc - 10) / 10;
        uint8_t param = cc % 10;
        if (ch >= 6 || param > 9) return;
        
        ChannelConfig &ch_config = settings_.channels[ch];
        parameter_feedback_signed_val_ = 0;
        
        switch (param) {
            case 0: { // Clock Modifier / Trigger Src / MIDI combined
                uint8_t mod = (val * 55) / 127;
                if (pending_main_.active && pending_main_.channel == ch && pending_main_.type == PENDING_MODIFIER) {
                    pending_main_.active = false;
                }
                if (ch_config.clock_modifier != mod) {
                    ch_config.clock_modifier = mod;
                    if (mod >= 40 && mod <= 55) {
                        last_midi_channel_[ch] = mod;
                    }
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    division_feedback_timer_ = 1200;
                    division_feedback_value_ = mod;
                    sync_params_to_core1();
                }
                break;
            }
            case 1: { // Steps
                uint8_t steps = (val * 16) / 127;
                if (pending_x_.active && pending_x_.channel == ch && pending_x_.type == PENDING_STEPS) {
                    pending_x_.active = false;
                }
                if (ch_config.euclidean_steps != steps) {
                    ch_config.euclidean_steps = steps;
                    if (ch_config.euclidean_fills > steps) ch_config.euclidean_fills = steps;
                    if (ch_config.get_quantizer_scale() >= SCALE_NUM_SCALES) {
                        ch_config.set_quantizer_scale(SCALE_OFF);
                    }
                    if (steps > 0) {
                        if (ch_config.get_euclidean_offset() >= steps) {
                            ch_config.set_euclidean_offset(steps - 1);
                        }
                    } else {
                        ch_config.set_euclidean_offset(0);
                    }
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_value_ = steps;
                    parameter_feedback_mode_  = FEEDBACK_BINARY;
                    sync_params_to_core1();
                }
                break;
            }
            case 2: { // Fills
                uint8_t max_fills = (ch >= 4) ? 16 : 255;
                uint8_t fills = (val * max_fills) / 127;
                if (pending_y_.active && pending_y_.channel == ch && pending_y_.type == PENDING_FILLS) {
                    pending_y_.active = false;
                }
                if (ch_config.euclidean_fills != fills) {
                    ch_config.euclidean_fills = fills;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    if (max_fills == 255) {
                        parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                        parameter_feedback_value_ = (uint8_t)(fills >> 2);
                    } else {
                        parameter_feedback_mode_  = FEEDBACK_BINARY;
                        parameter_feedback_value_ = (uint8_t)fills;
                    }
                    sync_params_to_core1();
                }
                break;
            }
            case 3: { // Wave Shape
                uint8_t max_shape = (ch >= 4) ? 5 : (WAVE_NUM_SHAPES - 1);
                uint8_t shape = (val * max_shape) / 127;
                if (pending_main_.active && pending_main_.channel == ch && pending_main_.type == PENDING_SHAPE) {
                    pending_main_.active = false;
                }
                if (ch_config.wave_shape != shape) {
                    ch_config.wave_shape = shape;
                    calculate_state_inverses(ch, shape, ch_config.wave_param);
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_value_ = shape;
                    parameter_feedback_mode_  = FEEDBACK_BINARY;
                    parameter_feedback_signed_val_ = 0;
                    sync_params_to_core1();
                }
                break;
            }
            case 4: { // Wave Param
                uint8_t param = (val * 255) / 127;
                if (ch_config.wave_param != param) {
                    ch_config.wave_param = param;
                    calculate_state_inverses(ch, ch_config.wave_shape, param);
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                    parameter_feedback_value_ = (uint8_t)(param >> 2);
                    sync_params_to_core1();
                }
                break;
            }
            case 5: { // Probability
                uint8_t prob = (val * 100) / 127;
                if (ch_config.probability != prob) {
                    ch_config.probability = prob;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                    parameter_feedback_value_ = (uint8_t)((prob * 63) / 100);
                    sync_params_to_core1();
                }
                break;
            }
            case 6: { // Quantizer Scale
                uint8_t scale = (val * 10) / 127;
                if (pending_y_.active && pending_y_.channel == ch && pending_y_.type == PENDING_SCALE) {
                    pending_y_.active = false;
                }
                if (ch_config.get_quantizer_scale() != scale) {
                    ch_config.set_quantizer_scale(scale);
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_value_ = scale;
                    parameter_feedback_mode_  = FEEDBACK_BINARY;
                    sync_params_to_core1();
                }
                break;
            }
            case 7: { // Loop length
                uint8_t loop = (val * 64) / 127;
                if (pending_main_.active && pending_main_.channel == ch && pending_main_.type == PENDING_LOOP) {
                    pending_main_.active = false;
                }
                if (ch_config.loop_length != loop) {
                    ch_config.loop_length = loop;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_value_ = loop;
                    parameter_feedback_mode_  = FEEDBACK_BINARY;
                    sync_params_to_core1();
                }
                break;
            }
            case 8: { // Level
                uint8_t level = (val * 200) / 127;
                if (ch_config.level != level) {
                    ch_config.level = level;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    if (ch < 4) {
                        parameter_feedback_mode_       = FEEDBACK_BAR_BIPOLAR;
                        parameter_feedback_signed_val_ = (int8_t)((level - 100) * 32 / 100);
                        parameter_feedback_value_      = 0;
                    } else {
                        parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                        parameter_feedback_value_ = (uint8_t)((level * 63) / 200);
                    }
                    sync_params_to_core1();
                }
                break;
            }
            case 9: { // Trigger Source
                uint8_t src = (val * 7) / 127;
                if (ch_config.get_trigger_source() != src) {
                    ch_config.set_trigger_source(src);
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    sync_params_to_core1();
                }
                break;
            }
        }
    } else if (cc >= 70 && cc <= 74) {
        switch (cc) {
            case 70: { // BPM (40-250)
                uint16_t bpm = 40 + (val * 210) / 127;
                if (settings_.bpm != bpm) {
                    settings_.bpm = bpm;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                    parameter_feedback_value_ = (uint8_t)(((bpm - 40) * 63) / 210);
                    sync_params_to_core1();
                }
                break;
            }
            case 71: { // Swing (0-50)
                uint8_t swing = (val * 50) / 127;
                if (settings_.global_swing != swing) {
                    settings_.global_swing = swing;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                    parameter_feedback_value_ = (uint8_t)((swing * 63) / 50);
                    sync_params_to_core1();
                }
                break;
            }
            case 72: { // Humanize (0-100)
                uint8_t humanize = (val * 100) / 127;
                if (settings_.global_humanize != humanize) {
                    settings_.global_humanize = humanize;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                    parameter_feedback_value_ = (uint8_t)((humanize * 63) / 100);
                    sync_params_to_core1();
                }
                break;
            }
            case 73: { // Seed (0-63)
                uint8_t seed = (val * 63) / 127;
                if (pending_x_.active && pending_x_.channel == 6u && pending_x_.type == PENDING_SEED) {
                    pending_x_.active = false;
                }
                if (settings_.global_seed != seed) {
                    settings_.global_seed = seed;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_value_ = seed;
                    parameter_feedback_mode_  = FEEDBACK_BINARY;
                    sync_params_to_core1();
                }
                break;
            }
            case 74: { // Sync/Reset (0-3)
                uint8_t select = (val * 3) / 127;
                uint8_t new_sync = (select >> 1) & 1;
                uint8_t new_reset = select & 1;
                if (pending_y_.active && pending_y_.channel == 6u && pending_y_.type == PENDING_SYNC_RESET) {
                    pending_y_.active = false;
                }
                if (settings_.sync_source != new_sync || settings_.reset_mode != new_reset) {
                    settings_.sync_source = new_sync;
                    settings_.reset_mode = new_reset;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_value_ = (uint8_t)select;
                    parameter_feedback_mode_  = FEEDBACK_BINARY;
                    sync_params_to_core1();
                }
                break;
            }
        }
    } else if (cc >= 75 && cc <= 80) {
        uint8_t ch = cc - 75;
        if (ch < 6) {
            ChannelConfig &ch_config = settings_.channels[ch];
            uint8_t glide = (val * 255) / 127;
            if (ch_config.glide != glide) {
                ch_config.glide = glide;
                config_dirty_ = true;
                last_change_time_ms_ = now_ms;
                sync_params_to_core1();
            }
        }
    }
    if (config_dirty_) {
        lock_knobs();
    }
}

void __not_in_flash_func(ClockworksCard::reset_channels_core1)(uint8_t global_seed) {
    ext_pulse_counter_ = 0;
    midi_pulse_counter_ = 0;
    master_phase_ = 0;
    for (int i = 0; i < 6; i++) {
        states_[i].phase = 0;
        states_[i].step_index = 0;
        states_[i].loop_step_count = 0;
        states_[i].current_pitch = 60.0f;
        // Compute correct initial step_active from euclidean pattern at step_index=0.
        // Without this, every channel fires on the very first beat regardless of offset.
        {
            volatile const ChannelConfig& vcfg = g_shared_params[g_shared_params_idx].channels[i];
            uint8_t eu_steps   = vcfg.euclidean_steps;
            uint8_t eu_fills   = vcfg.euclidean_fills;
            // get_euclidean_offset() = (quantizer_scale_and_offset >> 4) & 0x0F
            uint8_t eu_offset  = (vcfg.quantizer_scale_and_offset >> 4) & 0x0F;
            if (eu_steps > 0 && eu_fills > 0) {
                uint32_t adjusted = eu_offset % eu_steps; // step_index=0
                uint32_t num = adjusted * eu_fills;
                uint32_t rem = num - (num / eu_steps) * eu_steps;
                states_[i].step_active = (rem < (uint32_t)eu_fills);
            } else {
                states_[i].step_active = true;
            }
        }
        states_[i].ext_pulse_ch_counter = 0;
        states_[i].step_triggered = true;
        states_[i].bounce_decay = 0;
        states_[i].delay_impulse_counter = 0;
        states_[i].delay_write_ptr = 0;
        states_[i].rand_pending = false;
        states_[i].rand_pending_val = 0;
        states_[i].bounce_decay_coeff = 65536; // Reset to neutral so CV_DELAY bounce decays correctly after reset
        states_[i].last_value = 0;
        states_[i].last_gate_high = false;
        states_[i].telemetry_max = 0;
        states_[i].telemetry_min = 0;
        states_[i].last_q_input_note = INT16_MIN;
        states_[i].last_q_scale = 0xFF;
        states_[i].last_q_output_note = 60;
        states_[i].jitter_phase = 0;

        if (i < 4) {
            std::memset(delay_buffers[i], 0, (delay_buffer_masks[i] + 1) * sizeof(int16_t));
        }

        // Seed the generator uniquely for each channel
        rand_seed = 0x12345678U + ((uint32_t)global_seed * 0x7FEDCBA9U) + (uint32_t)i * 0x13579BDFU;

        for (int j = 0; j < 64; j++) {
            if (i >= 4) {
                states_[i].random_history[j] = (int16_t)(fast_rand() & 0xFF);
            } else {
                states_[i].random_history[j] = (int16_t)((fast_rand() & 0xFFF) - 2048);
            }
        }
        states_[i].rand_current = states_[i].random_history[0];
        states_[i].rand_next = states_[i].random_history[1];
    }
}

// Render parameter feedback on all 6 LEDs.
// FEEDBACK_BAR_UNIPOLAR: val 0-63 fills LEDs left-to-right with fractional boundary LED brightness
// FEEDBACK_BAR_BIPOLAR:  signed_val -32..+32, LEDs 0-2 = negative side, LEDs 3-5 = positive side
// FEEDBACK_BINARY:       6-bit binary of val on LEDs 0-5; val==64 flashes LED 5
void ClockworksCard::render_leds_feedback(uint8_t mode, uint8_t val, int8_t signed_val, uint32_t now_ms) {
    if (mode == FEEDBACK_BAR_UNIPOLAR) {
        uint32_t scaled = (uint32_t)val * 6u * 4096u / 63u; // 0..24576
        int full = (int)(scaled / 4096u);
        if (full > 6) full = 6;
        uint16_t frac = (uint16_t)(scaled % 4096u);
        for (int i = 0; i < 6; i++) {
            if (i < full)                    LedBrightness(i, 4095);
            else if (i == full && full < 6)  LedBrightness(i, frac);
            else                             LedBrightness(i, 0);
        }
    } else if (mode == FEEDBACK_BAR_UNIPOLAR_BLINK) {
        if ((now_ms % 400) < 200) {
            uint32_t scaled = (uint32_t)val * 6u * 4096u / 63u; // 0..24576
            int full = (int)(scaled / 4096u);
            if (full > 6) full = 6;
            uint16_t frac = (uint16_t)(scaled % 4096u);
            for (int i = 0; i < 6; i++) {
                if (i < full)                    LedBrightness(i, 4095);
                else if (i == full && full < 6)  LedBrightness(i, frac);
                else                             LedBrightness(i, 0);
            }
        } else {
            for (int i = 0; i < 6; i++) {
                LedBrightness(i, 0);
            }
        }
    } else if (mode == FEEDBACK_BAR_BIPOLAR) {
        // LEDs 0-2: negative side (fills from LED 2 toward LED 0 as magnitude increases)
        // LEDs 3-5: positive side (fills from LED 3 toward LED 5 as magnitude increases)
        if (signed_val >= 0) {
            uint32_t mag    = (uint32_t)signed_val;
            uint32_t scaled = mag * 3u * 4096u / 32u;
            int      full   = (int)(scaled / 4096u);
            if (full > 3) full = 3;
            uint16_t frac = (uint16_t)(scaled % 4096u);
            for (int i = 0; i < 3; i++) LedBrightness(i, 0);
            for (int i = 3; i < 6; i++) {
                int pos = i - 3;
                if (pos < full)                    LedBrightness(i, 4095);
                else if (pos == full && full < 3)  LedBrightness(i, frac);
                else                               LedBrightness(i, 0);
            }
        } else {
            uint32_t mag    = (uint32_t)(-(int)signed_val);
            uint32_t scaled = mag * 3u * 4096u / 32u;
            int      full   = (int)(scaled / 4096u);
            if (full > 3) full = 3;
            uint16_t frac = (uint16_t)(scaled % 4096u);
            for (int i = 3; i < 6; i++) LedBrightness(i, 0);
            for (int i = 0; i < 3; i++) {
                int pos = 2 - i; // LED 2 = pos 0 (nearest centre)
                if (pos < full)                    LedBrightness(i, 4095);
                else if (pos == full && full < 3)  LedBrightness(i, frac);
                else                               LedBrightness(i, 0);
            }
        }
    } else { // FEEDBACK_BINARY
        bool dim_underlay = (signed_val == 2);
        for (int i = 0; i < 6; i++) {
            bool active = (val >> i) & 1;
            if (active) {
                LedBrightness(i, 4095);
            } else {
                LedBrightness(i, dim_underlay ? 100 : 0);
            }
        }
    }
}

void __not_in_flash_func(ClockworksCard::ProcessSample)() {
    // Sync external period counters (one per ISR call at 48kHz)
    sync_sample_counter_++;
    samples_since_last_pulse_++;
    samples_since_last_midi_clock_++;

    // Load parameters from Core 0
    uint32_t active_idx = g_shared_params_idx;

    bool external_pulse_received = false;

    static SharedParams cached_params;
    static uint32_t last_active_idx = 0xFFFFFFFF;
    if (active_idx != last_active_idx) {
        last_active_idx = active_idx;
        volatile SharedParams &p = g_shared_params[active_idx];
        cached_params.bpm = p.bpm;
        cached_params.sync_source = p.sync_source;
        cached_params.reset_mode = p.reset_mode;
        cached_params.ext_ppqn = p.ext_ppqn;
        cached_params.global_swing = p.global_swing;
        cached_params.global_humanize = p.global_humanize;
        cached_params.global_seed = p.global_seed;
        cached_params.global_paused = p.global_paused;
        cached_params.manual_reset_counter = p.manual_reset_counter;
        cached_params.base_increment = p.base_increment;
        cached_params.swing_mult_even = p.swing_mult_even;
        cached_params.swing_mult_odd = p.swing_mult_odd;
        for (int i = 0; i < 6; i++) {
            cached_params.channels[i].clock_modifier = p.channels[i].clock_modifier;
            cached_params.channels[i].euclidean_steps = p.channels[i].euclidean_steps;
            cached_params.channels[i].euclidean_fills = p.channels[i].euclidean_fills;
            cached_params.channels[i].wave_shape = p.channels[i].wave_shape;
            cached_params.channels[i].wave_param = p.channels[i].wave_param;
            cached_params.channels[i].probability = p.channels[i].probability;
            cached_params.channels[i].quantizer_scale_and_offset = p.channels[i].quantizer_scale_and_offset;
            cached_params.channels[i].loop_length = p.channels[i].loop_length;
            cached_params.channels[i].level = p.channels[i].level;
        }
    }

    static uint8_t last_global_seed = 0xFF;
    bool seed_changed = (cached_params.global_seed != last_global_seed);
    if (seed_changed) {
        last_global_seed = cached_params.global_seed;
    }

    static bool first_run = true;
    if (first_run || seed_changed) {
        reset_channels_core1(cached_params.global_seed);
        first_run = false;
    }

    // Detect MIDI Reset
    static uint32_t last_seen_midi_resets = 0;
    uint32_t current_midi_resets = midi_reset_counter_;
    if (current_midi_resets != last_seen_midi_resets) {
        last_seen_midi_resets = current_midi_resets;
        reset_channels_core1(cached_params.global_seed);
    }

    // Parse MIDI clock tick
    static uint32_t last_seen_midi_ticks = 0;
    uint32_t current_midi_ticks = midi_clock_tick_count_;
    bool midi_clock_tick_received = false;
    if (current_midi_ticks != last_seen_midi_ticks) {
        last_seen_midi_ticks = current_midi_ticks;
        midi_clock_tick_received = true;
        samples_since_last_midi_clock_ = 0;
    }

    bool midi_clock_active = (samples_since_last_midi_clock_ < 2 * SAMPLE_RATE);

    static uint32_t sync_midi_sample_counter = 0;
    sync_midi_sample_counter++;

    static uint32_t last_midi_period_candidate = 0;
    bool midi_pulse_processed = false;

    if (!midi_clock_active) {
        midi_pulse_counter_ = 0;
    }

    if (midi_clock_tick_received) {
        uint32_t period = sync_midi_sample_counter;
        sync_midi_sample_counter = 0;

        uint32_t ppqn = 24;
        uint32_t min_period = SAMPLES_PER_MINUTE / (300 * ppqn);
        uint32_t max_period = SAMPLES_PER_MINUTE / (30 * ppqn);
        if (min_period < 5) min_period = 5;
        if (max_period > 240000) max_period = 240000;

        bool is_restart = (midi_period_samples_ > 0) && (period > 12000); // 500ms threshold
        if (midi_pulse_counter_ == 0 || is_restart) {
            for (int i = 0; i < 6; i++) {
                states_[i].phase = 0;
                states_[i].step_index = 0;
                states_[i].loop_step_count = 0;
                {
                    volatile const ChannelConfig& vcfg = g_shared_params[g_shared_params_idx].channels[i];
                    uint8_t eu_steps  = vcfg.euclidean_steps;
                    uint8_t eu_fills  = vcfg.euclidean_fills;
                    uint8_t eu_offset = (vcfg.quantizer_scale_and_offset >> 4) & 0x0F;
                    if (eu_steps > 0 && eu_fills > 0) {
                        uint32_t adjusted = eu_offset % eu_steps;
                        uint32_t num = adjusted * eu_fills;
                        uint32_t rem = num - (num / eu_steps) * eu_steps;
                        states_[i].step_active = (rem < (uint32_t)eu_fills);
                    } else {
                        states_[i].step_active = true;
                    }
                }
                states_[i].ext_pulse_ch_counter = 0;
                states_[i].step_triggered = true;
                states_[i].bounce_decay = 0;
                states_[i].delay_impulse_counter = 0;
            }
            if (midi_period_samples_ == 0 || is_restart) {
                if (midi_period_samples_ == 0) {
                    midi_period_samples_ = safe_div_u32(SAMPLES_PER_MINUTE, (uint32_t)cached_params.bpm * ppqn);
                }
            }
            midi_pulse_counter_ = 1;
            last_midi_period_candidate = 0;
        } else {
            if (period >= min_period && period <= max_period) {
                if (midi_pulse_counter_ == 1) {
                    last_midi_period_candidate = period;
                    midi_pulse_counter_ = 2;
                } else if (midi_pulse_counter_ == 2) {
                    uint32_t diff = (period > last_midi_period_candidate) ? (period - last_midi_period_candidate) : (last_midi_period_candidate - period);
                    if (diff * 8 <= last_midi_period_candidate) {
                        midi_period_samples_ = period;
                        midi_pulse_counter_ = 3;
                        midi_pulse_processed = true;
                    } else {
                        last_midi_period_candidate = period;
                    }
                } else {
                    midi_period_samples_ = (7 * midi_period_samples_ + period) >> 3;
                    midi_pulse_counter_++;
                    midi_pulse_processed = true;
                }
            }
        }
    }

    // Detect external clock (Pulse 1, Jack 5)
    if (PulseIn1RisingEdge()) {
        uint32_t period = sync_sample_counter_;
        sync_sample_counter_ = 0;

        uint32_t ppqn = cached_params.ext_ppqn;
        if (ppqn == 0) ppqn = 4; // safety fallback
        
        // Calculate valid period bounds corresponding to 30 to 300 BPM at the active PPQN
        uint32_t min_period = SAMPLES_PER_MINUTE / (300 * ppqn);
        uint32_t max_period = SAMPLES_PER_MINUTE / (30 * ppqn);
        if (min_period < 50) min_period = 50;
        if (max_period > 240000) max_period = 240000;

        static uint32_t last_period_candidate = 0;

        // Detect a restart if the clock has stopped for a significant period
        bool is_restart = external_clock_active_ && (ext_period_samples_ > 0) && (period > 2 * ext_period_samples_);
        if (!external_clock_active_ || is_restart) {
            // First pulse of the clock: reset phases to start in perfect alignment
            // ONLY if MIDI clock is not taking priority
            if (!midi_clock_active) {
                for (int i = 0; i < 6; i++) {
                    states_[i].phase = 0;
                    states_[i].step_index = 0;
                    states_[i].loop_step_count = 0;
                    {
                        volatile const ChannelConfig& vcfg = g_shared_params[g_shared_params_idx].channels[i];
                        uint8_t eu_steps  = vcfg.euclidean_steps;
                        uint8_t eu_fills  = vcfg.euclidean_fills;
                        uint8_t eu_offset = (vcfg.quantizer_scale_and_offset >> 4) & 0x0F;
                        if (eu_steps > 0 && eu_fills > 0) {
                            uint32_t adjusted = eu_offset % eu_steps;
                            uint32_t num = adjusted * eu_fills;
                            uint32_t rem = num - (num / eu_steps) * eu_steps;
                            states_[i].step_active = (rem < (uint32_t)eu_fills);
                        } else {
                            states_[i].step_active = true;
                        }
                    }
                    states_[i].ext_pulse_ch_counter = 0;
                    states_[i].step_triggered = true;
                    states_[i].bounce_decay = 0;
                    states_[i].delay_impulse_counter = 0;
                }
            }
            external_clock_active_ = true;
            samples_since_last_pulse_ = 0;
            if (ext_period_samples_ == 0 || is_restart) {
                // Keep the last measured period if restarting, to avoid startup glitches
                if (ext_period_samples_ == 0) {
                    ext_period_samples_ = safe_div_u32(SAMPLES_PER_MINUTE, (uint32_t)cached_params.bpm * ppqn);
                }
            }
            ext_pulse_counter_ = 0; // Start counter at 0 on first pulse
            last_period_candidate = 0;
        } else {
            // Clock is already active, process the interval period if within valid bounds
            if (period >= min_period && period <= max_period) {
                if (ext_pulse_counter_ == 0) {
                    // Store the first measured interval as a candidate
                    last_period_candidate = period;
                    ext_pulse_counter_ = 1;
                    samples_since_last_pulse_ = 0;
                } else if (ext_pulse_counter_ == 1) {
                    // Check consistency with the first interval (within 12.5%)
                    uint32_t diff = (period > last_period_candidate) ? (period - last_period_candidate) : (last_period_candidate - period);
                    if (diff * 8 <= last_period_candidate) {
                        // Consistent: lock instantly and snap the running period
                        ext_period_samples_ = period;
                        ext_pulse_counter_ = 2; // Locked state
                        external_pulse_received = true;
                    } else {
                        // Inconsistent: treat this as the new candidate
                        last_period_candidate = period;
                    }
                    samples_since_last_pulse_ = 0;
                } else {
                    // Already locked: apply PPQN-adaptive smoothing window
                    uint32_t weight = 15;
                    uint32_t shift = 4;
                    if (ppqn == 1) {
                        weight = 3;
                        shift = 2;
                    } else if (ppqn == 4) {
                        weight = 7;
                        shift = 3;
                    }
                    ext_period_samples_ = (weight * ext_period_samples_ + period) >> shift;
                    samples_since_last_pulse_ = 0;
                    ext_pulse_counter_++;
                    external_pulse_received = true;
                }
            }
        }
    }

    uint32_t sync_ppqn = 4;
    uint32_t sync_pulse_counter = 0;
    bool sync_pulse_received = false;

    if (midi_clock_active) {
        sync_ppqn = 24;
        sync_pulse_counter = midi_pulse_counter_;
        sync_pulse_received = midi_pulse_processed;
    } else if (external_clock_active_ && ext_period_samples_ > 0) {
        sync_ppqn = cached_params.ext_ppqn;
        sync_pulse_counter = ext_pulse_counter_;
        sync_pulse_received = external_pulse_received;
    }

    // Timeout external clock
    if (samples_since_last_pulse_ > (2 * SAMPLE_RATE)) { // 2 seconds
        external_clock_active_ = false;
        // Keep ext_period_samples_ intact as the flywheel memory
        ext_pulse_counter_ = 0;
        for (int i = 0; i < 6; i++) {
            states_[i].ext_pulse_ch_counter = 0;
        }
    }

    // Handle Reset Input (Pulse 2, Jack 6) or Manual/Factory Reset from Core 0
    static uint32_t last_manual_reset_counter = 0;
    bool manual_reset_triggered = (cached_params.manual_reset_counter != last_manual_reset_counter);
    if (manual_reset_triggered) {
        last_manual_reset_counter = cached_params.manual_reset_counter;
    }

    bool run_gate_paused = cached_params.global_paused;
    bool pulse2_connected = Connected(Input::Pulse2);

    if ((pulse2_connected && PulseIn2RisingEdge() && cached_params.reset_mode == 0) || manual_reset_triggered) {
        // Reset Mode: Align all step counts and phases to 0
        reset_channels_core1(cached_params.global_seed);
    } else if (pulse2_connected && cached_params.reset_mode == 1) {
        // Run Gate Mode: Pause clock execution when Pulse 2 is LOW, reset phases on transition
        static bool was_pulse2_low = true;
        if (!PulseIn2()) {
            run_gate_paused = true;
            if (!was_pulse2_low) {
                // Transitioned from HIGH to LOW (run end): Reset all phases, steps, and loops immediately!
                reset_channels_core1(cached_params.global_seed);
                was_pulse2_low = true;
            }
        } else {
            if (was_pulse2_low) {
                // Transitioned from LOW to HIGH (run start): align all phases and step indices to 0 for instant sync!
                reset_channels_core1(cached_params.global_seed);
                was_pulse2_low = false;
            }
        }
    }

    // Determine base step increment
    uint32_t base_inc = cached_params.base_increment;
    if (midi_clock_active && midi_period_samples_ > 0) {
        static uint32_t last_midi_period = 0;
        static uint32_t cached_midi_base_inc = 0;
        if (midi_period_samples_ != last_midi_period) {
            last_midi_period = midi_period_samples_;
            cached_midi_base_inc = safe_div_u32(0xFFFFFFFFU, (uint32_t)midi_period_samples_ * 24);
        }
        base_inc = cached_midi_base_inc;
    } else if (external_clock_active_ && ext_period_samples_ > 0 && cached_params.ext_ppqn > 0) {
        static uint32_t last_active_idx = 0xFFFFFFFF;
        static uint32_t last_ext_period = 0;
        static uint32_t cached_ext_base_inc = 0;
        if (active_idx != last_active_idx || ext_period_samples_ != last_ext_period) {
            last_active_idx = active_idx;
            last_ext_period = ext_period_samples_;
            cached_ext_base_inc = safe_div_u32(0xFFFFFFFFU, (uint32_t)ext_period_samples_ * cached_params.ext_ppqn);
        }
        base_inc = cached_ext_base_inc;
    }

    // Global swing is applied per-channel inside the channel loop below.

    // Static variables for analog trigger state tracking (Schmitt trigger)
    static bool last_analog_states[4] = {false, false, false, false};

    // Pre-compute rising-edge gate status for all 6 channels based on the previous sample
    bool gate_rising[6] = {false};
    for (int ch = 0; ch < 6; ch++) {
        bool current_gate_high = (states_[ch].last_value > 0);
        if (current_gate_high && !states_[ch].last_gate_high) {
            gate_rising[ch] = true;
        }
        states_[ch].last_gate_high = current_gate_high;
    }

    if (!run_gate_paused) {
        master_phase_ += base_inc;
    }

    // Process all 6 channels
    for (int i = 0; i < 6; i++) {
        // Snapshot the volatile shared config in one memcpy to guarantee a
        // consistent view (no torn reads between individual volatile field loads).
        const ChannelConfig &ch_config = cached_params.channels[i];
        ChannelState &state = states_[i];

        int16_t local_prob = ch_config.probability;
        int16_t local_param = ch_config.wave_param;
        int16_t local_modifier = ch_config.get_clock_modifier();
        uint8_t trigger_src = ch_config.get_trigger_source();
        int16_t local_fills = ch_config.euclidean_fills;
        uint8_t current_scale = ch_config.get_quantizer_scale();

        if (trigger_src == 7) {
            if (state.midi_trigger_pending) {
                state.midi_trigger_pending = false;
                state.phase = 0;
                state.step_active = true;
                state.step_triggered = true;
            }
        }

        bool is_one_shot = (ch_config.wave_shape == WAVE_GATE ||
                            ch_config.wave_shape == WAVE_RATCHET ||
                            ch_config.wave_shape == WAVE_ENVELOPE ||
                            ch_config.wave_shape == WAVE_LOG_ENVELOPE ||
                            ch_config.wave_shape == WAVE_RANDOM_SH ||
                            ch_config.wave_shape == WAVE_RANDOM_SM);

        // Check if a cable is connected to this channel's dedicated input jack (Jacks 1-4 for Channels 0-3 only)
        bool input_connected = (i < 4) && Connected((Input)i);
        int32_t channel_cv = 0;
        if (input_connected) {
            if (i == 0) channel_cv = AudioIn1();
            else if (i == 1) channel_cv = AudioIn2();
            else if (i == 2) channel_cv = CVIn1();
            else if (i == 3) channel_cv = CVIn2();
        }

        // Schmitt trigger for analog channel trigger detection
        bool is_analog_triggered = false;
        if (input_connected) {
            bool last_state = last_analog_states[i];
            if (!last_state && channel_cv > 1000) {
                last_analog_states[i] = true;
                is_analog_triggered = true;
            } else if (last_state && channel_cv < 200) {
                last_analog_states[i] = false;
            }
        }

        // Rising-edge trigger status for channel i
        bool channel_trigger = false;
        if (input_connected) {
            channel_trigger = is_analog_triggered;
        }

        // WAVE_GATE / WAVE_RATCHET / LFO Shapes: Modulate parameter by channel-specific CV
        if (input_connected && ch_config.wave_shape != WAVE_RANDOM_SH && ch_config.wave_shape != WAVE_RANDOM_SM && ch_config.wave_shape != WAVE_ENVELOPE && ch_config.wave_shape != WAVE_LOG_ENVELOPE && ch_config.wave_shape != WAVE_CV_DELAY) {
            int32_t cv_mod = channel_cv >> 4; // Map -2048..2047 to -128..127
            local_param += cv_mod;
            if (local_param < 0) local_param = 0;
            if (local_param > 255) local_param = 255;
        }

        // Apply clock modifier Q16 multiplier
        if (local_modifier < 0 || local_modifier >= 34) {
            local_modifier = 20;
        }
        uint32_t phase_inc = mul_u32_u32_shift16(base_inc, clock_multipliers_q16[local_modifier]);

        // Apply global swing per-channel (only for x2 and faster clock multipliers)
        if (cached_params.global_swing > 0 && local_modifier >= 21) {
            if (state.loop_step_count % 2 == 0) {
                phase_inc = ((uint64_t)phase_inc * cached_params.swing_mult_even) >> 16;
            } else {
                phase_inc = ((uint64_t)phase_inc * cached_params.swing_mult_odd) >> 16;
            }
        }

        bool is_envelope_triggered_mode = ((ch_config.wave_shape == WAVE_ENVELOPE || ch_config.wave_shape == WAVE_LOG_ENVELOPE) && input_connected);

        if (is_envelope_triggered_mode) {
            // Triggered Envelope Generator Mode: Bypasses standard clock step index advance
            if (channel_trigger) {
                state.phase = 0;
                state.step_active = true;
                state.jitter_phase = 0;
            } else if (state.phase < 0xFFFFFFFF) {
                uint32_t old_phase = state.phase;
                state.phase += phase_inc;
                if (state.phase < old_phase) {
                    state.phase = 0xFFFFFFFF; // Clamp decay output at 0
                }
            }
        } else if (!run_gate_paused) {
            // Align phase to match external clock pulse grid
            if (trigger_src == 0 && (midi_clock_active || (external_clock_active_ && ext_period_samples_ > 0)) && sync_pulse_received) {
                uint32_t expected_phase = 0;
                if (sync_ppqn > 0) {
                    uint32_t pulse_phase_step = 0x100000000ULL / sync_ppqn;
                    uint32_t step = (uint32_t)(((uint64_t)pulse_phase_step * clock_multipliers_q16[local_modifier]) >> 16);
                    expected_phase = sync_pulse_counter * step;
                }
                int32_t phase_diff = (int32_t)(expected_phase - state.phase);
                
                // If we are in the initial lock phase (first 8 pulses) or phase error is very large (> 1/8th cycle),
                // snap phase directly to expected_phase for instant, perfect synchronization.
                uint32_t abs_diff = (phase_diff < 0) ? -phase_diff : phase_diff;
                if (sync_pulse_counter < 8 || abs_diff > 0x20000000) {
                    state.phase = expected_phase;
                } else {
                    int32_t nudge = phase_diff / 16;
                    if (nudge < 0) {
                        if (state.phase < (uint32_t)(-nudge)) {
                            state.phase = 0;
                        } else {
                            state.phase += nudge;
                        }
                    } else if (nudge > 0) {
                        if (state.phase > 0xFFFFFFFFU - (uint32_t)nudge) {
                            state.phase = 0xFFFFFFFFU;
                        } else {
                            state.phase += nudge;
                        }
                    }
                }
            }

            uint32_t old_phase = state.phase;
            if (trigger_src > 0 && is_one_shot) {
                if (state.phase < 0xFFFFFFFF) {
                    state.phase += phase_inc;
                    if (state.phase < old_phase) {
                        state.phase = 0xFFFFFFFF;
                    }
                }
            } else {
                state.phase += phase_inc;
            }

            bool step_wrap = false;
            if (trigger_src == 7) {
                step_wrap = false;
            } else if (trigger_src > 0) {
                step_wrap = gate_rising[trigger_src - 1];
            } else {
                step_wrap = (state.phase < old_phase);
            }

            // Check clock phase wrap (next step)
            if (step_wrap) {
                state.step_triggered = true;
                if (trigger_src > 0) {
                    state.phase = 0; // Reset phase on trigger
                }
                // Apply humanize timing jitter on step start
                if (cached_params.global_humanize > 0) {
                    uint32_t max_j = 5368709ULL * cached_params.global_humanize;
                    if (max_j > 0) {
                        state.jitter_phase = (uint32_t)(((uint64_t)fast_rand() * max_j) >> 32);
                    } else {
                        state.jitter_phase = 0;
                    }
                } else {
                    state.jitter_phase = 0;
                }

                // Advance step indices
                state.step_index++;
                if (ch_config.euclidean_steps > 0) {
                    if (state.step_index >= ch_config.euclidean_steps) {
                        state.step_index = 0;
                    }
                } else {
                    if (state.step_index >= 16) {
                        state.step_index = 0;
                    }
                }

                // Advance loop step counters
                state.loop_step_count++;
                uint8_t loop_len = ch_config.loop_length; // 0 = infinite, 1..64 = steps
                if (loop_len > 0 && state.loop_step_count >= loop_len) {
                    state.loop_step_count = 0;
                    state.step_index = 0;
                    state.phase = 0; // Reset phase to align sines/LFOs perfectly at loop start
                }

                // Populate random history on every step in infinite mode
                if (ch_config.loop_length == 0) {
                    if (i >= 4) {
                        state.random_history[state.loop_step_count & 63] = (int16_t)(fast_rand() & 0xFF);
                    } else {
                        state.random_history[state.loop_step_count & 63] = (int16_t)((fast_rand() & 0xFFF) - 2048);
                    }
                }

                // Calculate Euclidean step state
                if (ch_config.euclidean_steps > 0) {
                    uint32_t offset = ch_config.get_euclidean_offset();
                    uint32_t adjusted_step = (state.step_index + offset) % ch_config.euclidean_steps;
                    uint32_t num = adjusted_step * local_fills;
                    uint32_t den = ch_config.euclidean_steps;
                    uint32_t rem = num - safe_div_u32(num, den) * den;
                    state.step_active = (rem < (uint32_t)local_fills);
                } else {
                    state.step_active = true;
                }

                // Apply trigger probability
                if (state.step_active) {
                    if (local_prob < 100) {
                        uint8_t roll;
                        if (ch_config.loop_length > 0) {
                            uint32_t hist_val = state.random_history[state.loop_step_count % ch_config.loop_length];
                            if (i < 4) {
                                int32_t val_abs = (int16_t)hist_val;
                                if (val_abs < 0) val_abs = -val_abs;
                                roll = (uint8_t)((val_abs * 100) >> 11);
                            } else {
                                roll = (uint8_t)((hist_val * 100) >> 8);
                            }
                        } else {
                            roll = mul_u32_u32_high(fast_rand(), 100);
                        }
                        if (roll >= local_prob) {
                            state.step_active = false;
                        }
                    }
                }

                // Humanize loop trigger mutation (fills/ghost notes) starting at 50%
                if (cached_params.global_humanize > 50) {
                    uint32_t mutate_chance = (cached_params.global_humanize - 50) / 2;
                    if (mul_u32_u32_high(fast_rand(), 100) < mutate_chance) {
                        state.step_active = !state.step_active;
                    }
                }

                // Sample random gate width for RANDOM shape on Pulse outputs (Channels 4 & 5)
                if (i >= 4 && ch_config.wave_shape == 4) {
                    if (ch_config.loop_length > 0) {
                        uint8_t loop_len_r = ch_config.loop_length;
                        uint8_t idx = state.loop_step_count % loop_len_r;
                        int16_t r_val = state.random_history[idx];
                        if (cached_params.global_humanize > 50) {
                            uint32_t mutate_chance = (cached_params.global_humanize - 50);
                            if (mul_u32_u32_high(fast_rand(), 100) < mutate_chance) {
                                r_val = (int16_t)(fast_rand() & 0xFF);
                            }
                        }
                        state.rand_current = r_val;
                    } else {
                        state.rand_current = state.random_history[state.loop_step_count & 63];
                    }
                }

                // Fetch S&H random targets on new step.
                // Store into rand_pending_val so the voltage latch is deferred
                // until the humanize jitter delay expires (see rendering below).
                // Without humanize this fires at jitter_phase==0 and pending
                // resolves on the same sample the gate opens — perfectly in sync.
                if (state.step_active && (ch_config.wave_shape == WAVE_RANDOM_SH || ch_config.wave_shape == WAVE_RANDOM_SM)) {
                    state.rand_pending = true;
                    if (input_connected) {
                        state.rand_pending_val = channel_cv;
                        state.rand_next = channel_cv;
                    } else {
                        state.rand_pending_val = state.rand_next;
                        if (ch_config.loop_length > 0) {
                            uint8_t loop_len_r = ch_config.loop_length;
                            uint8_t next_idx = (state.loop_step_count + 1) % loop_len_r;
                            int16_t next_val = state.random_history[next_idx];
                            
                            // Non-destructive loop mutation starting at 50%
                            if (cached_params.global_humanize > 50) {
                                uint32_t mutate_chance = (cached_params.global_humanize - 50);
                                if (mul_u32_u32_high(fast_rand(), 100) < mutate_chance) {
                                    // Scale mutation to +/- 1 octave (approx 341 steps)
                                    int16_t offset = (int16_t)(((fast_rand() & 3) - 1) * 341);
                                    if (offset == 0) offset = 341;
                                    next_val += offset;
                                    if (next_val > 2047) next_val = 2047 - (fast_rand() & 0xFF);
                                    if (next_val < -2048) next_val = -2048 + (fast_rand() & 0xFF);
                                }
                            }
                            state.rand_next = next_val;
                        } else {
                            int16_t next_val = (int16_t)((fast_rand() & 0xFFF) - 2048);
                            state.rand_next = next_val;
                            state.random_history[state.loop_step_count & 63] = next_val;
                        }
                    }
                }
            }
        }


        // Output waveform generation
        int16_t val = 0;
        bool humanize_muted = false;

        if (run_gate_paused) {
            // Keep outputs at low level when run gate is paused (rest at 0V)
            val = 0;
        } else if (trigger_src == 7) {
            // ==========================================
            // MIDI Mode Synthesis & CV Generation
            // ==========================================
            uint32_t rendering_phase = state.phase;
            
            if (i < 4) {
                // Analog/CV Outputs 0-3
                switch (ch_config.wave_shape) {
                    case WAVE_GATE: {
                        val = state.midi_note_active ? 2047 : 0;
                        break;
                    }
                    case WAVE_RATCHET: {
                        uint32_t trig_width = 429496730; // ~10% of LFO cycle
                        uint32_t min_delay = trig_width;
                        uint32_t max_delay = 0xFFFFFFFF - trig_width;
                        uint32_t delay = min_delay + mul_u32_u32_shift8(local_param, max_delay - min_delay);
                        bool trig1 = rendering_phase < trig_width;
                        bool trig2 = rendering_phase >= delay && rendering_phase < (delay + trig_width);
                        bool high = state.midi_note_active && (trig1 || trig2);
                        val = high ? 2047 : 0;
                        break;
                    }
                    case WAVE_SINE: {
                        uint32_t offset = (uint32_t)local_param << 24;
                        val = interpolate_sine(rendering_phase + offset);
                        break;
                    }
                    case WAVE_TRIANGLE: {
                        uint32_t clamped = local_param;
                        if (clamped < 2) clamped = 2;
                        if (clamped > 253) clamped = 253;
                        uint32_t skew_point = clamped << 24;
                        uint32_t inv_skew = inv_skew_lut[clamped];
                        uint32_t inv_rem = inv_rem_lut[clamped];
                        if (rendering_phase < skew_point) {
                            val = -2048 + (int16_t)mul_u32_u32_high(rendering_phase, inv_skew);
                        } else {
                            uint32_t rem_phase = rendering_phase - skew_point;
                            val = 2047 - (int16_t)mul_u32_u32_high(rem_phase, inv_rem);
                        }
                        break;
                    }
                    case WAVE_SAW_UP: {
                        int32_t lin = -2048 + (int16_t)mul_u32_u32_high(rendering_phase, 4095);
                        int32_t exp = -2048 + (4095 - interpolate_exp(rendering_phase));
                        int32_t log = -2048 + interpolate_exp(0xFFFFFFFF - rendering_phase);
                        if (local_param == 128) {
                            val = lin;
                        } else if (local_param < 128) {
                            val = log + (((lin - log) * local_param) >> 7);
                        } else {
                            val = lin + (((exp - lin) * (local_param - 128)) >> 7);
                        }
                        break;
                    }
                    case WAVE_SAW_DOWN: {
                        int32_t lin = 2047 - (int16_t)mul_u32_u32_high(rendering_phase, 4095);
                        int32_t exp = 2047 - (4095 - interpolate_exp(rendering_phase));
                        int32_t log = 2047 - interpolate_exp(0xFFFFFFFF - rendering_phase);
                        if (local_param == 128) {
                            val = lin;
                        } else if (local_param < 128) {
                            val = log + (((lin - log) * local_param) >> 7);
                        } else {
                            val = lin + (((exp - lin) * (local_param - 128)) >> 7);
                        }
                        break;
                    }
                    case WAVE_ENVELOPE: {
                        uint32_t mult_phase = mul_u32_u16_shift8_clamp(rendering_phase, env_multiplier_lut[local_param]);
                        uint16_t env_val = interpolate_exp(mult_phase);
                        val = (int16_t)(env_val >> 1); // 0..2047
                        val = (val * state.midi_velocity) / 127;
                        break;
                    }
                    case WAVE_LOG_ENVELOPE: {
                        uint32_t mult_phase = mul_u32_u16_shift8_clamp(rendering_phase, env_multiplier_lut[local_param]);
                        uint16_t env_val = 4095 - interpolate_exp(0xFFFFFFFFU - mult_phase);
                        val = (int16_t)(env_val >> 1); // 0..2047
                        val = (val * state.midi_velocity) / 127;
                        break;
                    }
                    case WAVE_RANDOM_SH: { // S&H -> Pitch CV
                        int note_num = state.midi_note_num;
                        if (current_scale != SCALE_OFF) {
                            note_num = quantize_to_scale(note_num, current_scale);
                        }
                        int32_t temp_note = (note_num - 60) * 256;
                        int32_t abs_note = temp_note < 0 ? -temp_note : temp_note;
                        int32_t quotient_note = (abs_note * 58255) >> 19;
                        val = (int16_t)(temp_note < 0 ? -quotient_note : quotient_note);
                        if (val > 2047) val = 2047;
                        if (val < -2048) val = -2048;
                        break;
                    }
                    case WAVE_RANDOM_SM: { // Smooth -> Velocity CV (0V to 6V)
                        val = (state.midi_velocity * 2047) / 127;
                        break;
                    }
                    case WAVE_TRAPEZOID: {
                        uint32_t clamped = local_param;
                        if (clamped < 2) clamped = 2;
                        if (clamped > 253) clamped = 253;
                        uint32_t F = (clamped * 0x40000000ULL) >> 8;
                        uint32_t R = 0x80000000ULL - F;
                        if (rendering_phase < R) {
                            val = -2048 + (int16_t)mul_u32_u32_high(rendering_phase, state.inv_skew);
                        } else if (rendering_phase < R + F) {
                            val = 2047;
                        } else if (rendering_phase < 2 * R + F) {
                            uint32_t rem = rendering_phase - (R + F);
                            val = 2047 - (int16_t)mul_u32_u32_high(rem, state.inv_skew);
                        } else {
                            val = -2048;
                        }
                        break;
                    }
                    case WAVE_HUMP: {
                        uint32_t clamped = local_param;
                        if (clamped < 2) clamped = 2;
                        if (clamped > 253) clamped = 253;
                        uint32_t skew_point = clamped << 24;
                        if (rendering_phase < skew_point) {
                            uint32_t frac = mul_u32_u32_high(rendering_phase, state.inv_skew);
                            if (frac > 4095) frac = 4095;
                            val = interpolate_sine(frac << 18);
                        } else {
                            uint32_t rem_phase = rendering_phase - skew_point;
                            uint32_t frac = mul_u32_u32_high(rem_phase, state.inv_rem);
                            if (frac > 4095) frac = 4095;
                            val = interpolate_sine(0x40000000U + (frac << 18));
                        }
                        break;
                    }
                    case WAVE_CV_DELAY: {
                        break;
                    }
                    case WAVE_MATH: {
                        uint32_t skew_point = 0x80000000U;
                        if (rendering_phase < skew_point) {
                            val = -2048 + (int16_t)mul_u32_u32_high(rendering_phase, inv_skew_lut[128]);
                        } else {
                            uint32_t rem_phase = rendering_phase - skew_point;
                            val = 2047 - (int16_t)mul_u32_u32_high(rem_phase, inv_rem_lut[128]);
                        }
                        break;
                    }
                }
            } else {
                // Digital Gate/Pulse Outputs 4-5
                uint8_t scaled_delay = ch_config.level / 2;
                uint32_t delay_phase = (uint32_t)scaled_delay * 42949672U;
                if (rendering_phase < delay_phase) {
                    val = 0;
                } else {
                    uint32_t shifted_phase = rendering_phase - delay_phase;
                    switch (ch_config.wave_shape) {
                        case 0: { // GATE
                            val = state.midi_note_active ? 2047 : 0;
                            break;
                        }
                        case 1: { // RATCHET
                            uint32_t trig_width = 429496730;
                            uint32_t min_delay = trig_width;
                            uint32_t max_delay = 0xFFFFFFFF - trig_width;
                            uint32_t delay = min_delay + mul_u32_u32_shift8(local_param, max_delay - min_delay);
                            bool trig1 = shifted_phase < trig_width;
                            bool trig2 = shifted_phase >= delay && shifted_phase < (delay + trig_width);
                            bool high = state.midi_note_active && (trig1 || trig2);
                            val = high ? 2047 : 0;
                            break;
                        }
                        case 2: { // TRIGGER (DELAY)
                            uint32_t trig_width = 288 * phase_inc;
                            if (trig_width > 3865470565U) {
                                trig_width = 3865470565U;
                            }
                            uint32_t max_extra_delay = 3865470565U - trig_width;
                            uint32_t extra_delay = mul_u32_u32_shift8(local_param, max_extra_delay);
                            bool high = state.midi_note_active && (shifted_phase >= extra_delay) && (shifted_phase < (extra_delay + trig_width));
                            val = high ? 2047 : 0;
                            break;
                        }
                        case 3: { // BURST
                            uint32_t num_pulses = 1 + (((uint32_t)local_param * 7) >> 8);
                            uint32_t W = segment_widths_ram[num_pulses - 1];
                            uint32_t k = mul_u32_u32_high(shifted_phase, num_pulses);
                            uint32_t phase_within_segment = shifted_phase - k * W;
                            uint32_t pulse_width = W >> 1;
                            uint32_t min_trig_phase = 288 * phase_inc;
                            if (pulse_width < min_trig_phase) {
                                pulse_width = min_trig_phase;
                            }
                            bool high = state.midi_note_active && (phase_within_segment < pulse_width);
                            val = high ? 2047 : 0;
                            break;
                        }
                        case 4: { // RANDOM
                            val = state.midi_note_active ? ((state.midi_velocity * 2047) / 127) : 0;
                            break;
                        }
                        case 5: { // UTILITY
                            if (local_param >= 192) {
                                val = (!run_gate_paused) ? 2047 : 0;
                            } else {
                                uint32_t N = 1;
                                if (local_param >= 128)      N = 24;
                                else if (local_param >= 64)  N = 4;

                                uint32_t period_samples = safe_div_u32(1440000, cached_params.bpm * N);
                                uint32_t pulse_width = 240; // 10ms
                                if (pulse_width > period_samples / 2) {
                                    pulse_width = period_samples / 2;
                                }
                                if (pulse_width < 1) {
                                    pulse_width = 1;
                                }

                                uint32_t step = 0x100000000ULL / N;
                                uint32_t phase_mod = master_phase_ % step;
                                bool high = (!run_gate_paused) && (phase_mod < base_inc * pulse_width);
                                val = high ? 2047 : 0;
                            }
                            break;
                        }
                    }
                }
            }
        } else {
            uint32_t base_phase = state.phase;
            if (i < 4 && ch_config.euclidean_steps == 0) {
                uint32_t offset = (uint32_t)ch_config.euclidean_fills * 16843009U;
                if (is_one_shot) {
                    if (state.phase < offset) {
                        base_phase = 0;
                    } else {
                        base_phase = state.phase - offset;
                    }
                } else {
                    base_phase = state.phase - offset;
                }
            }
            uint32_t rendering_phase = base_phase;
            bool can_humanize = (ch_config.wave_shape == WAVE_GATE || 
                                 ch_config.wave_shape == WAVE_RATCHET || 
                                 ch_config.wave_shape == WAVE_ENVELOPE || 
                                 ch_config.wave_shape == WAVE_RANDOM_SH || 
                                 ch_config.wave_shape == WAVE_RANDOM_SM ||
                                 i >= 4);
            if (can_humanize) {
                if (state.phase < (uint32_t)state.jitter_phase) {
                    humanize_muted = true;
                } else {
                    // Jitter delay just expired or never existed: latch any pending S&H voltage
                    // exactly here so CV and gate edge are sample-accurate.
                    if (state.rand_pending) {
                        state.rand_current = state.rand_pending_val;
                        state.rand_pending  = false;
                    }
                    rendering_phase = base_phase - state.jitter_phase;
                }
            }

            if (i < 4) {
                switch (ch_config.wave_shape) {
                    case WAVE_GATE: {
                        uint32_t threshold = (uint32_t)local_param << 24;
                        if (local_param > 0) {
                            uint32_t min_trig_phase = 288 * phase_inc;
                            if (threshold < min_trig_phase) {
                                threshold = min_trig_phase;
                            }
                            if (local_param < 255) {
                                uint32_t max_trig_phase = 0xFFFFFFFF;
                                if (10 * phase_inc < 0xFFFFFFFF) {
                                    max_trig_phase = 0xFFFFFFFF - 10 * phase_inc;
                                }
                                if (threshold > max_trig_phase) {
                                    threshold = max_trig_phase;
                                }
                            }
                        }
                        bool high = state.step_active && (rendering_phase < threshold);
                        val = high ? 2047 : 0;
                        break;
                    }
                    case WAVE_RATCHET: {
                        // Ratchet: a double trigger where local_param controls the spacing between them
                        // Fixed trigger width: 10% of the step cycle
                        uint32_t trig_width = 429496730; // ~10% of 0xFFFFFFFF
                        uint32_t min_trig_phase = 288 * phase_inc;
                        if (trig_width < min_trig_phase) {
                            trig_width = min_trig_phase;
                        }
                        if (trig_width > 1932735282U) { // Cap at 45% of 0xFFFFFFFF to avoid underflow
                            trig_width = 1932735282U;
                        }
                        // Scale local_param to determine the start phase of the second trigger
                        // Spacing can range from right after the first trigger (trig_width) to near the end of the step
                        uint32_t min_delay = trig_width;
                        uint32_t max_delay = 0xFFFFFFFF - trig_width;
                        uint32_t delay = min_delay + mul_u32_u32_shift8(local_param, max_delay - min_delay);

                        bool trig1 = rendering_phase < trig_width;
                        bool trig2 = rendering_phase >= delay && rendering_phase < (delay + trig_width);
                        bool high = state.step_active && (trig1 || trig2);
                        val = high ? 2047 : 0;
                        break;
                    }
                    case WAVE_SINE: {
                        uint32_t offset = (uint32_t)local_param << 24;
                        int16_t sine_val = interpolate_sine(rendering_phase + offset);
                        val = state.step_active ? sine_val : 0;
                        break;
                    }
                    case WAVE_TRIANGLE: {
                        uint32_t clamped = local_param;
                        if (clamped < 2) clamped = 2;
                        if (clamped > 253) clamped = 253;
                        uint32_t skew_point = clamped << 24;
                        uint32_t inv_skew = inv_skew_lut[clamped];
                        uint32_t inv_rem = inv_rem_lut[clamped];

                        if (rendering_phase < skew_point) {
                            val = -2048 + (int16_t)mul_u32_u32_high(rendering_phase, inv_skew);
                        } else {
                            uint32_t rem_phase = rendering_phase - skew_point;
                            val = 2047 - (int16_t)mul_u32_u32_high(rem_phase, inv_rem);
                        }
                        val = state.step_active ? val : 0;
                        break;
                    }
                    case WAVE_SAW_UP: {
                        int32_t lin = -2048 + (int16_t)mul_u32_u32_high(rendering_phase, 4095);
                        int32_t exp = -2048 + (4095 - interpolate_exp(rendering_phase));
                        int32_t log = -2048 + interpolate_exp(0xFFFFFFFF - rendering_phase);
                        
                        if (local_param == 128) {
                            val = lin;
                        } else if (local_param < 128) {
                            // Blend log ↔ linear
                            val = log + (((lin - log) * local_param) >> 7);
                        } else {
                            // Blend linear ↔ exp
                            val = lin + (((exp - lin) * (local_param - 128)) >> 7);
                        }
                        val = state.step_active ? val : 0;
                        break;
                    }
                    case WAVE_SAW_DOWN: {
                        int32_t lin = 2047 - (int16_t)mul_u32_u32_high(rendering_phase, 4095);
                        int32_t exp = 2047 - (4095 - interpolate_exp(rendering_phase));
                        int32_t log = 2047 - interpolate_exp(0xFFFFFFFF - rendering_phase);

                        if (local_param == 128) {
                            val = lin;
                        } else if (local_param < 128) {
                            // Blend log ↔ linear
                            val = log + (((lin - log) * local_param) >> 7);
                        } else {
                            // Blend linear ↔ exp
                            val = lin + (((exp - lin) * (local_param - 128)) >> 7);
                        }
                        val = state.step_active ? val : 0;
                        break;
                    }
                    case WAVE_ENVELOPE: {
                        // Prevent wrapping (decay stops at 0). env_multiplier_lut modulates the decay time.
                        // For WAVE_ENVELOPE, we want the trigger timing to be swung, but the decay phase increment
                        // to remain constant so the envelope decay duration doesn't alternate between steps.
                        uint32_t env_phase = rendering_phase;
                        if (cached_params.global_swing > 0 && local_modifier >= 14) {
                            uint32_t swing_mult = (state.loop_step_count % 2 == 0) ? cached_params.swing_mult_even : cached_params.swing_mult_odd;
                            if (swing_mult > 0) {
                                uint64_t calc = ((uint64_t)rendering_phase << 16) / swing_mult;
                                env_phase = (calc > 0xFFFFFFFFU) ? 0xFFFFFFFFU : (uint32_t)calc;
                            }
                        }
                        uint32_t mult_phase = mul_u32_u16_shift8_clamp(env_phase, env_multiplier_lut[local_param]);
                        uint16_t env_val = interpolate_exp(mult_phase);
                        val = (int16_t)(env_val >> 1); // unipolar envelope 0 to 2047
                        val = state.step_active ? val : 0;
                        break;
                    }
                    case WAVE_RANDOM_SH: {
                        uint32_t clamped = local_param;
                        if (clamped < 2) clamped = 2;
                        uint32_t slew_duration = clamped << 24;
                        uint32_t inv_slew = inv_slew_lut[clamped];

                        int16_t q_current = scale_and_quantize_val(state.rand_current, ch_config, current_scale);
                        int16_t q_next = scale_and_quantize_val(state.rand_next, ch_config, current_scale);

                        if (rendering_phase < slew_duration) {
                            uint32_t frac = mul_u32_u32_high(rendering_phase, inv_slew);
                            val = q_current + (((q_next - q_current) * (int32_t)frac) >> 16);
                        } else {
                            val = q_next;
                        }
                        val = state.step_active ? val : 0;
                        break;
                    }
                    case WAVE_RANDOM_SM: {
                        uint32_t clamped = local_param;
                        if (clamped < 2) clamped = 2;
                        uint32_t slew_duration = clamped << 24;
                        uint32_t inv_slew = inv_slew_lut[clamped];

                        int16_t q_current = scale_and_quantize_val(state.rand_current, ch_config, current_scale);
                        int16_t q_next = scale_and_quantize_val(state.rand_next, ch_config, current_scale);

                        if (rendering_phase < slew_duration) {
                            uint32_t frac = mul_u32_u32_high(rendering_phase, inv_slew);
                            // Map frac (0..65536) to a half-sine cycle (0x40000000..0xC0000000) for smooth cosine curve
                            uint32_t table_phase = (frac << 15) + 0x40000000;
                            int16_t sine_val = interpolate_sine(table_phase);
                            uint32_t interp_frac = 2048 - sine_val; // ranges from 1 to 4095 (Q12)
                            val = q_current + (((q_next - q_current) * (int32_t)interp_frac) >> 12);
                        } else {
                            val = q_next;
                        }
                        val = state.step_active ? val : 0;
                        break;
                    }
                    case WAVE_CV_DELAY: {
                        uint32_t mask = delay_buffer_masks[i];
                        uint32_t max_samples = mask + 1;
                        uint32_t delay_samples = 0;
                        uint32_t step_period_samples = safe_div_u32(0xFFFFFFFFU, phase_inc);
                        if (step_period_samples > (2 * SAMPLE_RATE)) step_period_samples = (2 * SAMPLE_RATE);
                        if (step_period_samples < (SAMPLE_RATE / 100)) step_period_samples = (SAMPLE_RATE / 100);

                        if (local_param < 128) {
                            delay_samples = 10 + (((uint32_t)local_param * (max_samples - 20)) >> 7);
                        } else {
                            uint32_t sync_idx = (local_param - 128) >> 4;
                            switch (sync_idx) {
                                case 0: delay_samples = step_period_samples / 16; break;
                                case 1: delay_samples = step_period_samples / 8; break;
                                case 2: delay_samples = step_period_samples / 4; break;
                                case 3: delay_samples = step_period_samples / 2; break;
                                case 4: delay_samples = step_period_samples; break;
                                case 5: delay_samples = (step_period_samples * 3) / 2; break;
                                case 6: delay_samples = step_period_samples * 2; break;
                                default: delay_samples = step_period_samples * 4; break;
                            }
                            if (delay_samples > max_samples - 10) {
                                  delay_samples = max_samples - 10;
                            }
                            if (delay_samples < 10) {
                                  delay_samples = 10;
                            }
                        }

                        int32_t input_val = 0;
                        uint32_t actual_delay = delay_samples;

                        if (input_connected) {
                            input_val = channel_cv;
                        } else {
                            if (state.step_triggered) {
                                if (state.step_active) {
                                    state.delay_impulse_counter = (SAMPLE_RATE / 1000); // 1ms pulse width
                                    state.bounce_decay = 2047;
                                    uint32_t denom = (step_period_samples * ((uint32_t)local_prob + 10)) / 60;
                                    if (denom < 1) denom = 1;
                                    uint32_t sub = 262144 / denom;
                                    if (sub > 65536) sub = 65536;
                                    state.bounce_decay_coeff = 65536 - sub;
                                }
                                state.step_triggered = false;
                            }

                            // Decay bounce envelope on every sample
                            state.bounce_decay = ((int32_t)state.bounce_decay * (int32_t)state.bounce_decay_coeff) >> 16;

                            if (state.delay_impulse_counter > 0) {
                                input_val = 2047;
                                if (state.delay_impulse_counter > 1) {
                                    state.delay_impulse_counter -= 1;
                                } else {
                                    state.delay_impulse_counter = 0;
                                }
                            } else {
                                input_val = 0;
                            }

                            uint32_t min_delay = (SAMPLE_RATE * 25) / 10000; // 2.5ms
                            if (delay_samples > min_delay) {
                                actual_delay = min_delay + (((delay_samples - min_delay) * state.bounce_decay) / 2047);
                            }
                        }

                        uint32_t write_ptr = state.delay_write_ptr;
                        uint32_t read_ptr = (write_ptr - actual_delay) & mask;
                        int16_t delayed_val = delay_buffers[i][read_ptr];

                        int32_t fb_gain = ((int32_t)local_prob * 15) >> 4; // 0 to 93
                        int32_t next_write_val = input_val + ((delayed_val * fb_gain) / 100);
                        
                        if (next_write_val > 2047) next_write_val = 2047;
                        if (next_write_val < -2048) next_write_val = -2048;

                        delay_buffers[i][write_ptr] = (int16_t)next_write_val;
                        state.delay_write_ptr = (write_ptr + 1) & mask;

                        val = (int16_t)delayed_val;
                        break;
                    }
                    case WAVE_TRAPEZOID: {
                        uint32_t clamped = local_param;
                        if (clamped < 2) clamped = 2;
                        if (clamped > 253) clamped = 253;
                        uint32_t F = (clamped * 0x40000000ULL) >> 8;
                        uint32_t R = 0x80000000ULL - F;
                        if (rendering_phase < R) {
                            val = -2048 + (int16_t)mul_u32_u32_high(rendering_phase, state.inv_skew);
                        } else if (rendering_phase < R + F) {
                            val = 2047;
                        } else if (rendering_phase < 2 * R + F) {
                            uint32_t rem = rendering_phase - (R + F);
                            val = 2047 - (int16_t)mul_u32_u32_high(rem, state.inv_skew);
                        } else {
                            val = -2048;
                        }
                        val = state.step_active ? val : 0;
                        break;
                    }
                    case WAVE_HUMP: {
                        uint32_t clamped = local_param;
                        if (clamped < 2) clamped = 2;
                        if (clamped > 253) clamped = 253;
                        uint32_t skew_point = clamped << 24;
                        if (rendering_phase < skew_point) {
                            uint32_t frac = mul_u32_u32_high(rendering_phase, state.inv_skew);
                            if (frac > 4095) frac = 4095;
                            val = interpolate_sine(frac << 18);
                        } else {
                            uint32_t rem_phase = rendering_phase - skew_point;
                            uint32_t frac = mul_u32_u32_high(rem_phase, state.inv_rem);
                            if (frac > 4095) frac = 4095;
                            val = interpolate_sine(0x40000000U + (frac << 18));
                        }
                        val = state.step_active ? val : 0;
                        break;
                    }
                    case WAVE_LOG_ENVELOPE: {
                        uint32_t env_phase = rendering_phase;
                        if (cached_params.global_swing > 0 && local_modifier >= 14) {
                            uint32_t swing_mult = (state.loop_step_count % 2 == 0) ? cached_params.swing_mult_even : cached_params.swing_mult_odd;
                            if (swing_mult > 0) {
                                uint64_t calc = ((uint64_t)rendering_phase << 16) / swing_mult;
                                env_phase = (calc > 0xFFFFFFFFU) ? 0xFFFFFFFFU : (uint32_t)calc;
                            }
                        }
                        uint32_t mult_phase = mul_u32_u16_shift8_clamp(env_phase, env_multiplier_lut[local_param]);
                        uint16_t env_val = 4095 - interpolate_exp(0xFFFFFFFFU - mult_phase);
                        val = (int16_t)(env_val >> 1); // unipolar envelope 0 to 2047
                        val = state.step_active ? val : 0;
                        break;
                    }
                    case WAVE_MATH: {
                        // 1. Compute Carrier
                        int16_t carrier = 0;
                        if (input_connected) {
                            carrier = channel_cv;
                        } else {
                            // Standard 50% skew triangle LFO (inv_skew/rem_lut[128])
                            uint32_t skew_point = 0x80000000U;
                            if (rendering_phase < skew_point) {
                                carrier = -2048 + (int16_t)mul_u32_u32_high(rendering_phase, inv_skew_lut[128]);
                            } else {
                                uint32_t rem_phase = rendering_phase - skew_point;
                                carrier = 2047 - (int16_t)mul_u32_u32_high(rem_phase, inv_rem_lut[128]);
                            }
                        }

                        // 2. Retrieve Modulator
                        int16_t modulator = 0;
                        uint8_t op = local_param / 32;       // 0..7 discrete operations
                        if (trigger_src > 0) {
                            modulator = states_[trigger_src - 1].last_value;
                        } else {
                            // Neutral defaults for stand-alone mode
                            if (op == 0 || op == 1) { // Sum, Difference
                                modulator = 0;
                            } else if (op == 2 || op == 4 || op == 5) { // Min, Mult, AND
                                modulator = 2047;
                            } else { // Max, OR, XOR
                                modulator = -2048;
                            }
                        }

                        // 3. Perform Operation
                        int32_t result = carrier;
                        switch (op) {
                            case 0: // Mix/Sum
                                result = (int32_t)carrier + (int32_t)modulator;
                                break;
                            case 1: // Difference
                                result = (int32_t)carrier - (int32_t)modulator;
                                break;
                            case 2: // Minimum
                                result = (carrier < modulator) ? carrier : modulator;
                                break;
                            case 3: // Maximum
                                result = (carrier > modulator) ? carrier : modulator;
                                break;
                            case 4: // Multiplication (VCA/RingMod)
                                result = ((int32_t)carrier * (int32_t)modulator) >> 11;
                                break;
                            case 5: // AND Logic
                                result = (carrier > 0 && modulator > 0) ? 2047 : -2048;
                                break;
                            case 6: // OR Logic
                                result = (carrier > 0 || modulator > 0) ? 2047 : -2048;
                                break;
                            case 7: // XOR Logic
                                result = ((carrier > 0) ^ (modulator > 0)) ? 2047 : -2048;
                                break;
                        }

                        if (result > 2047) result = 2047;
                        if (result < -2048) result = -2048;
                        val = (int16_t)result;
                        val = state.step_active ? val : 0;
                        break;
                    }
                }
            } else {
                // Specialized Digital Pulse shapes for Channels 4 and 5
                // level parameter (0-200) acts as Phase Offset (Pulse Delay)
                // where 0 = 0% delay, 200 = 100% delay (internally scaled 0-100 by division by 2)
                uint8_t scaled_delay = ch_config.level / 2;
                uint32_t delay_phase = (uint32_t)scaled_delay * 42949672U;
                if (rendering_phase < delay_phase) {
                    val = 0;
                } else {
                    uint32_t shifted_phase = rendering_phase - delay_phase;
                    switch (ch_config.wave_shape) {
                        case 0: { // GATE
                            uint32_t width_phase = (uint32_t)local_param * 16843009U;
                            if (local_param > 0) {
                                uint32_t min_trig_phase = 288 * phase_inc;
                                if (width_phase < min_trig_phase) {
                                    width_phase = min_trig_phase;
                                }
                                if (local_param < 255) {
                                    uint32_t max_trig_phase = 0xFFFFFFFF;
                                    if (10 * phase_inc < 0xFFFFFFFF) {
                                        max_trig_phase = 0xFFFFFFFF - 10 * phase_inc;
                                    }
                                    if (width_phase > max_trig_phase) {
                                        width_phase = max_trig_phase;
                                    }
                                }
                            }
                            bool high = state.step_active && (shifted_phase < width_phase);
                            val = high ? 2047 : 0;
                            break;
                        }
                        case 1: { // RATCHET
                            uint32_t trig_width = 429496730; // ~10% of 0xFFFFFFFF
                            uint32_t min_trig_phase = 288 * phase_inc;
                            if (trig_width < min_trig_phase) {
                                trig_width = min_trig_phase;
                            }
                            if (trig_width > 1932735282U) { // Cap at 45% of 0xFFFFFFFF to avoid underflow
                                trig_width = 1932735282U;
                            }
                            uint32_t min_delay = trig_width;
                            uint32_t max_delay = 0xFFFFFFFF - trig_width;
                            uint32_t delay = min_delay + mul_u32_u32_shift8(local_param, max_delay - min_delay);

                            bool trig1 = shifted_phase < trig_width;
                            bool trig2 = shifted_phase >= delay && shifted_phase < (delay + trig_width);
                            bool high = state.step_active && (trig1 || trig2);
                            val = high ? 2047 : 0;
                            break;
                        }
                        case 2: { // TRIGGER (DELAY)
                            uint32_t trig_width = 288 * phase_inc;
                            if (trig_width > 3865470565U) { // 90% of 0xFFFFFFFF
                                trig_width = 3865470565U;
                            }
                            uint32_t max_extra_delay = 3865470565U - trig_width;
                            uint32_t extra_delay = mul_u32_u32_shift8(local_param, max_extra_delay);
                            bool high = state.step_active && (shifted_phase >= extra_delay) && (shifted_phase < (extra_delay + trig_width));
                            val = high ? 2047 : 0;
                            break;
                        }
                        case 3: { // BURST
                            uint32_t num_pulses = 1 + (((uint32_t)local_param * 7) >> 8); // 1..8
                            uint32_t W = segment_widths_ram[num_pulses - 1];
                            uint32_t k = mul_u32_u32_high(shifted_phase, num_pulses);
                            uint32_t phase_within_segment = shifted_phase - k * W;
                            uint32_t pulse_width = W >> 1;
                            uint32_t min_trig_phase = 288 * phase_inc;
                            if (pulse_width < min_trig_phase) {
                                  pulse_width = min_trig_phase;
                            }
                            bool high = state.step_active && (phase_within_segment < pulse_width);
                            val = high ? 2047 : 0;
                            break;
                        }
                        case 4: { // RANDOM
                            uint32_t gate_width_scale = ((uint32_t)state.rand_current * local_param) >> 8;
                            uint32_t threshold = gate_width_scale << 24;
                            if (local_param > 0 && state.rand_current > 0) {
                                uint32_t min_trig_phase = 288 * phase_inc;
                                if (threshold < min_trig_phase) {
                                    threshold = min_trig_phase;
                                }
                                uint32_t max_trig_phase = 0xFFFFFFFF;
                                if (10 * phase_inc < 0xFFFFFFFF) {
                                    max_trig_phase = 0xFFFFFFFF - 10 * phase_inc;
                                }
                                if (threshold > max_trig_phase) {
                                    threshold = max_trig_phase;
                                }
                            }
                            bool high = state.step_active && (shifted_phase < threshold);
                            val = high ? 2047 : 0;
                            break;
                        }
                        case 5: { // UTILITY
                            if (local_param >= 192) {
                                val = (!run_gate_paused) ? 2047 : 0;
                            } else {
                                uint32_t N = 1;
                                if (local_param >= 128)      N = 24;
                                else if (local_param >= 64)  N = 4;

                                uint32_t period_samples = safe_div_u32(1440000, cached_params.bpm * N);
                                uint32_t pulse_width = 240; // 10ms
                                if (pulse_width > period_samples / 2) {
                                    pulse_width = period_samples / 2;
                                }
                                if (pulse_width < 1) {
                                    pulse_width = 1;
                                }

                                uint32_t step = 0x100000000ULL / N;
                                uint32_t phase_mod = master_phase_ % step;
                                bool high = (!run_gate_paused) && (phase_mod < base_inc * pulse_width);
                                val = high ? 2047 : 0;
                            }
                            break;
                        }
                        default: {
                            val = 0;
                            break;
                        }
                    }
                }
            }
            if (humanize_muted) {
                val = 0;
            }
        }

        // Apply Level Attenuation first (only for analog channels 0-3; digital outputs 4-5 ignore level scaling for trigger consistency)
        if (i < 4 && ch_config.wave_shape != WAVE_RANDOM_SH && ch_config.wave_shape != WAVE_RANDOM_SM) {
            int32_t signed_level = (int32_t)ch_config.level - 100;
            int32_t abs_scale = signed_level >= 0 ? signed_level : -signed_level;
            bool is_bipolar = (ch_config.wave_shape == WAVE_SINE || 
                               ch_config.wave_shape == WAVE_TRIANGLE || 
                               ch_config.wave_shape == WAVE_SAW_UP || 
                               ch_config.wave_shape == WAVE_SAW_DOWN || 
                               ch_config.wave_shape == WAVE_RANDOM_SH || 
                               ch_config.wave_shape == WAVE_RANDOM_SM || 
                               ch_config.wave_shape == WAVE_CV_DELAY || 
                               ch_config.wave_shape == WAVE_TRAPEZOID);
            if (is_bipolar) {
                if (signed_level < 0) {
                    // Negative level (left side): bipolar mode
                    int32_t temp_level = (int32_t)val * abs_scale;
                    int32_t abs_level = temp_level < 0 ? -temp_level : temp_level;
                    int32_t quotient_level = (abs_level * 5243) >> 19;
                    val = (int16_t)(temp_level < 0 ? -quotient_level : quotient_level);
                } else {
                    // Positive level (right side): unipolar scaling (0V to +6V) scaling up from 0V
                    int32_t uni_val = ((int32_t)val + 2048) >> 1; // 0..2047
                    val = (int16_t)((uni_val * abs_scale * 5243) >> 19);
                }
            } else {
                // Unipolar shape
                int32_t temp_level = (int32_t)val * abs_scale;
                int16_t scaled = (int16_t)((temp_level * 5243) >> 19);
                val = (signed_level >= 0) ? scaled : -scaled;
            }
        }

        // Apply Scale Quantizer (Calibrated for 1V/Octave on a -6V to +6V DAC: 28.44 steps per semitone)
        bool is_quantized = false;
        int q_note = 60;
        if (trigger_src == 7 && ch_config.wave_shape == WAVE_RANDOM_SH) {
            is_quantized = true;
            q_note = state.midi_note_num;
            if (current_scale != SCALE_OFF) {
                q_note = quantize_to_scale(q_note, current_scale);
            }
        } else if (i < 4 && state.step_active && !humanize_muted && current_scale != SCALE_OFF && 
            ch_config.wave_shape != WAVE_GATE && ch_config.wave_shape != WAVE_RATCHET && 
            ch_config.wave_shape != WAVE_RANDOM_SH && ch_config.wave_shape != WAVE_RANDOM_SM) {
            int note = 60 + ((val * 9 + 128) >> 8); // ~28.44 steps per semitone with rounding
            // Only re-run the scale search when the input semitone or scale changed.
            // At a held step this fires once and then caches, saving ~7 comparisons/sample.
            if ((int16_t)note != state.last_q_input_note || current_scale != state.last_q_scale) {
                state.last_q_input_note  = (int16_t)note;
                state.last_q_scale       = current_scale;
                state.last_q_output_note = (int16_t)quantize_to_scale(note, current_scale);
            }
            q_note = state.last_q_output_note;
            int32_t temp_note = (q_note - 60) * 256;
            int32_t abs_note = temp_note < 0 ? -temp_note : temp_note;
            int32_t quotient_note = (abs_note * 58255) >> 19;
            val = (int16_t)(temp_note < 0 ? -quotient_note : quotient_note);
            if (val > 2047) val = 2047;
            if (val < -2048) val = -2048;
            is_quantized = true;
        }
        
        if (is_quantized) {
            if (ch_config.glide == 0) {
                state.current_pitch = (float)q_note;
            } else {
                float tau_samples = (float)ch_config.glide * (float)ch_config.glide * 8.0f;
                float coef = 1.0f / (1.0f + tau_samples);
                state.current_pitch += ((float)q_note - state.current_pitch) * coef;
            }
            // Update val with the smoothed gliding pitch for Audio Out 1 and 2 (channels 0 and 1)
            float temp_note = (state.current_pitch - 60.0f) * 28.444444f;
            int16_t smooth_val = (int16_t)temp_note;
            if (smooth_val > 2047) smooth_val = 2047;
            if (smooth_val < -2048) smooth_val = -2048;
            val = smooth_val;
        }

        state.last_value = val;
        if (val > state.telemetry_max) state.telemetry_max = val;
        if (val < state.telemetry_min) state.telemetry_min = val;

        // Route output value to the correct hardware channel
        if (i == 0) {
            AudioOut1(val);
        } else if (i == 1) {
            AudioOut2(val);
        } else if (i == 2) {
            // Calibrated output scaling using custom calibration mV DAC table
            int32_t mv;
            if (is_quantized) {
                mv = (int32_t)((state.current_pitch - 60.0f) * 83.333333f);
            } else {
                mv = ((int32_t)val * 375) >> 7;
            }
            CVOut1Millivolts(mv);
        } else if (i == 3) {
            // Calibrated output scaling using custom calibration mV DAC table
            int32_t mv;
            if (is_quantized) {
                mv = (int32_t)((state.current_pitch - 60.0f) * 83.333333f);
            } else {
                mv = ((int32_t)val * 375) >> 7;
            }
            CVOut2Millivolts(mv);
        } else if (i == 4) {
            PulseOut1(val > 0);
        } else if (i == 5) {
            PulseOut2(val > 0);
        }
    }

    // Dynamic LED displays (Real-time live output visual feedback)
    if (!ui_led_override_) {
        for (int i = 0; i < 6; i++) {
            // Use absolute amplitude (not raw signed value) so bipolar signals
            // light the LED based on how much signal is going out, not its polarity.
            int32_t abs_brightness = states_[i].last_value;
            bool is_unipolar = (i >= 4) ||
                               (cached_params.channels[i].wave_shape == WAVE_GATE) ||
                               (cached_params.channels[i].wave_shape == WAVE_RATCHET) ||
                               (cached_params.channels[i].wave_shape == WAVE_ENVELOPE);

            if (is_unipolar) {
                if (abs_brightness < 0) abs_brightness = 0;
                if (abs_brightness > 2047) abs_brightness = 2047;
            } else {
                // Bipolar: take absolute value so 0V = dark, ±full = bright
                if (abs_brightness < 0) abs_brightness = -abs_brightness;
                if (abs_brightness > 2047) abs_brightness = 2047;
            }

            // Map 0..2047 amplitude to 0..4095 range for LedBrightness (which applies its own quadratic curve)
            uint16_t led_val = (uint16_t)abs_brightness << 1;

            // Always dim non-active channels on Channel Pages to 10% brightness
            if (active_page_ < 6 && i != active_page_) {
                led_val = (led_val * 6554) >> 16;
            } else if (active_page_ < 6 && i == active_page_) {
                // Active page channel LED: ensure a baseline minimum brightness so it's visible even when output is 0V
                if (led_val < 1500) {
                    led_val = 1500;
                }
            } else if (active_page_ == 6) {
                // Global page: add a static dim baseline glow to all LEDs (no pulsing)
                uint16_t base_glow = 250;
                if (led_val < base_glow) {
                    led_val = base_glow;
                }
            }

            LedBrightness(i, led_val);
        }
    }
}

// ============================================================================
// Core 0: User Interface, Telemetry, and Housekeeping
// ============================================================================

void ClockworksCard::run_core0_ui_loop() {
    // Setup USB Host or Device stack dynamically based on VBUS detection
    sleep_ms(150); // wait for power rails to stabilize
    ComputerCard::USBPowerState_t powerState = USBPowerState();
    g_usb_midi_host = (powerState == ComputerCard::DFP);
    if (g_usb_midi_host) {
        tuh_init(0);
    } else {
        tud_init(0);
    }
    sleep_ms(50);

    // If load_settings() found invalid flash (first boot or firmware update),
    // write defaults to flash now — Core 1 audio is running from RAM so
    // the flash erase is invisible to it.
    if (config_dirty_ && last_change_time_ms_ == 0) {
        save_settings_to_flash();
        config_dirty_ = false;
    }

    uint32_t last_tick_time        = to_ms_since_boot(get_absolute_time());
    uint32_t last_dirty_sync_ms    = 0;
    uint32_t last_periodic_sync_ms = 0;

    while (1) {
        if (g_usb_midi_host) {
            tuh_task();
        } else {
            tud_task();
            process_usb_midi_device();
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_tick_time >= 1) {
            last_tick_time = now;
            tick_ui_once();
            process_incoming_serial();

            // Sync while knobs are being turned (every 80ms)
            if (config_dirty_ && (now - last_dirty_sync_ms >= 80)) {
                last_dirty_sync_ms    = now;
                last_periodic_sync_ms = now;
                send_sync_packet();
            }

            // Autosave 1.5s after last change
            if (config_dirty_ && (now - last_change_time_ms_ > 1500)) {
                save_settings_to_flash();
                config_dirty_ = false;
                send_sync_packet();
                last_dirty_sync_ms    = now;
                last_periodic_sync_ms = now;
            }

            // Unconditional keepalive every 2s — WebSerial never sets DTR so
            // tud_cdc_connected() is always false; don't gate on it.
            if (now - last_periodic_sync_ms >= 2000) {
                last_periodic_sync_ms = now;
                send_sync_packet();
            }
        }
    }
}

void ClockworksCard::tick_ui_once() {
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    static bool initialized = false;
    if (!initialized) {
        if (now_ms > 1000) {
            lock_knobs();
            initialized = true;
        }
        return;
    }

    // Tempo Flywheel Fallback: seamlessly sync internal BPM to external clock when it stops
    static bool was_midi_active = false;
    bool current_midi_active = (now_ms - last_midi_clock_time_ms_ < 2000);

    static bool was_ext_active = false;
    bool current_ext_active = external_clock_active_;

    if (was_midi_active && !current_midi_active) {
        if (midi_period_samples_ > 0) {
            uint32_t midi_bpm = safe_div_u32(SAMPLES_PER_MINUTE, midi_period_samples_ * 24);
            if (midi_bpm >= 30 && midi_bpm <= 300) {
                settings_.bpm = (uint16_t)midi_bpm;
                config_dirty_ = true;
                last_change_time_ms_ = now_ms;
                sync_params_to_core1();
            }
        }
    } else if (was_ext_active && !current_ext_active) {
        if (ext_period_samples_ > 0 && settings_.ext_ppqn > 0) {
            uint32_t ext_bpm = safe_div_u32(SAMPLES_PER_MINUTE, ext_period_samples_ * settings_.ext_ppqn);
            if (ext_bpm >= 30 && ext_bpm <= 300) {
                settings_.bpm = (uint16_t)ext_bpm;
                config_dirty_ = true;
                last_change_time_ms_ = now_ms;
                sync_params_to_core1();
            }
        }
    }
    was_midi_active = current_midi_active;
    was_ext_active = current_ext_active;

    if (edit_feedback_timer_ > 0) {
        edit_feedback_timer_--;
    }

    if (division_feedback_timer_ > 0) {
        division_feedback_timer_--;
        if (division_feedback_timer_ == 0) {
            // Commit pending clock modifier when display timer expires
            if (pending_main_.active && pending_main_.type == PENDING_MODIFIER) {
                commit_pending(pending_main_);
            }
        }
    }

    if (parameter_feedback_timer_ > 0) {
        parameter_feedback_timer_--;
        if (parameter_feedback_timer_ == 0) {
            // Commit all non-modifier pending values when display timer expires
            if (pending_main_.active && pending_main_.type != PENDING_MODIFIER) {
                commit_pending(pending_main_);
            }
            commit_pending(pending_x_);
            commit_pending(pending_y_);
        }
    }

    // Page-change blink timer tick
    if (page_blink_timer_ > 0) page_blink_timer_--;

    // Switch Debounce / Momentary Hold & Multi-tap state machine
    Switch sw = SwitchVal();
    static bool prev_sw_down = false;
    static uint32_t down_press_time = 0;
    static bool param_changed_during_press = false;

    static bool prev_sw_up = false;
    static uint32_t up_press_time = 0;

    // -----------------------------------------------------------
    // Switch UP Flick Gesture for Play/Pause (on every page)
    // -----------------------------------------------------------
    if (sw == Switch::Up && !prev_sw_up) {
        up_press_time = now_ms;
        prev_sw_up = true;
    } else if (sw != Switch::Up && prev_sw_up) {
        prev_sw_up = false;
        uint32_t duration = now_ms - up_press_time;
        if (!preset_menu_active_) {
            if (duration < 400 && !param_changed_during_press) {
                paused_ = !paused_;
                manual_reset_counter_++;
            }
        }
    }

    if (sw == Switch::Down && !prev_sw_down) {
        down_press_time = now_ms;
        prev_sw_down = true;
        is_down_held_ = false;
        manual_reset_triggered_ = false;
        param_changed_during_press = false;
    } else if (sw != Switch::Down && prev_sw_down) {
        prev_sw_down = false;
        uint32_t duration = now_ms - down_press_time;

        if (preset_menu_active_) {
            // Execute the armed action on release
            if (armed_preset_action_ == 1) {
                if (selected_preset_ <= 5) {
                    load_preset(selected_preset_);
                }
            } else if (armed_preset_action_ == 2) {
                if (selected_preset_ <= 5) {
                    save_preset(selected_preset_);
                    edit_feedback_timer_ = 500; // brief flash confirmation
                } else if (selected_preset_ == 0xFF) {
                    // RESET zone: reset to defaults
                    apply_default_settings();
                    save_settings_to_flash();
                    sync_params_to_core1();
                    lock_knobs();
                    manual_reset_counter_++;
                    edit_feedback_timer_ = 1000;
                    send_sync_packet();
                }
            }
            preset_menu_active_ = false;
            armed_preset_action_ = 0;
            is_down_held_ = false;
            lock_knobs();
        } else if (!is_down_held_ && !param_changed_during_press) {
            uint8_t old_page = active_page_;
            if (duration < 300) {
                // Short tap: cycle forward
                active_page_ = (active_page_ + 1) % 7;
                if (active_page_ != old_page) page_blink_timer_ = 250;
                lock_knobs();
            } else if (duration >= 300 && duration < 800) {
                // Medium tap: cycle backward
                active_page_ = (active_page_ + 6) % 7;
                if (active_page_ != old_page) page_blink_timer_ = 250;
                lock_knobs();
            }
        }
        is_down_held_ = false;
        manual_reset_triggered_ = false;
    }

    if (sw == Switch::Down && prev_sw_down) {
        if (now_ms - down_press_time >= 800) { // Hold threshold set to 800ms
            if (!is_down_held_) {
                is_down_held_ = true;
                lock_knobs();
                if (active_page_ == 6) {
                    // Enter preset menu instead of triggering a clock reset
                    preset_menu_active_ = true;
                    selected_preset_ = 0;
                }
            }
        }
    }

    // Toggle switch position check (Tier transitions)
    enum class UiTier {
        Performance,
        Pattern,
        Advanced
    };
    UiTier current_tier = UiTier::Performance;
    if (is_down_held_) {
        current_tier = UiTier::Advanced;
    } else if (sw == Switch::Up) {
        current_tier = UiTier::Pattern;
    }
    static UiTier last_tier = UiTier::Performance;
    if (current_tier != last_tier) {
        last_tier = current_tier;
        lock_knobs();
        param_changed_during_press = false; // Reset changed flag on tier change
    }

    // Knob polling with Pickup / Latching
    int32_t raw_main = KnobVal(Knob::Main); // 0 to 4095
    int32_t raw_x = KnobVal(Knob::X);
    int32_t raw_y = KnobVal(Knob::Y);

    int32_t main_val = raw_main;
    int32_t x_val = raw_x;
    int32_t y_val = raw_y;

    bool main_active = false;
    bool x_active = false;
    bool y_active = false;

    if (active_page_ < 6) { // Channel pages
        ChannelConfig &ch = settings_.channels[active_page_];

        if (is_down_held_) {
            // ==========================================
            // Advanced Tier (Holding DOWN momentary)
            // ==========================================
            // Main knob: Loop Length or Clock Modifier (for WAVE_CV_DELAY)
            if (ch.wave_shape == WAVE_CV_DELAY) {
                // Main knob: Clock Modifier (0 to 33) or Trigger Src (34 to 39) or MIDI (40) - deferred
                int32_t target_mod = ch.get_clock_modifier();
                if (ch.get_trigger_source() > 0) {
                    target_mod = 33 + ch.get_trigger_source();
                }
                main_active = lock_main_.update(raw_main, target_mod * 102, main_val);
                if (main_active) {
                    int mod = (main_val * 41) >> 12;
                    if (mod < 0) mod = 0;
                    if (mod > 40) mod = 40;
                    if (pending_main_.active && pending_main_.type != PENDING_MODIFIER)
                        commit_pending(pending_main_);
                    if (mod != (int)(pending_main_.active ? pending_main_.value : target_mod)) {
                        uint8_t real_val = mod;
                        if (mod == 40) {
                            real_val = last_midi_channel_[active_page_];
                        }
                        pending_main_ = {true, (uint8_t)active_page_, PENDING_MODIFIER, real_val};
                        division_feedback_timer_ = 1200;
                        division_feedback_value_ = real_val;
                        send_param_update_sysex(active_page_, 0, real_val);
                    }
                }
            } else {
                // Main knob: Loop Length (curated list)
                static const uint8_t ALLOWED_LOOPS[] = {
                    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                    18, 20, 24, 28, 30, 32, 40, 48, 56, 60, 64
                };
                static const size_t NUM_ALLOWED_LOOPS = sizeof(ALLOWED_LOOPS) / sizeof(ALLOWED_LOOPS[0]);

                int current_idx = 0;
                int min_diff = 999;
                for (size_t k = 0; k < NUM_ALLOWED_LOOPS; k++) {
                    int diff = abs((int)ALLOWED_LOOPS[k] - (int)ch.loop_length);
                    if (diff < min_diff) {
                        min_diff = diff;
                        current_idx = k;
                    }
                }
                int32_t target_val = (current_idx * 4095) / (NUM_ALLOWED_LOOPS - 1);
                main_active = lock_main_.update(raw_main, target_val, main_val);
                if (main_active) {
                    int idx = (main_val * NUM_ALLOWED_LOOPS) >> 12;
                    if (idx < 0) idx = 0;
                    if (idx >= (int)NUM_ALLOWED_LOOPS) idx = NUM_ALLOWED_LOOPS - 1;
                    uint8_t loop = ALLOWED_LOOPS[idx];
                    if (pending_main_.active && pending_main_.type != PENDING_LOOP)
                        commit_pending(pending_main_);
                    if (loop != (int)(pending_main_.active ? pending_main_.value : ch.loop_length)) {
                        pending_main_ = {true, (uint8_t)active_page_, PENDING_LOOP, (uint8_t)loop};
                        parameter_feedback_timer_ = 1200;
                        parameter_feedback_value_ = loop;
                        parameter_feedback_mode_  = FEEDBACK_BINARY;
                        parameter_feedback_signed_val_ = 0;
                        send_param_update_sysex(active_page_, 7, loop);
                    }
                }
            }

            // X knob: Probability or Loop Length (for WAVE_CV_DELAY)
            if (ch.wave_shape == WAVE_CV_DELAY) {
                // X knob: Loop Length (curated list) - deferred
                static const uint8_t ALLOWED_LOOPS[] = {
                    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                    18, 20, 24, 28, 30, 32, 40, 48, 56, 60, 64
                };
                static const size_t NUM_ALLOWED_LOOPS = sizeof(ALLOWED_LOOPS) / sizeof(ALLOWED_LOOPS[0]);

                int current_idx = 0;
                int min_diff = 999;
                for (size_t k = 0; k < NUM_ALLOWED_LOOPS; k++) {
                    int diff = abs((int)ALLOWED_LOOPS[k] - (int)ch.loop_length);
                    if (diff < min_diff) {
                        min_diff = diff;
                        current_idx = k;
                    }
                }
                int32_t target_val = (current_idx * 4095) / (NUM_ALLOWED_LOOPS - 1);
                x_active = lock_x_.update(raw_x, target_val, x_val);
                if (x_active) {
                    int idx = (x_val * NUM_ALLOWED_LOOPS) >> 12;
                    if (idx < 0) idx = 0;
                    if (idx >= (int)NUM_ALLOWED_LOOPS) idx = NUM_ALLOWED_LOOPS - 1;
                    uint8_t loop = ALLOWED_LOOPS[idx];
                    if (pending_x_.active && pending_x_.type != PENDING_LOOP)
                        commit_pending(pending_x_);
                    if (loop != (int)(pending_x_.active ? pending_x_.value : ch.loop_length)) {
                        pending_x_ = {true, (uint8_t)active_page_, PENDING_LOOP, (uint8_t)loop};
                        parameter_feedback_timer_ = 1200;
                        parameter_feedback_value_ = loop;
                        parameter_feedback_mode_  = FEEDBACK_BINARY;
                        parameter_feedback_signed_val_ = 0;
                        send_param_update_sysex(active_page_, 7, loop);
                    }
                }
            } else {
                // X knob: Probability (0 to 100%)
                int32_t target_prob = ch.probability;
                x_active = lock_x_.update(raw_x, target_prob * 40, x_val);
                if (x_active) {
                    int prob = (x_val * 100) >> 12;
                    if (prob < 0) prob = 0;
                    if (prob > 100) prob = 100;
                    if (ch.probability != prob) {
                        ch.probability = prob;
                        config_dirty_ = true;
                        last_change_time_ms_ = now_ms;
                        parameter_feedback_timer_ = 1200;
                        parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                        parameter_feedback_value_ = (uint8_t)((prob * 63) / 100);
                        parameter_feedback_signed_val_ = 0;
                        send_param_update_sysex(active_page_, 5, prob);
                    }
                }
            }

            // Y knob: Pitch Quantizer Scale or Euclidean Offset (0 to steps - 1)
            if (ch.euclidean_steps > 0) {
                int32_t max_offset = ch.euclidean_steps - 1;
                if (max_offset > 0) {
                    int32_t target_offset = ch.get_euclidean_offset();
                    if (target_offset > max_offset) target_offset = max_offset;
                    y_active = lock_y_.update(raw_y, target_offset * (4095 / max_offset), y_val);
                    if (y_active) {
                        int offset = (y_val * (max_offset + 1)) >> 12;
                        if (offset < 0) offset = 0;
                        if (offset > max_offset) offset = max_offset;
                        if (pending_y_.active && pending_y_.type != PENDING_OFFSET)
                            commit_pending(pending_y_);
                        if (offset != (int)(pending_y_.active ? pending_y_.value : (uint8_t)ch.get_euclidean_offset())) {
                            pending_y_ = {true, (uint8_t)active_page_, PENDING_OFFSET, (uint8_t)offset};
                            parameter_feedback_timer_ = 1200;
                            parameter_feedback_value_ = offset;
                            parameter_feedback_mode_  = FEEDBACK_BINARY;
                            parameter_feedback_signed_val_ = 0;
                            send_param_update_sysex(active_page_, 6, (offset << 4) | ch.get_quantizer_scale());
                        }
                    }
                } else {
                    if (ch.get_euclidean_offset() != 0) {
                        ch.set_euclidean_offset(0);
                        config_dirty_ = true;
                    }
                }
            } else {
                int32_t target_scale = ch.get_quantizer_scale();
                y_active = lock_y_.update(raw_y, target_scale * (4095 / (SCALE_NUM_SCALES - 1)), y_val);
                if (y_active) {
                    int scale = (y_val * SCALE_NUM_SCALES) >> 12;
                    if (scale < 0) scale = 0;
                    if (scale >= SCALE_NUM_SCALES) scale = SCALE_NUM_SCALES - 1;
                    if (pending_y_.active && pending_y_.type != PENDING_SCALE)
                        commit_pending(pending_y_);
                    if (scale != (int)(pending_y_.active ? pending_y_.value : (uint8_t)ch.get_quantizer_scale())) {
                        pending_y_ = {true, (uint8_t)active_page_, PENDING_SCALE, (uint8_t)scale};
                        parameter_feedback_timer_ = 1200;
                        parameter_feedback_value_ = scale;
                        parameter_feedback_mode_  = FEEDBACK_BINARY;
                        parameter_feedback_signed_val_ = 0;
                        send_param_update_sysex(active_page_, 6, (ch.get_euclidean_offset() << 4) | scale);
                    }
                }
            }
        } else if (sw == Switch::Up) {
            // ==========================================
            // Sound Shaping Tier (Switch is UP)
            // ==========================================
            // Main knob: Waveform Shape (0 to 12 or 0 to 5)
            int32_t target_shape = ch.wave_shape;
            int32_t shape_ref = (active_page_ >= 4) ? (target_shape * 682) : (target_shape * 315);
            main_active = lock_main_.update(raw_main, shape_ref, main_val);
            if (main_active) {
                int shape;
                if (active_page_ >= 4) {
                    shape = (main_val * 6) >> 12;
                    if (shape < 0) shape = 0;
                    if (shape >= 6) shape = 5;
                } else {
                    shape = (main_val * WAVE_NUM_SHAPES) >> 12;
                    if (shape < 0) shape = 0;
                    if (shape >= WAVE_NUM_SHAPES) shape = WAVE_NUM_SHAPES - 1;
                }
                if (pending_main_.active && pending_main_.type != PENDING_SHAPE)
                    commit_pending(pending_main_);
                if (shape != (int)(pending_main_.active ? pending_main_.value : ch.wave_shape)) {
                    pending_main_ = {true, (uint8_t)active_page_, PENDING_SHAPE, (uint8_t)shape};
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_value_ = shape;
                    parameter_feedback_mode_  = FEEDBACK_BINARY;
                    parameter_feedback_signed_val_ = 0;
                    send_param_update_sysex(active_page_, 3, shape);
                }
            }

            // X knob: Level / Phase Delay (0 to 200)
            int32_t target_level = ch.level;
            x_active = lock_x_.update(raw_x, target_level * 20, x_val);
            if (x_active) {
                int level = (x_val * 200) >> 12;
                if (level < 0) level = 0;
                if (level > 200) level = 200;
                if (ch.level != level) {
                    ch.level = level;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    if (active_page_ < 4) {
                        // CV/audio channels: bipolar -100..+100 (stored 0..200, centre=100)
                        parameter_feedback_mode_       = FEEDBACK_BAR_BIPOLAR;
                        parameter_feedback_signed_val_ = (int8_t)((level - 100) * 32 / 100);
                        parameter_feedback_value_      = 0;
                    } else {
                        // Pulse channels: unipolar phase delay 0..100%
                        parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                        parameter_feedback_value_ = (uint8_t)((level * 63) / 200);
                        parameter_feedback_signed_val_ = 0;
                    }
                    send_param_update_sysex(active_page_, 8, level);
                }
            }

            // Y knob: Wave Parameter / Intensity or Feedback (for WAVE_CV_DELAY)
            if (ch.wave_shape == WAVE_CV_DELAY) {
                // Y knob: Feedback (controls ch.probability 0..100%)
                int32_t target_prob = ch.probability;
                y_active = lock_y_.update(raw_y, target_prob * 40, y_val);
                if (y_active) {
                    int prob = (y_val * 100) >> 12;
                    if (prob < 0) prob = 0;
                    if (prob > 100) prob = 100;
                    if (ch.probability != prob) {
                        ch.probability = prob;
                        config_dirty_ = true;
                        last_change_time_ms_ = now_ms;
                        parameter_feedback_timer_ = 1200;
                        parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                        parameter_feedback_value_ = (uint8_t)((prob * 63) / 100);
                        parameter_feedback_signed_val_ = 0;
                        send_param_update_sysex(active_page_, 5, prob);
                    }
                }
            } else {
                // Y knob: Wave Parameter / Intensity (0 to 255)
                int32_t target_param = ch.wave_param;
                y_active = lock_y_.update(raw_y, target_param * 16, y_val);
                if (y_active) {
                    int param = (y_val * 256) >> 12;
                    if (param < 0) param = 0;
                    if (param > 255) param = 255;
                    if (ch.wave_param != param) {
                        ch.wave_param = param;
                        calculate_state_inverses(active_page_, ch.wave_shape, param);
                        config_dirty_ = true;
                        last_change_time_ms_ = now_ms;
                        parameter_feedback_timer_ = 1200;
                        parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                        parameter_feedback_value_ = (uint8_t)(param >> 2);
                        parameter_feedback_signed_val_ = 0;
                        send_param_update_sysex(active_page_, 4, param);
                    }
                }
            }
        } else if (sw == Switch::Middle) {
            // ==========================================
            // Rhythm & Performance Tier (Switch is MIDDLE)
            // ==========================================
            // Main knob: Clock Modifier or Delay Length (for WAVE_CV_DELAY)
            if (ch.wave_shape == WAVE_CV_DELAY) {
                // Main knob: Delay Length (controls ch.wave_param 0..255)
                int32_t target_param = ch.wave_param;
                main_active = lock_main_.update(raw_main, target_param * 16, main_val);
                if (main_active) {
                    int param = (main_val * 256) >> 12;
                    if (param < 0) param = 0;
                    if (param > 255) param = 255;
                    if (ch.wave_param != param) {
                        ch.wave_param = param;
                        calculate_state_inverses(active_page_, ch.wave_shape, param);
                        config_dirty_ = true;
                        last_change_time_ms_ = now_ms;
                        parameter_feedback_timer_ = 1200;
                        parameter_feedback_mode_  = (param >= 128) ? FEEDBACK_BAR_UNIPOLAR_BLINK : FEEDBACK_BAR_UNIPOLAR;
                        parameter_feedback_value_ = (uint8_t)(param >> 2);
                        parameter_feedback_signed_val_ = 0;
                        send_param_update_sysex(active_page_, 4, param);
                    }
                }
            } else {
                // Main knob: Clock Modifier (0 to 33) or Trigger Src (34 to 39) or MIDI (40)
                int32_t target_mod = ch.get_clock_modifier();
                if (ch.get_trigger_source() > 0) {
                    target_mod = 33 + ch.get_trigger_source();
                }
                main_active = lock_main_.update(raw_main, target_mod * 102, main_val);
                if (main_active) {
                    int mod = (main_val * 41) >> 12;
                    if (mod < 0) mod = 0;
                    if (mod > 40) mod = 40;
                    if (pending_main_.active && pending_main_.type != PENDING_MODIFIER)
                        commit_pending(pending_main_);
                    if (mod != (int)(pending_main_.active ? pending_main_.value : target_mod)) {
                        uint8_t real_val = mod;
                        if (mod == 40) {
                            real_val = last_midi_channel_[active_page_];
                        }
                        pending_main_ = {true, (uint8_t)active_page_, PENDING_MODIFIER, real_val};
                        division_feedback_timer_ = 1200;
                        division_feedback_value_ = real_val;
                        send_param_update_sysex(active_page_, 0, real_val);
                    }
                }
            }

            // X knob: Euclidean Steps (0 to 16)
            int32_t target_steps = ch.euclidean_steps;
            x_active = lock_x_.update(raw_x, target_steps * 240, x_val);
            if (x_active) {
                int steps = (x_val * 17) >> 12; // 0 to 16
                if (steps < 0) steps = 0;
                if (steps > 16) steps = 16;
                if (pending_x_.active && pending_x_.type != PENDING_STEPS)
                    commit_pending(pending_x_);
                if (steps != (int)(pending_x_.active ? pending_x_.value : ch.euclidean_steps)) {
                    pending_x_ = {true, (uint8_t)active_page_, PENDING_STEPS, (uint8_t)steps};
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_value_ = steps;
                    parameter_feedback_mode_  = FEEDBACK_BINARY;
                    parameter_feedback_signed_val_ = 0;
                    send_param_update_sysex(active_page_, 1, steps);
                }
            }

            // Y knob: Euclidean Fills / Phase Offset or Feedback (for WAVE_CV_DELAY)
            if (ch.wave_shape == WAVE_CV_DELAY) {
                // Y knob: Feedback (controls ch.probability 0..100%)
                int32_t target_prob = ch.probability;
                y_active = lock_y_.update(raw_y, target_prob * 40, y_val);
                if (y_active) {
                    int prob = (y_val * 100) >> 12;
                    if (prob < 0) prob = 0;
                    if (prob > 100) prob = 100;
                    if (ch.probability != prob) {
                        ch.probability = prob;
                        config_dirty_ = true;
                        last_change_time_ms_ = now_ms;
                        parameter_feedback_timer_ = 1200;
                        parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                        parameter_feedback_value_ = (uint8_t)((prob * 63) / 100);
                        parameter_feedback_signed_val_ = 0;
                        send_param_update_sysex(active_page_, 5, prob);
                    }
                }
            } else {
                int32_t target_fills = ch.euclidean_fills;
                int max_fills = (ch.euclidean_steps == 0 && active_page_ < 4) ? 255 : (ch.euclidean_steps == 0 ? 16 : ch.euclidean_steps);
                int32_t multiplier = max_fills == 255 ? 16 : 240;
                y_active = lock_y_.update(raw_y, target_fills * multiplier, y_val);
                if (y_active) {
                    int fills = (y_val * (max_fills + 1)) >> 12;
                    if (fills < 0) fills = 0;
                    if (fills > max_fills) fills = max_fills;
                    if (pending_y_.active && pending_y_.type != PENDING_FILLS)
                        commit_pending(pending_y_);
                    if (fills != (int)(pending_y_.active ? pending_y_.value : ch.euclidean_fills)) {
                        pending_y_ = {true, (uint8_t)active_page_, PENDING_FILLS, (uint8_t)fills};
                        parameter_feedback_timer_ = 1200;
                        if (max_fills == 255) {
                            // Phase offset for CV channel - show as bar
                            parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                            parameter_feedback_value_ = (uint8_t)(fills >> 2);
                        } else {
                            parameter_feedback_mode_  = FEEDBACK_BINARY;
                            parameter_feedback_value_ = (uint8_t)fills;
                        }
                        parameter_feedback_signed_val_ = 0;
                        send_param_update_sysex(active_page_, 2, fills);
                    }
                }
            }
        }
    } else {
        // ==========================================
        // Global Page Settings
        // ==========================================

        if (preset_menu_active_) {
            // ==========================================
            // Preset Menu (Hold DOWN on global page)
            // ==========================================
            // Main knob: select preset slot 0–5, or special zones:
            //   Fully left  (0–340)   = RESET zone (all LEDs lit)
            //   Slot 0–5    (341–3754) = select slot (LED i lit)
            //   Fully right (3755–4095) = do nothing / deadzone
            //
            // X knob:
            //   Fully left  (0–341)   = Load selected slot
            //   Fully right (3755–4095) = Save to selected slot

            main_active = lock_main_.update(raw_main, selected_preset_ * 570 + 570, main_val);
            if (main_active) {
                // Determine zone from main_val (only update selection when knob is unlocked/wiggled)
                if (main_val < 341) {
                    selected_preset_ = 0xFF; // RESET zone sentinel
                } else {
                    int slot = ((main_val - 341) * 6) / (4095 - 341);
                    if (slot < 0) slot = 0;
                    if (slot > 5) slot = 5;
                    selected_preset_ = (uint8_t)slot;
                }
            }

            // X knob: Arm Load or Save (only select when knob is wiggled/unlocked)
            x_active = lock_x_.update(raw_x, 2048, x_val);
            if (x_active) {
                if (x_val < 341) {
                    armed_preset_action_ = 1; // Load armed
                } else if (x_val >= 3755) {
                    armed_preset_action_ = 2; // Save armed
                } else {
                    armed_preset_action_ = 0; // Neutral/no action
                }
            } else {
                armed_preset_action_ = 0; // Lock is engaged
            }

        } else if (sw == Switch::Middle) {
            // MIDDLE Position: BPM, Swing, Humanize
            // ==========================================
            // Main knob: Master BPM (40 to 250 BPM)
            int32_t target_bpm = settings_.bpm;
            main_active = lock_main_.update(raw_main, (target_bpm - 40) * 19, main_val);
            if (main_active) {
                int bpm = 40 + ((main_val * 210) >> 12);
                if (bpm < 40) bpm = 40;
                if (bpm > 250) bpm = 250;
                if (settings_.bpm != bpm) {
                    settings_.bpm = bpm;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                    parameter_feedback_value_ = (uint8_t)(((bpm - 40) * 63) / 210);
                    parameter_feedback_signed_val_ = 0;
                    send_global_update_sysex(0, bpm);
                }
            }

            // X knob: Global Swing (0 to 50%)
            int32_t target_swing = settings_.global_swing;
            x_active = lock_x_.update(raw_x, target_swing * 81, x_val);
            if (x_active) {
                int swing = (x_val * 51) >> 12; // 0 to 50
                if (swing < 0) swing = 0;
                if (swing > 50) swing = 50;
                if (settings_.global_swing != swing) {
                    settings_.global_swing = swing;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                    parameter_feedback_value_ = (uint8_t)((swing * 63) / 50);
                    parameter_feedback_signed_val_ = 0;
                    send_global_update_sysex(4, swing);
                }
            }

            // Y knob: Global Humanize (0 to 100%)
            int32_t target_humanize = settings_.global_humanize;
            y_active = lock_y_.update(raw_y, target_humanize * 40, y_val);
            if (y_active) {
                int humanize = (y_val * 101) >> 12; // 0 to 100
                if (humanize < 0) humanize = 0;
                if (humanize > 100) humanize = 100;
                if (settings_.global_humanize != humanize) {
                    settings_.global_humanize = humanize;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_mode_  = FEEDBACK_BAR_UNIPOLAR;
                    parameter_feedback_value_ = (uint8_t)((humanize * 63) / 100);
                    parameter_feedback_signed_val_ = 0;
                    send_global_update_sysex(5, humanize);
                }
            }
        } else if (sw == Switch::Up) {
            // UP Position: Ext PPQN, Reset Mode, Sync Source
            // ==========================================
            // Main knob: External Clock PPQN (1, 4, 24, 48)
            int32_t target_ppqn_pos = 1536;
            if (settings_.ext_ppqn == 1)  target_ppqn_pos = 512;
            else if (settings_.ext_ppqn == 4)  target_ppqn_pos = 1536;
            else if (settings_.ext_ppqn == 24) target_ppqn_pos = 2560;
            else if (settings_.ext_ppqn == 48) target_ppqn_pos = 3584;
            main_active = lock_main_.update(raw_main, target_ppqn_pos, main_val);
            if (main_active) {
                int select = (main_val * 4) >> 12;
                if (select < 0) select = 0;
                if (select > 3) select = 3;
                uint8_t ppqn = 4;
                if (select == 0) ppqn = 1;
                else if (select == 1) ppqn = 4;
                else if (select == 2) ppqn = 24;
                else if (select == 3) ppqn = 48;
                if (settings_.ext_ppqn != ppqn) {
                    settings_.ext_ppqn = ppqn;
                    config_dirty_ = true;
                    last_change_time_ms_ = now_ms;
                    send_global_update_sysex(3, ppqn);
                }
            }

            // X knob: Global Seed (0 to 63)
            int32_t target_seed = settings_.global_seed;
            int32_t target_val = (target_seed * 4095) / 63;
            x_active = lock_x_.update(raw_x, target_val, x_val);
            if (x_active) {
                int seed = (x_val * 64) >> 12; // 0 to 63
                if (seed < 0) seed = 0;
                if (seed > 63) seed = 63;
                if (pending_x_.active && pending_x_.type != PENDING_SEED)
                    commit_pending(pending_x_);
                if (seed != (int)(pending_x_.active ? pending_x_.value : settings_.global_seed)) {
                    pending_x_ = {true, 6u, PENDING_SEED, (uint8_t)seed};
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_value_ = seed;
                    parameter_feedback_mode_  = FEEDBACK_BINARY;
                    parameter_feedback_signed_val_ = 0;
                    send_global_update_sysex(6, seed);
                }
            }

            // Y knob: Combined Sync Source & Reset Mode
            // 0 = Sync Int, Reset Pulse
            // 1 = Sync Int, Run Gate
            // 2 = Sync Ext, Reset Pulse
            // 3 = Sync Ext, Run Gate
            int32_t current_combination = (settings_.sync_source << 1) | settings_.reset_mode;
            y_active = lock_y_.update(raw_y, current_combination * 1000, y_val);
            if (y_active) {
                int select = (y_val * 4) >> 12; // 0 to 3
                if (select < 0) select = 0;
                if (select > 3) select = 3;
                uint8_t new_sync = select >= 2 ? 1 : 0;
                uint8_t new_reset = select & 1;
                if (settings_.sync_source != new_sync || settings_.reset_mode != new_reset) {
                    if (pending_y_.active && pending_y_.type != PENDING_SYNC_RESET)
                        commit_pending(pending_y_);
                    uint8_t combined = (uint8_t)((new_sync << 1) | new_reset);
                    pending_y_ = {true, 6u, PENDING_SYNC_RESET, combined};
                    parameter_feedback_timer_ = 1200;
                    parameter_feedback_value_ = (uint8_t)select;
                    parameter_feedback_mode_  = FEEDBACK_BINARY;
                    parameter_feedback_signed_val_ = 0;
                    send_global_update_sysex(1, new_sync);
                    send_global_update_sysex(2, new_reset);
                }
            }
        }
    }

    if (main_active || x_active || y_active) {
        param_changed_during_press = true;
    }

    // Sync parameters with Core 1
    sync_params_to_core1();

    // ==========================================
    // Special LED overrides (preset menu, blink, feedback)
    // ==========================================
    bool should_override_leds = (edit_feedback_timer_ > 0) ||
                                (preset_menu_active_) ||
                                (division_feedback_timer_ > 0) ||
                                (parameter_feedback_timer_ > 0) ||
                                (active_page_ == 6 && sw == Switch::Up) ||
                                (page_blink_timer_ > 0);
    ui_led_override_ = should_override_leds;

    if (edit_feedback_timer_ > 0) {
        // Rapid flash of all LEDs (save/load/reset confirmation)
        bool flash_on = (now_ms % 100) < 50;
        for (int i = 0; i < 6; i++) {
            LedOn(i, flash_on);
        }
    } else if (preset_menu_active_) {
        // Preset menu: show slot selection on LEDs
        // Blink pattern:
        // - Neutral: Solid
        // - Load armed (armed_preset_action_ == 1): Slow Blink (4Hz)
        // - Save armed (armed_preset_action_ == 2): Fast Blink (10Hz)
        bool flash_state = true;
        if (armed_preset_action_ == 1) {
            flash_state = (now_ms % 250) < 125; // 4Hz slow blink
        } else if (armed_preset_action_ == 2) {
            flash_state = (now_ms % 100) < 50;  // 10Hz fast blink
        }

        if (selected_preset_ == 0xFF) {
            // RESET zone: all 6 LEDs lit
            for (int i = 0; i < 6; i++) LedOn(i, flash_state);
        } else {
            // Normal slot: only the selected LED lit
            for (int i = 0; i < 6; i++) LedOn(i, (i == selected_preset_) && flash_state);
        }
    } else if (division_feedback_timer_ > 0) {
        uint8_t m = division_feedback_value_;
        if (m >= 40 && m <= 55) {
            render_leds_feedback(FEEDBACK_BINARY, m - 39, 2, now_ms);
        } else if (m > 33) {
            // Show trigger source: light up the single LED corresponding to the source channel
            uint8_t src_ch = m - 34; // 0..5
            for (int i = 0; i < 6; i++) {
                LedBrightness(i, (i == src_ch) ? 4095 : 0);
            }
        } else {
            uint8_t val = 1;
            bool special_div = false;
            if (m == 20) { // x1
                val = 1;
            } else {
                int diff = (m > 20) ? (m - 20) : (20 - m);
                switch (diff) {
                    case 1:  val = 2; break;  // /2 or x2
                    case 2:  val = 3; break;  // /3 or x3
                    case 3:  val = 4; break;  // /4 or x4
                    case 4:  val = 6; break;  // /6 or x6
                    case 5:  val = 8; break;  // /8 or x8
                    case 6:  val = 12; break; // /12 or x12
                    case 7:  val = 16; break; // /16 or x16
                    case 8:  val = 24; break; // /24 or x24
                    case 9:  val = 32; break; // /32 or x32
                    case 10: val = 48; break; // /48 or x48
                    case 11: val = 32; special_div = true; break; // /64 or x64
                    case 12: val = 48; special_div = true; break; // /96 or x96
                    case 13: val = 56; special_div = true; break; // /128 or x128
                    case 14: val = 24; special_div = true; break; // /192
                    case 15: val = 16; special_div = true; break; // /256
                    case 16: val = 12; special_div = true; break; // /384
                    case 17: val = 8;  special_div = true; break; // /512
                    case 18: val = 6;  special_div = true; break; // /768
                    case 19: val = 4;  special_div = true; break; // /1024
                    case 20: val = 3;  special_div = true; break; // /2048
                    default: val = 1; break;
                }
            }
            render_leds_feedback(FEEDBACK_BINARY, val, special_div ? 2 : 0, now_ms);
        }
    } else if (parameter_feedback_timer_ > 0) {
        render_leds_feedback(parameter_feedback_mode_, parameter_feedback_value_,
                             parameter_feedback_signed_val_, now_ms);
    } else if (active_page_ == 6 && sw == Switch::Up && page_blink_timer_ == 0) {
        // Show PPQN selection on LEDs
        int num_leds = 2;
        if (settings_.ext_ppqn == 1)       num_leds = 1;
        else if (settings_.ext_ppqn == 4)  num_leds = 2;
        else if (settings_.ext_ppqn == 24) num_leds = 4;
        else if (settings_.ext_ppqn == 48) num_leds = 6;
        for (int i = 0; i < 6; i++) {
            LedOn(i, i < num_leds);
        }
    } else if (page_blink_timer_ > 0) {
        // Page-change double-blink: two quick flashes over 250ms
        // Pattern: on(0-50), off(50-100), on(100-150), off(150-250)
        uint32_t t = 250 - page_blink_timer_; // 0 = just triggered, 250 = end
        bool blink_on = (t < 50) || (t >= 100 && t < 150);
        for (int i = 0; i < 6; i++) {
            if (active_page_ < 6) {
                if (i == active_page_) {
                    LedOn(i, blink_on);
                } else {
                    LedOff(i);
                }
            } else {
                // Global page: blink all LEDs
                LedOn(i, blink_on);
            }
        }
    }

    // ==========================================
    // Telemetry Stream Output (30ms intervals)
    // ==========================================
    if (telemetry_enabled_ && (now_ms - telemetry_watchdog_ms_ < 5000)) {
        if (now_ms - last_telemetry_time_ms_ >= 30) {
            last_telemetry_time_ms_ = now_ms;
            send_telemetry_packet_sysex();
        }
    } else {
        telemetry_enabled_ = false;
    }
}

void ClockworksCard::process_incoming_serial() {
    // No-op (WebMIDI SysEx is used instead)
}

void ClockworksCard::apply_parameter_change(uint8_t ch, uint8_t param_id, uint8_t value) {
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (ch < 6) {
        ChannelConfig &ch_config = settings_.channels[ch];
        switch (param_id) {
            case 0: {
                uint8_t mod = (value > 55) ? 55 : value;
                ch_config.clock_modifier = mod;
                if (mod >= 40 && mod <= 55) {
                    last_midi_channel_[ch] = mod;
                }
                break;
            }
            case 1: 
                ch_config.euclidean_steps = (value > 16) ? 16 : value; 
                if (ch_config.get_quantizer_scale() >= SCALE_NUM_SCALES) {
                    ch_config.set_quantizer_scale(SCALE_OFF);
                }
                if (ch_config.euclidean_steps > 0) {
                    if (ch_config.get_euclidean_offset() >= ch_config.euclidean_steps) {
                        ch_config.set_euclidean_offset(ch_config.euclidean_steps - 1);
                    }
                } else {
                    ch_config.set_euclidean_offset(0);
                }
                break;
            case 2: ch_config.euclidean_fills = (value > 16) ? 16 : value; break;
            case 3: 
                if (ch >= 4) {
                    ch_config.wave_shape = (value >= 6) ? 5 : value;
                } else {
                    ch_config.wave_shape = (value >= WAVE_NUM_SHAPES) ? (WAVE_NUM_SHAPES - 1) : value; 
                }
                calculate_state_inverses(ch, ch_config.wave_shape, ch_config.wave_param);
                break;
            case 4: 
                ch_config.wave_param = value; 
                calculate_state_inverses(ch, ch_config.wave_shape, value);
                break;
            case 5: ch_config.probability = (value > 100) ? 100 : value; break;
            case 6: 
                if (ch_config.euclidean_steps > 0) {
                    uint8_t max_offset = ch_config.euclidean_steps - 1;
                    ch_config.set_euclidean_offset((value > max_offset) ? max_offset : value);
                } else {
                    ch_config.set_quantizer_scale((value >= SCALE_NUM_SCALES) ? (uint8_t)SCALE_OFF : (uint8_t)value);
                }
                break;
            case 7: ch_config.loop_length = (value > 64) ? 64 : value; break;
            case 8: ch_config.level = (value > 200) ? 200 : value; break;
            case 9: ch_config.glide = value; break;
        }
        config_dirty_ = true;
        last_change_time_ms_ = now_ms;
        sync_params_to_core1();
    } else if (ch == 6) { // Global edits
        switch (param_id) {
            case 0: settings_.bpm = (value < 40) ? 40 : ((value > 250) ? 250 : value); break;
            case 1: settings_.sync_source = value > 0 ? 1 : 0; break;
            case 2: settings_.reset_mode = value > 0 ? 1 : 0; break;
            case 3: settings_.ext_ppqn = (value == 1 || value == 4 || value == 24 || value == 48) ? value : 4; break;
            case 4: settings_.global_swing = (value > 50) ? 50 : value; break;
            case 5: settings_.global_humanize = (value > 100) ? 100 : value; break;
            case 6: settings_.global_seed = (value > 63) ? 63 : value; break;
        }
        config_dirty_ = true;
        last_change_time_ms_ = now_ms;
        sync_params_to_core1();
    }
}

void ClockworksCard::process_incoming_sysex(uint8_t const *buf, int len) {
    if (len < 5 || buf[1] != 0x7D || buf[2] != 0x43) return;

    uint8_t cmd = buf[3];
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());

    // Reset watchdog on any valid SysEx from Web UI
    telemetry_watchdog_ms_ = now_ms;

    if (cmd == 0x03) {
        // Ping / Start Telemetry
        telemetry_enabled_ = true;
        send_sync_packet();
    } else if (cmd == 0x04) {
        // Stop Telemetry
        telemetry_enabled_ = false;
    } else if (cmd == 0x05) {
        // Set Parameter: payload has ch, paramId, val_high, val_low
        if (len < 9) return;
        telemetry_enabled_ = true;
        uint8_t ch = buf[4];
        uint8_t param_id = buf[5];
        uint8_t val_high = buf[6];
        uint8_t val_low = buf[7];
        uint8_t value = (val_high << 4) | val_low;
        apply_parameter_change(ch, param_id, value);
        lock_knobs();
    } else if (cmd == 0x06) {
        // Play
        paused_ = false;
        manual_reset_counter_++;
        sync_params_to_core1();
    } else if (cmd == 0x07) {
        // Stop
        paused_ = true;
        sync_params_to_core1();
    } else if (cmd == 0x08) {
        // Save Preset
        if (len < 6) return;
        uint8_t slot = buf[4];
        if (slot < 6) {
            save_preset(slot);
            edit_feedback_timer_ = 500;
        }
    } else if (cmd == 0x09) {
        // Load Preset
        if (len < 6) return;
        uint8_t slot = buf[4];
        if (slot < 6) {
            load_preset(slot);
        }
    } else if (cmd == 0x0A) {
        // Reset Defaults
        apply_default_settings();
        save_settings_to_flash();
        lock_knobs();
        manual_reset_counter_++;
        edit_feedback_timer_ = 2000;
        send_sync_packet();
    }
}

void ClockworksCard::send_telemetry_packet_sysex() {
    if (g_usb_midi_host) return; // Only in Device mode
    if (!tud_midi_mounted()) return;

    uint8_t frame[29];
    frame[0] = 0xFA;
    frame[1] = 0xFB;

    static bool alternate_max_min = false;
    alternate_max_min = !alternate_max_min;

    int idx = 2;
    for (int i = 0; i < 6; i++) {
        frame[idx++] = states_[i].step_index;
        
        int16_t max_val = states_[i].telemetry_max;
        int16_t min_val = states_[i].telemetry_min;
        int16_t cur_val = states_[i].last_value;
        
        states_[i].telemetry_max = cur_val;
        states_[i].telemetry_min = cur_val;

        int16_t val;
        if (max_val > 0 && min_val < 0) {
            val = alternate_max_min ? max_val : min_val;
        } else {
            int16_t abs_max = max_val < 0 ? -max_val : max_val;
            int16_t abs_min = min_val < 0 ? -min_val : min_val;
            val = (abs_max >= abs_min) ? max_val : min_val;
        }

        frame[idx++] = (uint8_t)(val & 0xFF);
        frame[idx++] = (uint8_t)((val >> 8) & 0xFF);
    }

    int32_t raw_main = KnobVal(Knob::Main);
    int32_t raw_x = KnobVal(Knob::X);
    int32_t raw_y = KnobVal(Knob::Y);
    frame[idx++] = (uint8_t)(raw_main & 0xFF);
    frame[idx++] = (uint8_t)((raw_main >> 8) & 0xFF);
    frame[idx++] = (uint8_t)(raw_x & 0xFF);
    frame[idx++] = (uint8_t)((raw_x >> 8) & 0xFF);
    frame[idx++] = (uint8_t)(raw_y & 0xFF);
    frame[idx++] = (uint8_t)((raw_y >> 8) & 0xFF);

    frame[idx++] = paused_ ? 1 : 0;
    frame[idx++] = active_page_;
    frame[idx++] = 0xFC;

    // Encode as 58 nibbles in a SysEx message: F0 7D 43 01 <58 nibbles> F7
    uint8_t sysex[64];
    sysex[0] = 0xF0;
    sysex[1] = 0x7D;
    sysex[2] = 0x43;
    sysex[3] = 0x01; // Telemetry command

    for (int i = 0; i < 29; i++) {
        sysex[4 + 2 * i]     = (frame[i] >> 4) & 0x0F;
        sysex[4 + 2 * i + 1] = frame[i] & 0x0F;
    }

    sysex[62] = 0xF7;

    tud_midi_stream_write(0, sysex, 63);
}

void ClockworksCard::send_sync_packet() {
    if (g_usb_midi_host) return; // Only in Device mode
    if (!tud_midi_mounted()) return;

    uint8_t frame[80];
    frame[0] = 0xFD;
    frame[1] = 0xFE;
    frame[2] = 0xFD;
    uint32_t active_bpm = settings_.bpm;
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    bool midi_active = (now_ms - last_midi_clock_time_ms_ < 2000) && (last_midi_clock_time_ms_ > 0);
    uint8_t active_source = 0;
    if (midi_active && midi_period_samples_ > 0) {
        uint32_t midi_bpm = safe_div_u32(SAMPLES_PER_MINUTE, midi_period_samples_ * 24);
        if (midi_bpm >= 30 && midi_bpm <= 300) {
            active_bpm = midi_bpm;
        }
        active_source = 2; // MIDI Clock
    } else if (external_clock_active_ && ext_period_samples_ > 0) {
        uint32_t ext_bpm = safe_div_u32(SAMPLES_PER_MINUTE, ext_period_samples_ * settings_.ext_ppqn);
        if (ext_bpm >= 30 && ext_bpm <= 300) {
            active_bpm = ext_bpm;
        }
        active_source = 1; // External Pulse
    }
    frame[3] = (uint8_t)active_bpm;
    frame[4] = active_source;
    frame[5] = settings_.reset_mode;
    frame[6] = settings_.ext_ppqn;
    frame[7] = settings_.global_swing;
    frame[8] = settings_.global_humanize;
    frame[9] = settings_.global_seed;

    int idx = 10;
    for (int i = 0; i < 6; i++) {
        frame[idx++] = settings_.channels[i].clock_modifier;
        frame[idx++] = settings_.channels[i].euclidean_steps;
        frame[idx++] = settings_.channels[i].euclidean_fills;
        frame[idx++] = settings_.channels[i].wave_shape;
        frame[idx++] = settings_.channels[i].wave_param;
        frame[idx++] = settings_.channels[i].probability;
        frame[idx++] = settings_.channels[i].quantizer_scale_and_offset;
        frame[idx++] = settings_.channels[i].loop_length;
        frame[idx++] = settings_.channels[i].level;
        frame[idx++] = settings_.channels[i].glide;
    }

    // Encode as 140 nibbles in a SysEx message: F0 7D 43 02 <140 nibbles> F7
    uint8_t sysex[146];
    sysex[0] = 0xF0;
    sysex[1] = 0x7D;
    sysex[2] = 0x43;
    sysex[3] = 0x02; // Sync command

    for (int i = 0; i < 70; i++) {
        sysex[4 + 2 * i]     = (frame[i] >> 4) & 0x0F;
        sysex[4 + 2 * i + 1] = frame[i] & 0x0F;
    }

    sysex[144] = 0xF7;

    tud_midi_stream_write(0, sysex, 145);
}

void ClockworksCard::send_param_update_sysex(uint8_t ch, uint8_t param_id, uint8_t value) {
    if (g_usb_midi_host) return; // Only in Device mode
    if (!tud_midi_mounted()) return;
    uint8_t sysex[9];
    sysex[0] = 0xF0;
    sysex[1] = 0x7D;
    sysex[2] = 0x43;
    sysex[3] = 0x05;
    sysex[4] = ch;
    sysex[5] = param_id;
    sysex[6] = (value >> 4) & 0x0F;
    sysex[7] = value & 0x0F;
    sysex[8] = 0xF7;
    tud_midi_stream_write(0, sysex, 9);
}

void ClockworksCard::send_global_update_sysex(uint8_t param_id, uint8_t value) {
    if (g_usb_midi_host) return; // Only in Device mode
    if (!tud_midi_mounted()) return;
    uint8_t sysex[8];
    sysex[0] = 0xF0;
    sysex[1] = 0x7D;
    sysex[2] = 0x43;
    sysex[3] = 0x0B;
    sysex[4] = param_id;
    sysex[5] = (value >> 4) & 0x0F;
    sysex[6] = value & 0x0F;
    sysex[7] = 0xF7;
    tud_midi_stream_write(0, sysex, 8);
}

void ClockworksCard::lock_knobs() {
    int32_t p_main = KnobVal(Knob::Main);
    int32_t p_x = KnobVal(Knob::X);
    int32_t p_y = KnobVal(Knob::Y);
    lock_main_.engage(p_main);
    lock_x_.engage(p_x);
    lock_y_.engage(p_y);
}

// Global USB role variable
volatile bool g_usb_midi_host = false;

// Global card instance
ClockworksCard card;

// USB Host MIDI callbacks
extern "C" {
bool tuh_midi_packet_read(uint8_t dev_addr, uint8_t packet[4]);

void tuh_midi_mount_cb(uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep, uint8_t num_cables_rx, uint16_t num_cables_tx) {
    (void)dev_addr; (void)in_ep; (void)out_ep; (void)num_cables_rx; (void)num_cables_tx;
}

void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr; (void)instance;
}

void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets) {
    (void)num_packets;
    uint8_t packet[4];
    while (tuh_midi_packet_read(dev_addr, packet)) {
        card.process_incoming_midi_packet(packet);
    }
}

void tuh_midi_tx_cb(uint8_t dev_addr) {
    (void)dev_addr;
}
}

// Core 1 Entry Point — audio engine only, runs entirely from RAM
void core1_entry() {
    card.Run(); // 48kHz ISR, never returns. No lockout victim needed.
}

// Core 0 Main — UI, serial, flash, Web UI sync
// Flash writes here don't interrupt Core 1 because all code runs from RAM.
int main() {
    // 1. Raise core voltage for overclocking stability (1.25V matches grains/modes/bends)
    vreg_set_voltage(VREG_VOLTAGE_1_25);
    sleep_ms(10);

    // 2. 192MHz for maximum DSP headroom (hardware_vreg is linked)
    set_sys_clock_khz(192000, true);

    // 3. Core 1: audio engine (48kHz ISR, blocking, never returns)
    multicore_launch_core1(core1_entry);

    // 4. Core 0: UI, serial comms, autosave, Web UI sync
    card.run_core0_ui_loop();
}

