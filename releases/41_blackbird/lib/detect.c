#include "lib/detect.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include "sample_rate.h"

#define DETECT_DEBUG 0

extern volatile uint64_t global_sample_counter;

// ARM CORTEX-M0+ MEMORY BARRIERS FOR CACHE COHERENCY
// These are ESSENTIAL for reliable multicore communication on RP2040
// DMB (Data Memory Barrier) ensures all memory operations complete before proceeding
// DSB (Data Synchronization Barrier) is stronger - waits for all memory operations AND side effects
#define DMB() __asm volatile ("dmb" ::: "memory")
#define DSB() __asm volatile ("dsb" ::: "memory")

// Detection system state
static Detect_t* detectors = NULL;
static int detector_count = 0;

#define DETECT_CANARY 0xD37EC7u

// Detection processing sample rate (matches audio engine)
#define DETECT_SAMPLE_RATE PROCESS_SAMPLE_RATE_HZ
// Crow evaluates detectors once per 32-sample audio block. We still call
// Detect_process_sample() every sample for edge fidelity, but interval-based
// modes (stream / volume / peak) should interpret the user-specified interval
// the same way crow does: in units of 32-sample blocks. So we scale the
// interval by (sample_rate / block_size) rather than raw sample_rate.
#define DETECT_BLOCK_SIZE 16.0f
#define DETECT_BLOCK_RATE (DETECT_SAMPLE_RATE / DETECT_BLOCK_SIZE)

// VU Meter implementation
VU_meter_t* VU_init(void) {
    VU_meter_t* vu = (VU_meter_t*)malloc(sizeof(VU_meter_t));
    if (vu) {
        vu->level = 0.0f;
        vu->time_constant = 0.018f; // Default 18ms time constant
        vu->attack_coeff = 0.99f;   // Fast attack
        vu->release_coeff = 0.999f; // Slower release
    }
    return vu;
}

void VU_deinit(VU_meter_t* vu) {
    if (vu) {
        free(vu);
    }
}

void VU_time(VU_meter_t* vu, float time_seconds) {
    if (!vu) return;
    vu->time_constant = time_seconds;
    // Calculate coefficients based on time constant
    // Processing runs at the full audio sample rate
    float rate = DETECT_SAMPLE_RATE;
    vu->attack_coeff = expf(-1.0f / (time_seconds * rate * 0.1f)); // Fast attack
    vu->release_coeff = expf(-1.0f / (time_seconds * rate));       // Slower release
}

float VU_step(VU_meter_t* vu, float input) {
    if (!vu) return 0.0f;
    
    float abs_input = fabsf(input);
    
    if (abs_input > vu->level) {
        // Attack: fast response to rising levels
        vu->level = abs_input + vu->attack_coeff * (vu->level - abs_input);
    } else {
        // Release: slower response to falling levels
        vu->level = abs_input + vu->release_coeff * (vu->level - abs_input);
    }
    
    return vu->level;
}

// Forward declarations for detection mode functions
static void d_none(Detect_t* self, float level, bool block_boundary);
static void d_stream(Detect_t* self, float level, bool block_boundary);
static void d_change(Detect_t* self, float level, bool block_boundary);
static void d_window(Detect_t* self, float level, bool block_boundary);
static void d_scale(Detect_t* self, float level, bool block_boundary);
static void d_volume(Detect_t* self, float level, bool block_boundary);
static void d_peak(Detect_t* self, float level, bool block_boundary);

// Helper functions
static void scale_bounds(Detect_t* self, int ix, int oct);

void Detect_init(int channels) {
    detector_count = channels;
    detectors = (Detect_t*)calloc(channels, sizeof(Detect_t));
    
    for (int i = 0; i < channels; i++) {
        detectors[i].channel = i;
        detectors[i].last = 0.0f;
        detectors[i].state = 0;
        detectors[i].samples_in_current_block = 0;
        
        // *** OPTIMIZATION: Initialize integer-only ISR fields ***
        detectors[i].last_raw_adc = 0;
        detectors[i].sample_counter = 0;
        detectors[i].state_changed = false;
        detectors[i].event_raw_value = 0;
        detectors[i].threshold_raw = 0;
        detectors[i].hysteresis_raw = 1; // Minimum 1 count (~3mV)
        
        detectors[i].mode_switching = false;
        detectors[i].last_sample = 0.0f;
        detectors[i].canary = DETECT_CANARY;
        detectors[i].change_rise_count = 0;
        detectors[i].change_fall_count = 0;
        detectors[i].stream.last_sample_counter = 0;
        
        // CRITICAL: Initialize all detectors to "none" mode like real crow
        Detect_none(&detectors[i]);
    }
}

void Detect_deinit(void) {
    if (detectors) {
        free(detectors);
        detectors = NULL;
    }
    detector_count = 0;
}

Detect_t* Detect_ix_to_p(uint8_t index) {
    if (index < detector_count && detectors) {
        return &detectors[index];
    }
    return NULL;
}

int8_t Detect_str_to_dir(const char* str) {
    if (!str) return 0;
    
    if (strcmp(str, "rising") == 0 || strcmp(str, "up") == 0) {
        return 1;
    } else if (strcmp(str, "falling") == 0 || strcmp(str, "down") == 0) {
        return -1;
    } else if (strcmp(str, "both") == 0) {
        return 0;
    }
    
    return 0; // default to both
}

// Mode configuration functions
void Detect_none(Detect_t* self) {
    if (!self) return;
    
    // Signal ISR to skip this detector during reconfiguration
    self->mode_switching = true;
    DMB();  // Ensure ISR sees the flag immediately
    
    self->modefn = d_none;
    self->action = NULL;
    
    // Clear any pending events
    self->state_changed = false;
    
    // Reconfiguration complete
    DMB();  // Ensure all changes are visible
    self->mode_switching = false;
}

void Detect_stream(Detect_t* self, Detect_callback_t cb, float interval) {
    if (!self) return;
    
    // Signal ISR to skip this detector
    self->mode_switching = true;
    DMB();
    
    self->modefn = d_stream;
    self->action = cb;

    // Sample-accurate interval (seconds -> samples)
    // Preserve crow-style block semantics for compatibility, but prefer sample timing for fidelity
    uint32_t samples = (uint32_t)(interval * DETECT_SAMPLE_RATE + 0.5f);
    if (samples == 0) samples = 1;

    self->stream.interval_samples = samples;
    self->stream.sample_countdown = samples;

    // Legacy block-based fields (kept for compatibility/fallback in Core0)
    self->stream.blocks = (int)(interval * DETECT_BLOCK_RATE);
    if (self->stream.blocks <= 0) self->stream.blocks = 1;
    self->stream.countdown = self->stream.blocks;
    self->stream.last_sample_counter = global_sample_counter;
    
    // Clear any pending events
    self->state_changed = false;
    
    DMB();
    self->mode_switching = false;
}

void Detect_change(Detect_t* self, Detect_callback_t cb, float threshold, float hysteresis, int8_t direction) {
    if (!self) return;
    
    // Signal ISR to skip this detector
    self->mode_switching = true;
    DMB();
    
    self->modefn = d_change;
    self->action = cb;
    self->change.threshold = threshold;
    self->change.hysteresis = hysteresis;
    // Safety clamp: extremely small or zero hysteresis leads to chatter/noise-triggered floods.
    // crow firmware enforces minimum hysteresis in some modes (e.g. scale); we mirror that spirit here.
    if (self->change.hysteresis < 0.001f) {
        self->change.hysteresis = 0.001f; // ~1mV in crow volts domain (adjust if scaling differs)
    }
    self->change.direction = direction;
    
    // *** OPTIMIZATION: Pre-convert thresholds to integer for ISR use ***
    // Convert threshold from volts to raw ADC counts
    // ADC: ±2047 counts maps to ±6V, so conversion factor = 2047.0 / 6.0
    static const float VOLTS_TO_ADC = 341.166667f; // 2047.0 / 6.0
    self->threshold_raw = (int16_t)(threshold * VOLTS_TO_ADC);
    self->hysteresis_raw = (int16_t)(hysteresis * VOLTS_TO_ADC);
    
    // Clamp hysteresis minimum (in ADC counts, ~3mV = 1 count)
    if (self->hysteresis_raw < 1) {
        self->hysteresis_raw = 1;
    }
    
    // CRITICAL FIX: Initialize state based on current input voltage to prevent false triggers
    // This ensures we don't get a spurious callback when mode is set while input is already high/low
    // Real crow samples the current state before starting detection to avoid this issue
    if (self->last_raw_adc > self->threshold_raw) {
        self->state = 1; // Input is currently above threshold (high state)
    } else {
        self->state = 0; // Input is currently below threshold (low state)
    }
    
    // Clear any pending events
    self->state_changed = false;
    
    DMB();
    self->mode_switching = false;
}

void Detect_scale(Detect_t* self, Detect_callback_t cb, float* scale, int sLen, float divs, float scaling) {
    if (!self || !scale || sLen > SCALE_MAX_COUNT) return;
    
    // CRITICAL: Signal ISR to skip during reconfiguration
    self->mode_switching = true;
    DMB();  // Barrier ensures ISR sees flag before we modify state
    
    self->modefn = d_scale;
    self->action = cb;
    
    D_scale_t* s = &self->scale;
    s->sLen = (sLen > SCALE_MAX_COUNT) ? SCALE_MAX_COUNT : sLen;
    s->divs = divs;
    s->scaling = scaling;
    
    if (sLen == 0) {
        // Assume chromatic
        s->sLen = (divs > SCALE_MAX_COUNT) ? SCALE_MAX_COUNT : (int)divs;
        for (int i = 0; i < s->sLen; i++) {
            s->scale[i] = (float)i;
        }
    } else {
        for (int i = 0; i < s->sLen; i++) {
            s->scale[i] = scale[i];
        }
    }
    
    // Calculate parameters
    s->offset = 0.5f * scaling / divs;
    s->win = scaling / ((float)s->sLen);
    // Use fixed 40mV hysteresis for maximum noise immunity while still reaching all chromatic notes
    // For chromatic (83.3mV window), max is win/2 = 41.7mV before windows become unreachable
    s->hyst = 0.040f;  // 40mV fixed hysteresis
    
    // Set to invalid note initially (calls scale_bounds with DMB)
    scale_bounds(self, 0, -10);
    
    // Clear any pending events from previous mode
    self->state_changed = false;
    
    // All configuration complete, re-enable ISR processing
    DMB();  // Ensure all writes are visible to Core 1
    self->mode_switching = false;
}

void Detect_window(Detect_t* self, Detect_callback_t cb, float* windows, int wLen, float hysteresis) {
    if (!self || !windows || wLen > WINDOW_MAX_COUNT) return;
    
    // Signal ISR to skip this detector
    self->mode_switching = true;
    DMB();
    
    self->modefn = d_window;
    self->action = cb;
    self->win.wLen = (wLen > WINDOW_MAX_COUNT) ? WINDOW_MAX_COUNT : wLen;
    self->win.hysteresis = hysteresis;
    self->win.lastWin = 0;
    
    for (int i = 0; i < self->win.wLen; i++) {
        self->win.windows[i] = windows[i];
    }
    
    // Clear any pending events
    self->state_changed = false;
    
    DMB();
    self->mode_switching = false;
}

void Detect_volume(Detect_t* self, Detect_callback_t cb, float interval) {
    if (!self) return;
    
    // Signal ISR to skip this detector
    self->mode_switching = true;
    DMB();
    
    self->modefn = d_volume;
    self->action = cb;

    // Initialize VU meter if not already done
    if (!self->vu) {
        self->vu = VU_init();
        VU_time(self->vu, 0.018f); // 18ms time constant
    }

    // Crow semantics (see Detect_stream)
    self->volume.blocks = (int)(interval * DETECT_BLOCK_RATE);
    if (self->volume.blocks <= 0) self->volume.blocks = 1;
    self->volume.countdown = self->volume.blocks;
    
    // Clear any pending events
    self->state_changed = false;
    
    DMB();
    self->mode_switching = false;
}

void Detect_peak(Detect_t* self, Detect_callback_t cb, float threshold, float hysteresis) {
    if (!self) return;
    
    // Signal ISR to skip this detector
    self->mode_switching = true;
    DMB();
    
    self->modefn = d_peak;
    self->action = cb;

    // Initialize VU meter if not already done
    if (!self->vu) {
        self->vu = VU_init();
        VU_time(self->vu, 0.18f); // 180ms time constant for peak detection
    }

    self->peak.threshold = threshold;
    self->peak.hysteresis = hysteresis;
    self->peak.release = 0.01f;
    self->peak.envelope = 0.0f;
    self->state = 0; // Reset state
    
    // Clear any pending events
    self->state_changed = false;
    
    DMB();
    self->mode_switching = false;
}

void Detect_freq(Detect_t* self, Detect_callback_t cb, float interval) {
    if (!self) return;
    // Frequency detection not implemented - just stub for now
    self->modefn = d_none;
    self->action = cb;
}

// Detection mode processing functions
static void d_none(Detect_t* self, float level, bool block_boundary) {
    // Do nothing
    (void)level;
    (void)block_boundary;
    return;
}

static void d_stream(Detect_t* self, float level, bool block_boundary) {
    // Stream mode: Only decrement countdown on block boundaries for correct timing
    if (block_boundary) {
        if (--self->stream.countdown <= 0) {
            self->stream.countdown = self->stream.blocks;
            if (self->action) {
                (*self->action)(self->channel, level);
            }
        }
    }
}

static void d_change(Detect_t* self, float level, bool block_boundary) {
    // Change mode: Process every sample for sample-accurate edge detection
    (void)block_boundary; // Not used for change detection
    
    if (self->state) { // high to low
        if (level < (self->change.threshold - self->change.hysteresis)) {
            if (DETECT_DEBUG) {
                printf("[detect] ch%d FALL level=%.3f thresh=%.3f hyst=%.3f dir=%d\r\n",
                       self->channel, level, self->change.threshold, self->change.hysteresis, self->change.direction);
            }
            self->state = 0;
            self->change_fall_count++;
            if (self->change.direction != 1) { // not 'rising' only
                if (self->action) {
                    (*self->action)(self->channel, (float)self->state);
                }
            }
        }
    } else { // low to high
        if (level > (self->change.threshold + self->change.hysteresis)) {
            if (DETECT_DEBUG) {
                printf("[detect] ch%d RISE level=%.3f thresh=%.3f hyst=%.3f dir=%d\r\n",
                       self->channel, level, self->change.threshold, self->change.hysteresis, self->change.direction);
            }
            self->state = 1;
            self->change_rise_count++;
            if (self->change.direction != -1) { // not 'falling' only
                if (self->action) {
                    (*self->action)(self->channel, (float)self->state);
                }
            }
        }
    }
}

static void d_window(Detect_t* self, float level, bool block_boundary) {
    // Window mode: Process every sample for accurate threshold detection
    (void)block_boundary; // Not used for window detection
    
    // Find which window contains the level WITH HYSTERESIS
    // Hysteresis prevents rapid toggling when voltage is near a boundary
    int ix = 0;
    int lastWin = self->win.lastWin;
    float hyst = self->win.hysteresis;
    
    for (; ix < self->win.wLen; ix++) {
        float boundary = self->win.windows[ix];
        float effective_boundary = boundary;
        
        // Apply hysteresis based on which side of this boundary we're coming from
        // If we're currently in a lower window (lastWin <= ix+1), add hysteresis
        // This means we need to go ABOVE (boundary + hyst) to cross upward
        // If we're currently in a higher window (lastWin > ix+1), subtract hysteresis
        // This means we need to go BELOW (boundary - hyst) to cross downward
        if (lastWin <= ix + 1) {
            // Coming from below or at this window - need to exceed boundary + hyst to go up
            effective_boundary = boundary + hyst;
        } else {
            // Coming from above - need to go below boundary - hyst to go down
            effective_boundary = boundary - hyst;
        }
        
        if (level < effective_boundary) {
            break;
        }
    }
    ix++; // 1-based index
    
    // Check if window has changed
    if (ix != lastWin) {
        if (self->action) {
            (*self->action)(self->channel, (ix > lastWin) ? ix : -ix);
        }
        self->win.lastWin = ix;
    }
}

static void d_scale(Detect_t* self, float level, bool block_boundary) {
    // Scale mode: Process every sample for accurate note detection
    (void)block_boundary; // Not used for scale detection
    
    D_scale_t* s = &self->scale;
    
    if (level > s->upper || level < s->lower) {
        // Offset input to capture noisy notes at divisions
        level += s->offset;
        
        // Calculate scale position
        float norm = level / s->scaling;
        s->lastOct = (int)floorf(norm);
        float phase = norm - (float)s->lastOct;
        float fix = phase * s->sLen;
        s->lastIndex = (int)floorf(fix);
        
        // Ensure index is within bounds
        if (s->lastIndex >= s->sLen) s->lastIndex = s->sLen - 1;
        if (s->lastIndex < 0) s->lastIndex = 0;
        
        // Calculate output values
        float note = s->scale[s->lastIndex];
        s->lastNote = note + (float)s->lastOct * s->divs;
        s->lastVolts = (note / s->divs + (float)s->lastOct) * s->scaling;
        
        // Trigger callback
        if (self->action) {
            (*self->action)(self->channel, 0.0f); // Value is accessed via scale members
        }
        
        // Update bounds for next detection
        scale_bounds(self, s->lastIndex, s->lastOct);
    }
}

static void d_volume(Detect_t* self, float level, bool block_boundary) {
    if (self->vu) {
        level = VU_step(self->vu, level);
    }
    
    // Volume mode: Only decrement countdown on block boundaries for correct timing
    if (block_boundary) {
        if (--self->volume.countdown <= 0) {
            self->volume.countdown = self->volume.blocks;
            if (self->action) {
                (*self->action)(self->channel, level);
            }
        }
    }
}

static void d_peak(Detect_t* self, float level, bool block_boundary) {
    (void)block_boundary; // Process every sample for accurate peak detection
    
    if (self->vu) {
        level = VU_step(self->vu, level);
    }
    
    // Peak envelope processing
    if (level > self->last) {
        self->peak.envelope = level; // Instant attack
    } else {
        // Release with 1-pole filter
        self->peak.envelope = level + self->peak.release * (self->peak.envelope - level);
    }
    
    // Threshold detection with hysteresis
    if (self->state) { // high to low
        if (self->peak.envelope < (self->peak.threshold - self->peak.hysteresis)) {
            self->state = 0;
        }
    } else { // low to high
        if (self->peak.envelope > (self->peak.threshold + self->peak.hysteresis)) {
            self->state = 1;
            if (self->action) {
                (*self->action)(self->channel, 0.0f); // Peak detected
            }
        }
    }
    
    self->last = level;
}

// Helper function for scale bounds calculation
static void scale_bounds(Detect_t* self, int ix, int oct) {
    D_scale_t* s = &self->scale;
    
    // Find ideal voltage for this window
    float ideal = ((float)oct * s->scaling) + ix * s->win;
    ideal = ideal - s->offset;
    
    // Calculate bounds with hysteresis
    // Use crow's formula - small hysteresis to avoid window gaps/overlaps
    s->lower = ideal - s->hyst;
    s->upper = ideal + s->hyst + s->win;
    
    // Convert to integer ADC counts for fast ISR comparison
    // ADC_TO_VOLTS = 0.002930, so VOLTS_TO_ADC = 341.297
    static const float VOLTS_TO_ADC = 341.297f;
    s->lower_int = (int16_t)(s->lower * VOLTS_TO_ADC);
    s->upper_int = (int16_t)(s->upper * VOLTS_TO_ADC);
    
    // CRITICAL: Memory barrier ensures Core 1 sees the new bounds
    // Without this, Core 1's cache might have stale values for several milliseconds
    DMB();
}

// ========================================================================
// ULTRA-FAST ISR Processing - INTEGER ONLY, NO FLOATING POINT!
// Runs on Core 1 at the ProcessSample rate (PROCESS_SAMPLE_RATE_HZ) - must stay well inside one sample period
// Only tracks state changes, defers callbacks to Core 0
// ========================================================================
void __not_in_flash_func(Detect_process_sample)(int channel, int16_t raw_adc) {
    if (channel >= detector_count || !detectors) return;
    
    Detect_t* detector = &detectors[channel];
    
    // ===============================================
    // EARLY EXIT: Skip if detection disabled (~0.5µs)
    // ===============================================
    if (detector->modefn == d_none) {
        return;
    }
    
    // ===============================================
    // MODE SWITCHING CHECK: Skip during reconfiguration
    // ===============================================
    // CRITICAL: Check mode_switching flag with memory barrier
    // Prevents ISR from processing during mode reconfiguration
    DMB();  // Ensure we read the latest flag value
    if (detector->mode_switching) {
        // Mode is being reconfigured on Core 0, skip processing
        detector->last_raw_adc = raw_adc;
        return;
    }
    
    // ===============================================
    // INTEGER-ONLY BLOCK TRACKING (~0.5µs) FOR MODES THAT NEED IT
    // ===============================================
    Detect_mode_fn_t mode = detector->modefn;
    bool block_boundary = false;
    if (mode == d_volume || mode == d_peak) {
        detector->sample_counter++;
        if (detector->sample_counter >= (uint32_t)DETECT_BLOCK_SIZE) {
            detector->sample_counter = 0;
            block_boundary = true;
        }
    }
    
    // ===============================================
    // MODE-SPECIFIC INTEGER PROCESSING
    // All comparison done in raw ADC counts (int16_t)
    // NO floating-point operations!
    // ===============================================
    
    // STREAM MODE: sample-accurate countdown in ISR
    if (mode == d_stream) {
        detector->last_raw_adc = raw_adc;

        // Protect against zero/invalid intervals
        if (detector->stream.interval_samples == 0) {
            detector->stream.interval_samples = 1;
        }

        if (--detector->stream.sample_countdown == 0) {
            detector->stream.sample_countdown = detector->stream.interval_samples;
            detector->event_raw_value = raw_adc; // capture the exact sample
            detector->state_changed = true;
            DMB(); // Ensure Core 0 sees the flag and sample value
        }
        return;
    }
    
    // CHANGE MODE: Integer threshold comparison
    if (mode == d_change) {
        if (detector->state) { // high to low
            if (raw_adc < (detector->threshold_raw - detector->hysteresis_raw)) {
                detector->state = 0;
                detector->change_fall_count++;
                
                if (detector->change.direction != 1) { // not 'rising' only
                    // Queue event for Core 0
                    detector->state_changed = true;
                    detector->event_raw_value = raw_adc;
                    DMB();  // Ensure flag write is visible
                }
            }
        } else { // low to high
            if (raw_adc > (detector->threshold_raw + detector->hysteresis_raw)) {
                detector->state = 1;
                detector->change_rise_count++;
                
                if (detector->change.direction != -1) { // not 'falling' only
                    // Queue event for Core 0
                    detector->state_changed = true;
                    detector->event_raw_value = raw_adc;
                    DMB();  // Ensure flag write is visible
                }
            }
        }
        detector->last_raw_adc = raw_adc;
        return; // EXIT: ~3-4µs total
    }
    
    // VOLUME/PEAK MODE: Skip per-sample, only process blocks
    if (mode == d_volume || mode == d_peak) {
        if (block_boundary) {
            // Queue event for Core 0 (it will do VU meter processing)
            detector->state_changed = true;
            detector->event_raw_value = raw_adc;
            DMB();  // Ensure flag write is visible
        }
        detector->last_raw_adc = raw_adc;
        return; // EXIT: ~2µs on boundary, ~1µs otherwise
    }
    
    // WINDOW/SCALE MODE: Integer-only bounds checking in ISR
    // Only signal Core 0 when bounds are crossed (not every sample!)
    if (mode == d_window || mode == d_scale) {
        // CRITICAL: Memory barrier before reading volatile bounds
        DMB();  // Ensure we see latest bounds from scale_bounds()
        
        // Convert float bounds to integer for comparison
        // These are updated when scale/window changes, so read-only here
        int16_t upper_int = detector->scale.upper_int;
        int16_t lower_int = detector->scale.lower_int;
        
        // Check if we've crossed the boundary
        if (raw_adc > upper_int || raw_adc < lower_int) {
            // Crossed boundary! Signal Core 0 to do FP math and fire callback
            detector->event_raw_value = raw_adc;
            detector->state_changed = true;
            DMB();  // Ensure flag write is visible to Core 0
        }
        
        detector->last_raw_adc = raw_adc;
        return; // EXIT: ~1-2µs
    }
    
    // Store last value for next iteration
    detector->last_raw_adc = raw_adc;
}

// ========================================================================
// CORE 0 Event Processing - Does FP conversion and fires callbacks
// Called from MainControlLoop event processing
// Has ~100µs per iteration, plenty of time for complex FP math
// ========================================================================
void Detect_process_events_core0(void) {
    if (!detectors) return;
    
    static const float ADC_TO_VOLTS = 0.002930f;
    
    for (int ch = 0; ch < detector_count; ch++) {
        Detect_t* detector = &detectors[ch];
        
        // CRITICAL: Memory barrier before reading event flag
        DMB();  // Ensure we see latest state_changed from ISR

        if (detector->modefn == d_stream) {
            // Preferred path: ISR-generated sample-accurate events
            if (detector->state_changed) {
                detector->state_changed = false;
                DMB(); // ensure clear visible to ISR

                int16_t raw_value = detector->event_raw_value;
                float level_volts = (float)raw_value * ADC_TO_VOLTS;
                detector->last_sample = level_volts;
                if (detector->action) {
                    detector->action(ch, level_volts);
                }
                continue;
            }

            // Fallback: legacy block-based scheduling (kept for compatibility)
            const uint32_t block_samples = (uint32_t)DETECT_BLOCK_SIZE;

            // Protect against wraparound or uninitialized state
            uint64_t current_samples = global_sample_counter;
            uint64_t last_samples = detector->stream.last_sample_counter;
            if (current_samples < last_samples) {
                detector->stream.last_sample_counter = current_samples;
                continue;
            }

            uint64_t delta_samples = current_samples - last_samples;
            if (delta_samples < block_samples) {
                continue; // No full blocks elapsed since last check
            }

            uint64_t blocks_elapsed = delta_samples / block_samples;
            detector->stream.last_sample_counter += blocks_elapsed * block_samples;

            float level_volts = (float)detector->last_raw_adc * ADC_TO_VOLTS;
            while (blocks_elapsed--) {
                detector->stream.countdown--;
                if (detector->stream.countdown <= 0) {
                    detector->stream.countdown = detector->stream.blocks;
                    if (detector->action) {
                        detector->action(ch, level_volts);
                    }
                }
            }
            continue;
        }
        
        // Check if this channel has a pending event
        if (!detector->state_changed) continue;
        
        // ATOMIC CLEAR: Clear flag immediately to avoid missing new events
        detector->state_changed = false;
        DMB();  // Ensure ISR sees the cleared flag
        
        // For scale/window modes, use last_raw_adc (always current)
        // For other modes, use event_raw_value (captured at event time)
        int16_t raw_value = (detector->modefn == d_window || detector->modefn == d_scale) 
                            ? detector->last_raw_adc 
                            : detector->event_raw_value;
        
        // NOW do the floating-point conversion (on Core 0!)
        float level_volts = (float)raw_value * ADC_TO_VOLTS;
        
        // Update last_sample for Core 0 use
        detector->last_sample = level_volts;
        
        // For scale/window modes, Core 1 already did integer bounds check
        // Now do FP math to calculate the actual note/window and fire callback
        if (detector->modefn == d_window || detector->modefn == d_scale) {
            // Call the mode function with the captured voltage
            if (!detector->mode_switching) {
                detector->modefn(detector, level_volts, false);
            }
            continue; // Mode function already fired callback
        }
        
        // For simple modes (stream/change/volume/peak), just fire the callback
        if (detector->action) {
            (*detector->action)(ch, detector->modefn == d_change ? (float)detector->state : level_volts);
        }
    }
}
